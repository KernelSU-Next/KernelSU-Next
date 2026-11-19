/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_dirent_phys.h - getdents record layout and syscall execution (P layer)
 */

#ifndef __SUSFS_DIRENT_PHYS_H
#define __SUSFS_DIRENT_PHYS_H

#include <linux/types.h>
#include <linux/fs.h>

/* Fill the caller's buffer through iterate_dir() with the filtering filldir.
 * Returns the byte count or a negative errno. */
long susfs_dirent_run(struct file *f, void __user *dirp, unsigned int count,
		      bool compat);

#endif /* __SUSFS_DIRENT_PHYS_H */
