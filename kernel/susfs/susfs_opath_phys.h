/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_opath_phys.h - O_PATH layer table and wrappers (P layer)
 */

#ifndef __SUSFS_OPATH_PHYS_H
#define __SUSFS_OPATH_PHYS_H

#include <linux/types.h>
#include <linux/fs.h>

#include "susfs_fp_hook.h"
#include "susfs_hooks.h"

extern struct susfs_fp_hook opath_hooks[];
extern struct susfs_hook opath_reg[];
extern const unsigned int opath_hooks_count;

/* Resolved by the P layer, checked by the F layer's init. */
extern struct file *(*opath_fget_raw)(unsigned int fd);
extern struct file *(*opath_close_fd)(unsigned int fd);
extern int (*opath_close_fd_get_file)(unsigned int fd, struct file **res);
extern bool opath_close_fd_extra_ref;
extern const char *opath_close_fd_name;
bool opath_close_fd_resolved(void);
void *opath_close_fd_pointer(void);
void susfs_opath_dump_all(void);

#endif /* __SUSFS_OPATH_PHYS_H */
