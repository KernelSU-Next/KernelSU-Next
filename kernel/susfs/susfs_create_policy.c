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

bool susfs_create_name_hidden(int dfd, const char __user *path,
			      enum susfs_create_kind kind, bool link_old)
{
	struct susfs_create_view v;
	struct susfs_create_decision d;
	struct inode *inode;
	struct filename *name;
	struct path p;
	bool hidden;
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
	hidden = sus_path_inode_hidden_public(inode);

	memset(&v, 0, sizeof(v));
	v.kind = kind;
	v.state = SUSFS_CREATE_ST_EXISTS;
	v.link_old = link_old;
	v.ino = inode->i_ino;
	v.s_dev = inode->i_sb->s_dev;
	v.i_uid = 0;
	v.sb_magic = inode->i_sb->s_magic;
	v.caller_uid = 0;
	strscpy(v.name, name->name, sizeof(v.name));
	susfs_create_vfs_putname_after_lookup(name);

	/* The old D answer is still the source of truth; the view only carries it
	 * through the pure decision until sus_path_rule_match() replaces it. */
	v.rule_matched = hidden;
	v.hide_from_apps = hidden;
	d = susfs_create_decide_core(&v);
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
