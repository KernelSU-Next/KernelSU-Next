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

/* Own the single HooKern hk_init()/hk_exit() lifetime.  A slot consumer must
 * resolve through susfs_hook_slot_resolve() and use hk_inline_hook() only when
 * susfs_hook_slot_enabled() is true. */
int susfs_hook_engine_init(void);
bool susfs_hook_engine_active(void);
void susfs_hook_engine_exit(void);
void susfs_hook_override(const char *name, unsigned long addr);

#endif
