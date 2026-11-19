/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_or_entry_data.h - open_redirect name-layer state (D layer)
 */

#ifndef __SUSFS_OR_ENTRY_DATA_H
#define __SUSFS_OR_ENTRY_DATA_H

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/namei.h>

#include "susfs_or_entry.h"

struct susfs_or_symbols {
	struct filename *(*getname_flags)(const char __user *name, int flags, int *empty);
	int (*filename_lookup)(int dfd, struct filename *name, unsigned int flags,
			       struct path *path, struct path *root);
	void (*putname)(struct filename *name);
	struct file *(*filp_open)(const char *name, int flags, umode_t mode);
	int (*get_unused_fd_flags)(unsigned int flags);
	void (*fd_install)(unsigned int fd, struct file *file);
	void (*put_unused_fd)(unsigned int fd);
	void (*fsnotify_open)(struct file *file);
	int (*check_zeroed_user)(const void __user *from, unsigned long count);
};

extern struct susfs_or_symbols susfs_or_syms;
extern bool susfs_or_entry_ready;
extern bool susfs_or_entry_param;
extern atomic_t susfs_or_entry_hits;

#endif /* __SUSFS_OR_ENTRY_DATA_H */
