/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_supercall_policy.h - supercall decision and dispatch (L layer)
 */

#ifndef __SUSFS_SUPERCALL_POLICY_H
#define __SUSFS_SUPERCALL_POLICY_H

#include <linux/types.h>

/* Which feature owns @cmd, or -1 when no feature layer does. */
int susfs_supercall_cmd_feature(unsigned int cmd);

/* True when this command should be swallowed (and deferred) right now. */
bool susfs_supercall_cmd_handled(unsigned int cmd);

/* Run one deferred command.  *arg points at the userspace payload struct. */
void susfs_supercall_dispatch(unsigned int cmd, void __user **arg);

#endif /* __SUSFS_SUPERCALL_POLICY_H */
