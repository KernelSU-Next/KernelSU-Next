/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SUSFS_HOOK_ENGINE_H
#define _SUSFS_HOOK_ENGINE_H

#include <linux/types.h>
#include "susfs_hook_map.h"

bool susfs_hook_map_ready(void);
bool susfs_hook_slot_enabled(enum susfs_hook_slot slot);
unsigned long susfs_hook_slot_resolve(enum susfs_hook_slot slot);
const char *susfs_hook_slot_name(enum susfs_hook_slot slot);
void susfs_hook_map_report(void);

#endif
