/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_dirent_data.c - getdents prefilter state (D layer)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include "susfs_dirent_data.h"

int (*susfs_dirent_iterate_dir)(struct file *, struct dir_context *);
struct file *(*susfs_dirent_fget)(unsigned int);
bool susfs_dirent_armed;

bool susfs_dirent_prefilter_on = true;
module_param_named(prefilter, susfs_dirent_prefilter_on, bool, 0644);

atomic_t susfs_dirent_calls = ATOMIC_INIT(0);
atomic_t susfs_dirent_hidden = ATOMIC_INIT(0);

void susfs_dirent_data_init(void)
{
	/* nothing beyond the static initialisers */
}
