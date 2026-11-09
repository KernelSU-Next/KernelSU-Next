/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_create_policy.c - create gate policy
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/namei.h>
#include <linux/dcache.h>
#include <linux/path.h>
#include <linux/cred.h>
#include <linux/string.h>

#include "susfs.h"
#include "susfs_create_vfs.h"
#include "susfs_create_policy.h"
#include "susfs_create_view.h"
#include "sus_path_match.h"

bool susfs_create_name_hidden(int dfd, const char __user *path,
			      enum susfs_create_kind kind, bool link_old)
{
	struct susfs_create_view v;
	struct susfs_create_decision d;
	struct susfs_create_view *vp = &v;
	struct sus_path_match m;
	struct inode *inode;
	struct filename *name;
	struct path p;
	int rc;

	if (!sus_path_user_rules_present())
		return false;

	name = susfs_create_vfs_getname(path);
	if (IS_ERR(name))
		return false;

	rc = susfs_create_vfs_lookup(dfd, name, &p);
	if (rc) {
		susfs_create_vfs_putname_after_lookup(name);
		return false;		/* absent: the kernel's own answer */
	}

	inode = d_inode(p.dentry);
	if (!inode) {
		susfs_create_vfs_putname_after_lookup(name);
		susfs_create_vfs_path_put(&p);
		return false;
	}
	memset(vp, 0, sizeof(*vp));
	vp->kind = kind;
	vp->state = SUSFS_CREATE_ST_EXISTS;
	vp->link_old = link_old;
	vp->ino = inode->i_ino;
	vp->s_dev = inode->i_sb->s_dev;
	vp->i_uid = inode->i_uid.val;
	vp->sb_magic = inode->i_sb->s_magic;
	vp->caller_uid = current_uid().val;
	vp->caller_is_app = susfs_is_current_proc_umounted_app();
	vp->caller_is_su = susfs_control_visible();
	vp->control_visible = susfs_control_visible();
	vp->hide_from_apps = sus_path_hide_from_apps();
	strscpy(vp->name, name->name, sizeof(vp->name));
	susfs_create_vfs_putname_after_lookup(name);

	rc = sus_path_rule_match(vp, &m);
	if (rc) {
		susfs_create_vfs_path_put(&p);
		return false;
	}
	vp->rule_matched = m.rule_matched;
	vp->ordinary_rule = m.ordinary_rule;
	vp->self_protect = m.self_protect;
	vp->pending = m.pending;

	d = susfs_create_decide_core(vp);
	if (d.need_parent_permission)
		susfs_create_vfs_parent_permission(d_inode(p.dentry->d_parent));
	susfs_create_vfs_path_put(&p);

	return d.action == SUSFS_CREATE_HIDE ||
	       d.action == SUSFS_CREATE_HIDE_OLD ||
	       d.action == SUSFS_CREATE_HIDE_NEW;
}

int susfs_create_existing_answer(struct inode *inode)
{
	return sus_path_inode_hidden_public(inode) ? -EACCES : -EEXIST;
}
