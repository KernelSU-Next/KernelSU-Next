// SPDX-License-Identifier: GPL-2.0
/*
 * susfs_hook_engine.c - consume the loader-patched inline capability map and
 * own the one HooKern initialization.
 *
 * The map is advisory: ready == 0 (ksud, a plain insmod, a failed probe) leaves
 * every call site on its current mechanism.  A set bit means the loader observed
 * an out-of-line entry for that unique symbol; callers still resolve the body
 * through KallRecon and still validate its size/landing before patching.
 *
 * hide_syms resolves s_show/m_show through their seq_operations identity, not
 * through a plain kallsyms name.  Those two names are registered as overrides
 * before the hook is installed; the same resolver serves every other consumer.
 */
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/compiler.h>
#include "susfs_hook_engine.h"
#include "susfs_log.h"
#include "symbol_resolver.h"
#include "hk.h"

#define SUSFS_HK_OVERRIDES 8

static struct {
	char name[48];
	unsigned long addr;
} susfs_hk_override[SUSFS_HK_OVERRIDES];
static unsigned int susfs_hk_noverride;
static bool susfs_hk_inited;

static unsigned long susfs_hk_resolve(const char *name)
{
	unsigned int i;

	for (i = 0; i < susfs_hk_noverride; i++)
		if (!strcmp(name, susfs_hk_override[i].name))
			return susfs_hk_override[i].addr;
	return find_kernel_symbol_exact(name);
}

void susfs_hook_override(const char *name, unsigned long addr)
{
	unsigned int i;

	if (!name || !addr)
		return;
	for (i = 0; i < susfs_hk_noverride; i++) {
		if (!strcmp(name, susfs_hk_override[i].name)) {
			susfs_hk_override[i].addr = addr;
			return;
		}
	}
	if (susfs_hk_noverride >= SUSFS_HK_OVERRIDES) {
		pr_warn("hook engine: override table full, ignoring %s\n", name);
		return;
	}
	strscpy(susfs_hk_override[i].name, name,
		sizeof(susfs_hk_override[i].name));
	susfs_hk_override[i].addr = addr;
	susfs_hk_noverride++;
}

bool susfs_hook_engine_active(void)
{
	return susfs_hk_inited;
}

int susfs_hook_engine_init(void)
{
	struct hk_cfg cfg = { .resolve = susfs_hk_resolve };
	int rc;

	if (susfs_hk_inited)
		return 0;
	rc = hk_init(&cfg);
	if (rc) {
		pr_warn("hook engine: hk_init failed %d - inline consumers fall back\n", rc);
		return rc;
	}
	susfs_hk_inited = true;
	return 0;
}

void susfs_hook_engine_exit(void)
{
	if (!susfs_hk_inited)
		return;
	hk_exit();
	susfs_hk_inited = false;
}

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
	return susfs_hk_resolve(name);
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
