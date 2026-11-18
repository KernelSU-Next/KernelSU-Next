/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SUSFS_UNAME_DATA_H
#define __SUSFS_UNAME_DATA_H

#include <linux/mutex.h>
#include "susfs_fp_hook.h"
#include "susfs_uname_values.h"

/* Mutable state is private to L and the parameter adapter in P.
 * F may use only the orig accessor below. */
extern struct susfs_uname_values susfs_uname_state_values;
extern bool susfs_uname_enabled;
extern bool susfs_uname_registered;
extern bool susfs_uname_inline;
extern struct mutex susfs_uname_ctl_lock;

susfs_syscall_fn_t susfs_uname_orig_get(void);
void susfs_uname_data_pad(void);

#endif
