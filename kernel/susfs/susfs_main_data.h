/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_main_data.h - module entry data (D layer)
 */

#ifndef __SUSFS_MAIN_DATA_H
#define __SUSFS_MAIN_DATA_H

#include <linux/types.h>

#include "susfs_hooks.h"

/* Default-0 capability map: the user-space loader writes the probed bits and
 * ready last; ksud leaves ready 0, so every slot keeps the current fallback. */
extern struct susfs_hook_map susfs_hook_map;
extern bool susfs_expose_proc;
extern const char *const susfs_self_hide_paths[];
extern const unsigned int susfs_self_hide_paths_count;

#endif /* __SUSFS_MAIN_DATA_H */
