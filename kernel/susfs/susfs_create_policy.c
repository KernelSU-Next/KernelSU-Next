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

#include "susfs.h"
#include "susfs_create_vfs.h"
#include "susfs_create_policy.h"

bool susfs_create_name_hidden(int dfd, const char __user *path)
{
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
	susfs_create_vfs_putname_after_lookup(name);
	if (rc)
		return false;		/* absent: the kernel's own answer */

	hidden = sus_path_inode_hidden_public(d_inode(p.dentry));
	if (hidden)
		susfs_create_vfs_parent_permission(d_inode(p.dentry->d_parent));
	susfs_create_vfs_path_put(&p);
	return hidden;
}

int susfs_create_existing_answer(struct inode *inode)
{
	return sus_path_inode_hidden_public(inode) ? -EACCES : -EEXIST;
}
