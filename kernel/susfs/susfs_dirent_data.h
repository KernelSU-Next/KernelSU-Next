/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_dirent_data.h - getdents prefilter state (D layer)
 */

#ifndef __SUSFS_DIRENT_DATA_H
#define __SUSFS_DIRENT_DATA_H

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/fs.h>

extern int (*susfs_dirent_iterate_dir)(struct file *, struct dir_context *);
extern struct file *(*susfs_dirent_fget)(unsigned int);
extern bool susfs_dirent_armed;
extern bool susfs_dirent_prefilter_on;
extern atomic_t susfs_dirent_calls;
extern atomic_t susfs_dirent_hidden;

void susfs_dirent_data_init(void);

#endif /* __SUSFS_DIRENT_DATA_H */
