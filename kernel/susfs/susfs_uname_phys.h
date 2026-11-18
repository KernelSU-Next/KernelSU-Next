/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SUSFS_UNAME_PHYS_H
#define __SUSFS_UNAME_PHYS_H

#include <linux/compiler_types.h>
#include <linux/types.h>
#include "susfs_uname_values.h"

struct susfs_hook_probe;
extern const struct susfs_hook_probe susfs_uname_probe;

void susfs_uname_read_current(struct susfs_uname_values *out);
/* stop_on_fault preserves the kretprobe's short circuit; inline always
 * attempts both fields. Neither path changes the original syscall result. */
void susfs_uname_write_user(void __user *name, const struct susfs_uname_values *values,
			    bool stop_on_fault);

#endif
