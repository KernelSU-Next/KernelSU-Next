/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_priority_uid.h - priority_uid_filter ABI and lifecycle
 */

#ifndef __SUSFS_PRIORITY_UID_H
#define __SUSFS_PRIORITY_UID_H

#include <linux/types.h>

struct cred;

/* ---- data/policy (susfs_priority_uid.c) ---- */
int susfs_priority_init(void);
void susfs_priority_exit(void);

int susfs_priority_uid_init(void);
void susfs_priority_uid_exit(void);

/* Parse a comma-separated package list and rebuild the filtered-uid bitmap from
 * /data/system/packages.list.  Returns 0 or -errno. */
int susfs_priority_uid_set(const char *apps);

/* uid (app id) is in the filter bitmap.  RCU-safe, callable from any context. */
bool susfs_priority_uid_filtered(uid_t uid);

/* Supercall hander: CMD_SUSFS_SET_PRIORITY_UID_FILTER. */
void susfs_priority_uid_supercall(void __user **arg);

/* ---- syscall hooks (susfs_priority_hooks.c) ---- */
int susfs_priority_hooks_init(void);
void susfs_priority_hooks_exit(void);

/* entry-stub hit (from susfs_ksu_stub.S, KernelSU observer context) */
void susfs_priority_observer_hit(void);

#endif /* __SUSFS_PRIORITY_UID_H */
