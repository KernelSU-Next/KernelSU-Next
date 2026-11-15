/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUSFS_HIDE_SYMS_H
#define __SUSFS_HIDE_SYMS_H

#include <linux/types.h>

/* hide_modules (susfs_hide_syms.c): filters module NAMES out of /proc/modules (plus /sys/module and /proc/kallsyms for
 * non-root callers).  Node and same-named parameter are root-only; the node answers ENOENT through sus_path's hidden set. */
#define SUSFS_HIDE_MODULES_NODE "/proc/susfs_hide_modules"
bool susfs_hide_modules_active(void);

bool susfs_hide_modules_node_ready(void);

int susfs_hide_syms_init(void);
void susfs_hide_syms_exit(void);

/* Actual install state: enabled_features must not advertise a feature whose registration failed - hide_syms used to fail
 * silently and still be reported as active, the kind of inconsistency a detector looks for. */
bool susfs_hide_syms_active(void);

#endif /* __SUSFS_HIDE_SYMS_H */
