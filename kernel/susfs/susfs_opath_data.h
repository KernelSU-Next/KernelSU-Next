/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_opath_data.h - O_PATH layer counters and switches (D layer)
 */

#ifndef __SUSFS_OPATH_DATA_H
#define __SUSFS_OPATH_DATA_H

#include <linux/types.h>
#include <linux/atomic.h>

extern atomic_t n_opath_seen;
extern atomic_t n_opath_opath;
extern atomic_t n_opath_hidden;
extern atomic_t n_opath_deny;
extern atomic_t n_opath_err;
extern int opath_test;
extern int opath_dump;
extern bool opath_exiting;

#endif /* __SUSFS_OPATH_DATA_H */
