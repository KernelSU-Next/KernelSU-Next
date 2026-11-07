/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sus_mount_view.c - P-owned mount walk
 *
 * v5 view contract: values only.  The walk holds one reference on the starting
 * mount; next() copies values and climbs mnt_parent.  No path string is
 * produced here yet, so V_PATH stays clear and callers must not assume it.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/mount.h>
#include <linux/dcache.h>

#include "mount.h"
#include "sus_mount_view.h"

#define SUS_MOUNT_WALK_MAX_DEPTH 64

struct sus_mount_walk {
	struct mount *ref;
	struct mount *cur;
	enum sus_mount_context ctx;
	unsigned int depth;
	bool ended;
};

static void sus_mount_view_fill(struct sus_mount_view *out, struct mount *m,
				     enum sus_mount_context ctx)
{
	struct dentry *root = m->mnt.mnt_root;
	struct inode *inode = root ? d_inode(root) : NULL;

	memset(out, 0, sizeof(*out));
	out->ctx = ctx;
	out->mnt_id = (int)m->mnt_id;
	out->valid_mask |= SUS_MOUNT_V_MNT_ID;
	if (m->mnt_parent && m->mnt_parent != m) {
		out->parent_id = (int)m->mnt_parent->mnt_id;
		out->valid_mask |= SUS_MOUNT_V_PARENT;
	}
	if (m->mnt.mnt_sb) {
		out->s_dev = m->mnt.mnt_sb->s_dev;
		out->valid_mask |= SUS_MOUNT_V_DEVID;
	}
	if (inode) {
		out->root_ino = inode->i_ino;
		out->valid_mask |= SUS_MOUNT_V_MNT_ID;
	}
	if (m->mnt_devname) {
		strscpy(out->source_devname, m->mnt_devname,
			sizeof(out->source_devname));
		out->source_len = (u16)strlen(out->source_devname);
		out->source = out->source_len && out->source_devname[0] == '/'
			       ? SUS_MOUNT_SRC_PATH : SUS_MOUNT_SRC_DEVNAME;
		out->valid_mask |= SUS_MOUNT_V_DEVNAME;
	}
}

void sus_mount_view_from_mount(struct sus_mount_view *out, struct mount *m,
			       enum sus_mount_context ctx)
{
	if (out && m)
		sus_mount_view_fill(out, m, ctx);
}


struct sus_mount_walk *sus_mount_walk_begin(struct mount *start,
					    enum sus_mount_context ctx)
{
	struct sus_mount_walk *walk;

	if (!start || !sus_mount_view_ctx_valid(ctx))
		return NULL;
	walk = kzalloc(sizeof(*walk), GFP_ATOMIC);
	if (!walk)
		return NULL;
	walk->ref = start;
	walk->cur = start;
	walk->ctx = ctx;
	mntget(&start->mnt);
	return walk;
}

bool sus_mount_walk_next(struct sus_mount_walk *walk, struct sus_mount_view *out,
			 int *err)
{
	if (err)
		*err = 0;
	if (!walk || !out || walk->ended || !walk->cur)
		return false;
	sus_mount_view_fill(out, walk->cur, walk->ctx);
	walk->depth++;
	if (walk->depth >= SUS_MOUNT_WALK_MAX_DEPTH ||
	    !walk->cur->mnt_parent || walk->cur->mnt_parent == walk->cur) {
		walk->ended = true;
	} else {
		walk->cur = walk->cur->mnt_parent;
	}
	return true;
}

void sus_mount_walk_end(struct sus_mount_walk *walk)
{
	if (!walk)
		return;
	if (walk->ref)
		mntput(&walk->ref->mnt);
	kfree(walk);
}
