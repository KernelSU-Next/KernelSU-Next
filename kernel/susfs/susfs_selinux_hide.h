/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_selinux_hide.h - selinux_hide entry layer (LSM replacement)
 */

#ifndef __SUSFS_SELINUX_HIDE_H
#define __SUSFS_SELINUX_HIDE_H

int susfs_selinux_hide_init(void);
void susfs_selinux_hide_exit(void);

#endif /* __SUSFS_SELINUX_HIDE_H */
