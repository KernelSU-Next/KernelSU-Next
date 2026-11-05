/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_create_data.c - create gate state
 */

#include <linux/kernel.h>
#include <linux/types.h>

#include "susfs_create_data.h"

static susfs_syscall_fn_t create_orig[SUSFS_CREATE_COUNT];
static susfs_syscall_fn_t create_orig_compat[SUSFS_CREATE_COUNT];

susfs_syscall_fn_t *susfs_create_orig_slot(enum susfs_create_syscall id)
{
	if ((unsigned int)id >= SUSFS_CREATE_COUNT)
		return NULL;
	return &create_orig[id];
}

susfs_syscall_fn_t *susfs_create_orig_compat_slot(enum susfs_create_syscall id)
{
	if ((unsigned int)id >= SUSFS_CREATE_COUNT)
		return NULL;
	return &create_orig_compat[id];
}
