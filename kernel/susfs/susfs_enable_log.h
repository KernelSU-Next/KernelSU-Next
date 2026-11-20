/* SPDX-License-Identifier: GPL-2.0 */
/*
 * susfs_enable_log.h - ENABLE_LOG feature interface (F layer)
 *
 * The logging macros and the flag query stay in susfs_log.h; this header only
 * carries the feature's own entry points.
 */
#ifndef __SUSFS_ENABLE_LOG_H
#define __SUSFS_ENABLE_LOG_H

int susfs_enable_log_init(void);
void susfs_enable_log_exit(void);
void susfs_enable_log_supercall(void __user **arg);

#endif /* __SUSFS_ENABLE_LOG_H */
