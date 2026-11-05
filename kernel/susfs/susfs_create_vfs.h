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

int susfs_create_vfs_init(void);
bool susfs_create_vfs_takeover_ready(void);

struct filename *susfs_create_vfs_getname(const char __user *name);
int susfs_create_vfs_lookup(int dfd, struct filename *name, struct path *path);
void susfs_create_vfs_putname_after_lookup(struct filename *name);
void susfs_create_vfs_path_put(struct path *path);
void susfs_create_vfs_parent_permission(struct inode *dir);

long __nocfi susfs_create_vfs_mkdirat(int dfd, const char __user *pathname, umode_t mode,
                                     int (*existing)(struct inode *inode));

#endif /* __SUSFS_CREATE_VFS_H */
