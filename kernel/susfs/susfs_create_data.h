/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_create_data.h - create gate state
 */

#ifndef __SUSFS_CREATE_DATA_H
#define __SUSFS_CREATE_DATA_H

#include "susfs_fp_hook.h"

enum susfs_create_syscall {
	SUSFS_CREATE_MKDIRAT = 0,
	SUSFS_CREATE_MKNODAT,
	SUSFS_CREATE_SYMLINKAT,
	SUSFS_CREATE_LINKAT,
	SUSFS_CREATE_COUNT,
};

susfs_syscall_fn_t *susfs_create_orig_slot(enum susfs_create_syscall id);
susfs_syscall_fn_t *susfs_create_orig_compat_slot(enum susfs_create_syscall id);

#endif /* __SUSFS_CREATE_DATA_H */
