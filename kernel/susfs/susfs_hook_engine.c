// SPDX-License-Identifier: GPL-2.0
/*
 * susfs_hook_engine.c - consume the loader-patched inline capability map.
 *
 * The map is advisory: ready == 0 (ksud, a plain insmod, a failed probe) leaves
 * every call site on its current mechanism.  A set bit means the loader observed
 * an out-of-line entry for that unique symbol; callers still resolve the body
 * through KallRecon and still validate its size/landing before patching.
 */
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/compiler.h>
#include "susfs_hook_engine.h"
#include "susfs_log.h"
#include "symbol_resolver.h"

bool susfs_hook_map_ready(void)
{
	struct susfs_hook_map *m = &susfs_hook_map;

	if (READ_ONCE(m->ready) != SUSFS_HOOK_MAP_READY)
		return false;
	if (READ_ONCE(m->abi) != SUSFS_HOOK_MAP_ABI)
		return false;
	if (READ_ONCE(m->nbits) != SUSFS_HOOK_SLOT_COUNT)
		return false;
	return true;
}

bool susfs_hook_slot_enabled(enum susfs_hook_slot slot)
{
	struct susfs_hook_map *m = &susfs_hook_map;
	u32 word;

	if ((unsigned int)slot >= SUSFS_HOOK_SLOT_COUNT || !susfs_hook_map_ready())
		return false;
	word = READ_ONCE(m->bits[slot / 32]);
	return !!(word & ((u32)1 << (slot % 32)));
}

const char *susfs_hook_slot_name(enum susfs_hook_slot slot)
{
	switch (slot) {
#define SUSFS_HOOK_NAME(name, sym)	case SUSFS_HOOK_##name: return #sym;
	SUSFS_HOOK_SLOTS(SUSFS_HOOK_NAME)
#undef SUSFS_HOOK_NAME
	default:
		return NULL;
	}
}

unsigned long susfs_hook_slot_resolve(enum susfs_hook_slot slot)
{
	const char *name = susfs_hook_slot_name(slot);

	if (!name)
		return 0;
	return find_kernel_symbol_exact(name);
}

void susfs_hook_map_report(void)
{
	struct susfs_hook_map *m = &susfs_hook_map;
	unsigned int i;

	if (!susfs_hook_map_ready()) {
		SUSFS_LOGI("hook map: not ready (ready=%08x abi=%u nbits=%u) - fallback everywhere\n",
			   READ_ONCE(m->ready), READ_ONCE(m->abi), READ_ONCE(m->nbits));
		return;
	}
	SUSFS_LOGI("hook map: ready abi=%u nbits=%u bits=", READ_ONCE(m->abi),
		   READ_ONCE(m->nbits));
	for (i = 0; i < SUSFS_HOOK_WORDS; i++)
		SUSFS_LOGI("%08x%s", READ_ONCE(m->bits[i]),
			   i + 1 == SUSFS_HOOK_WORDS ? "\n" : ":");
}
