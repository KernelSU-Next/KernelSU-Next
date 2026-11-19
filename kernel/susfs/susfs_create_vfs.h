/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_create_vfs.h - the create family's VFS primitives
 *
 * Physical layer: symbol resolution and the VFS calls the create gate needs.
 * The op contract lives in susfs_create_op.h.
 */

#ifndef __SUSFS_CREATE_VFS_H
#define __SUSFS_CREATE_VFS_H

#include <linux/types.h>

int susfs_create_vfs_init(void);

#endif /* __SUSFS_CREATE_VFS_H */
