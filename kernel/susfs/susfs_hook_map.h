/* SPDX-License-Identifier: GPL-2.0 */
/*
 * susfs_hook_map.h - the inline-hook capability table shared by the module and
 * the user-space loader.
 *
 * The loader probes only symbols that are unique in the kernel kallsyms, then
 * writes 1 for a confirmed out-of-line entry into the module's .data map.  A
 * default-0 map is what ksud leaves behind; a map that was not written by the
 * loader has ready == 0 and the module keeps the current fallback for every slot.
 *
 * Duplicate names are deliberately absent: s_show/m_show are resolved through
 * their seq_operations identity, and seq_show is per-filesystem, so none of them
 * can be described by a single symbol bit.
 */
#ifndef _SUSFS_HOOK_MAP_H
#define _SUSFS_HOOK_MAP_H

#include <linux/types.h>

#define SUSFS_HOOK_MAP_ABI	1u
#define SUSFS_HOOK_MAP_READY	0x53484650u	/* "SHFP": written last by the loader */

/*
 * One bit per unique out-of-line function.  The order is ABI: do not renumber,
 * only append, and bump SUSFS_HOOK_MAP_ABI when the meaning changes.
 *
 * X(enum_name, kallsyms_name)
 */
#include "susfs_hook_slots.h"

enum susfs_hook_slot {
#define SUSFS_HOOK_ENUM(name, sym)	SUSFS_HOOK_##name,
	SUSFS_HOOK_SLOTS(SUSFS_HOOK_ENUM)
#undef SUSFS_HOOK_ENUM
	SUSFS_HOOK_SLOT_COUNT
};

#define SUSFS_HOOK_WORDS	((SUSFS_HOOK_SLOT_COUNT + 31) / 32)

struct susfs_hook_map {
	u32 ready;		/* SUSFS_HOOK_MAP_READY only after a complete probe */
	u32 abi;
	u32 nbits;
	u32 bits[SUSFS_HOOK_WORDS];	/* default 0; 1 means out-of-line observed */
};

#ifdef __KERNEL__
extern struct susfs_hook_map susfs_hook_map;
#endif

#ifndef __KERNEL__
#define SUSFS_HOOK_MAP_SECTION	".data.susfs_hook_map"
#else
#define SUSFS_HOOK_MAP_SECTION	".data.susfs_hook_map"
#endif

#endif /* _SUSFS_HOOK_MAP_H */
