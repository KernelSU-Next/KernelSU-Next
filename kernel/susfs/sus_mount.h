/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUS_MOUNT_H
#define __SUS_MOUNT_H

#include <linux/types.h>
#include <linux/compiler_types.h>

/* The sus_mount control node: decides WHICH mounts count as ours (the on/off switch stays the hide_sus_mnts_for_non_su_procs supercall); root-only, ENOENT for everyone else through sus_path. */
#define SUSFS_HIDE_MOUNTS_NODE "/proc/susfs_hide_mounts"

int susfs_sus_mount_init(void);
void susfs_sus_mount_exit(void);

void susfs_sus_mount_supercall(void __user **arg);

#endif /* __SUS_MOUNT_H */
