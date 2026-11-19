/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_opath_phys.h - O_PATH layer table and wrappers (P layer)
 */

#ifndef __SUSFS_OPATH_PHYS_H
#define __SUSFS_OPATH_PHYS_H

#include <linux/types.h>

#include "susfs_fp_hook.h"
#include "susfs_hooks.h"

extern struct susfs_fp_hook opath_hooks[];
extern struct susfs_hook opath_reg[];
extern const unsigned int opath_hooks_count;

#endif /* __SUSFS_OPATH_PHYS_H */
