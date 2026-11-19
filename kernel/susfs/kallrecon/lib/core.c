// SPDX-License-Identifier: GPL-2.0-only
/*
 * core.c
 *
 * Copyright (C) 2026 dere3046
 */

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/kallsyms.h>
#include <linux/version.h>
#include "core.h"
#include "ks_dbg.h"
#include "access.h"
#include "discover.h"
#include "fastboot.h"
#include "hint.h"

unsigned long sprint_addr;
unsigned long kernel_base;
unsigned long klbase_addr;
unsigned long klbase_val;
unsigned long kloffs_addr;
unsigned long klindex_addr;
unsigned long klseqs_addr;
int klseqs_stride = 3;
unsigned int  klnum_val;
unsigned long klmarks_addr;
unsigned long kltable_addr;
unsigned long klnames_addr;
unsigned long klnum_addr;

enum layout_v kl_layout = LAYOUT_V2;
int is_v1_layout;

unsigned long (*kallrecon_klp)(const char *name);
#ifdef KALLRECON_MODULE_LOOKUP
unsigned long (*kallrecon_module_klp)(const char *name); /* experimental, may be unstable */
#endif

static DEFINE_MUTEX(ks_lock);
static int ks_done;

static int ks_layout_ready(void)
{
	return kloffs_addr && klnames_addr && klnum_val;
}

static int ks_discover_once(void)
{
	if (!kr_discover_layout())
		return 0;
	if (!ks_layout_ready())
		kr_fail_set(KALLRECON_NO_LAYOUT);
	return 1;
}

static void ks_resolve_klp(void)
{
	unsigned long addr = 0;

#ifdef KALLRECON_FAST_BOOT
	/* only linear (no seqs) kernels need the walk, the seqs
	 * lookup is a fast binary search, KALLRECON_FAST_BOOT_ALL
	 * skips the seqs check */
	if (KALLRECON_FAST_BOOT_ALL || !klseqs_addr) {
		addr = fast_find_klp();
		ks_dbg("[kallrecon] fast boot: %s\n", addr ? "hit" : "miss");
	}
#endif
	if (!addr)
		addr = kallsyms_name_to_addr("kallsyms_lookup_name");
	if (addr)
		kallrecon_klp = (unsigned long (*)(const char *))addr;

#ifdef KALLRECON_MODULE_LOOKUP
	if (!kallrecon_module_klp) {
		unsigned long maddr =
			kallsyms_name_to_addr("module_kallsyms_lookup_name");

		if (maddr)
			kallrecon_module_klp =
				(unsigned long (*)(const char *))maddr;
	}
#endif
}

void find_kallsyms_base(void)
{
	mutex_lock(&ks_lock);
	if (!ks_done) {
		kr_discover_reset();
		if (ks_discover_once()) {
			ks_done = 1;
			if (ks_layout_ready())
				ks_resolve_klp();
		}
	}
	mutex_unlock(&ks_lock);
}

int kallrecon_retry(void)
{
	int ready;

	mutex_lock(&ks_lock);
	if (!ks_layout_ready()) {
		kr_discover_reset();
		if (!ks_discover_once()) {
			mutex_unlock(&ks_lock);
			return 0;
		}
		ks_done = 1;
	}
	if (ks_layout_ready() && !kallrecon_klp)
		ks_resolve_klp();
	ready = kallrecon_klp != NULL;
	mutex_unlock(&ks_lock);
	return ready;
}
