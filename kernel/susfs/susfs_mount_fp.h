/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_mount_fp.h - the mount-equivalent syscall face of SUS_MOUNT
 */
#ifndef __SUSFS_MOUNT_FP_H
#define __SUSFS_MOUNT_FP_H

int susfs_mount_fp_init(void);
void susfs_mount_fp_exit(void);

#endif /* __SUSFS_MOUNT_FP_H */
