/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_create_vfs.h - the create family's VFS primitives
 *
 * Physical layer: symbol resolution and the VFS calls the create gate needs.
 * It makes no hidden/visible decision; callers pass the existing-name answer.
 */

#ifndef __SUSFS_CREATE_VFS_H
#define __SUSFS_CREATE_VFS_H

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/path.h>
#include <linux/dcache.h>

struct susfs_create_op;
struct susfs_create_view;

int susfs_create_vfs_init(void);
bool susfs_create_vfs_takeover_ready(void);

/* P-owned lifecycle for resolving one create target.  begin() returns an op even
 * when the lookup misses (status() then returns 0); NULL means begin itself
 * failed and *err carries the reason.  end() is called exactly once. */
struct susfs_create_op *susfs_create_vfs_begin(int dfd, const char __user *path,
					       int *err);
bool susfs_create_vfs_is_name(struct susfs_create_op *op);
int susfs_create_vfs_lock(struct susfs_create_op *op);
int susfs_create_vfs_status(struct susfs_create_op *op,
			    struct susfs_create_view *view);
int susfs_create_vfs_commit(struct susfs_create_op *op, umode_t mode);
struct inode *susfs_create_vfs_inode(struct susfs_create_op *op);
void susfs_create_vfs_parent_permission_op(struct susfs_create_op *op);
void susfs_create_vfs_end(struct susfs_create_op *op);

struct filename *susfs_create_vfs_getname(const char __user *name);
int susfs_create_vfs_lookup(int dfd, struct filename *name, struct path *path);
void susfs_create_vfs_putname_after_lookup(struct filename *name);
void susfs_create_vfs_path_put(struct path *path);
void susfs_create_vfs_parent_permission(struct inode *dir);

long __nocfi susfs_create_vfs_mkdirat(int dfd, const char __user *pathname, umode_t mode,
                                     int (*existing)(struct inode *inode));

#endif /* __SUSFS_CREATE_VFS_H */
