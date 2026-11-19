/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_supercall_phys.h - reboot(2) entry hooks and task_work delivery (P layer)
 */

#ifndef __SUSFS_SUPERCALL_PHYS_H
#define __SUSFS_SUPERCALL_PHYS_H

#include <linux/types.h>
#include <linux/ptrace.h>

#include "susfs_hooks.h"

/* Queue @cmd for process context and swallow the syscall.  Returns 0 when the
 * syscall was answered, a negative errno when the caller must fall through. */
int susfs_supercall_defer(unsigned int cmd, void __user *payload);

extern struct susfs_fp_hook susfs_supercall_reboot_fp;
extern const struct susfs_hook_probe susfs_supercall_reboot_probe;

#endif /* __SUSFS_SUPERCALL_PHYS_H */
