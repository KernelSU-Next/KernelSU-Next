/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_or_entry_phys.h - open_redirect name-layer symbols (P layer)
 */

#ifndef __SUSFS_OR_ENTRY_PHYS_H
#define __SUSFS_OR_ENTRY_PHYS_H

#include <linux/types.h>
#include <linux/fs.h>

/* Resolve every kernel symbol the name layer needs.  Returns 0 when the
 * required set is present, 1 otherwise (the layer stays off). */
int susfs_or_entry_symbols_resolve(void);

void susfs_or_fsnotify_open(struct file *file);
int susfs_or_check_zeroed_user(const void __user *from, unsigned long count);
void susfs_or_put_unused_fd(unsigned int fd);

#endif /* __SUSFS_OR_ENTRY_PHYS_H */
