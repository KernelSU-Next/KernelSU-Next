/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_create_op.h - P interface for one create operation
 *
 * The policy layer sees only the opaque operation and the value view; getname,
 * parentat, locks, dentries and path references stay behind this header.
 */

#ifndef __SUSFS_CREATE_OP_H
#define __SUSFS_CREATE_OP_H

#include <linux/types.h>

struct susfs_create_op;
struct susfs_create_view;

struct susfs_create_op *susfs_create_vfs_begin(int dfd, const char __user *path,
					       int *err);
bool susfs_create_vfs_is_name(struct susfs_create_op *op);
int susfs_create_vfs_lock(struct susfs_create_op *op);
int susfs_create_vfs_status(struct susfs_create_op *op,
			    struct susfs_create_view *view);
int susfs_create_vfs_commit(struct susfs_create_op *op, umode_t mode);
int susfs_create_vfs_path(struct susfs_create_op *op, char *buf, size_t len);
void susfs_create_vfs_parent_permission_op(struct susfs_create_op *op);
void susfs_create_vfs_end(struct susfs_create_op *op);

#endif /* __SUSFS_CREATE_OP_H */
