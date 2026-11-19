// SPDX-License-Identifier: GPL-2.0-only
/*
 * main.c
 *
 * Copyright (C) 2026 dere3046
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/printk.h>
#include <linux/init.h>
#include <linux/errno.h>

#include "../lib/core.h"
#include "../lib/hint.h"
#include "verify.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KallRecon ARM64 probe");

static unsigned long offsets;
static unsigned long base;
static unsigned long num;
static unsigned long names;
static unsigned long markers;
static unsigned long seqs;
static unsigned long token_table;
static unsigned long token_index;
static unsigned long scan_back;
static unsigned long scan_fwd;
static unsigned int timeout_ms;

module_param(offsets, ulong, 0444);
module_param(base, ulong, 0444);
module_param(num, ulong, 0444);
module_param(names, ulong, 0444);
module_param(markers, ulong, 0444);
module_param(seqs, ulong, 0444);
module_param(token_table, ulong, 0444);
module_param(token_index, ulong, 0444);
module_param(scan_back, ulong, 0444);
module_param(scan_fwd, ulong, 0444);
module_param(timeout_ms, uint, 0444);

static int __init probe_init(void)
{
	struct kallrecon_hint hint = {
		.offsets = offsets,
		.relative_base = base,
		.num_syms = num,
		.names = names,
		.markers = markers,
		.seqs = seqs,
		.token_table = token_table,
		.token_index = token_index,
		.scan_back = scan_back,
		.scan_fwd = scan_fwd,
		.timeout_ms = timeout_ms,
	};

	pr_info("[kallrecon] init\n");

	kallrecon_supply(&hint);
	find_kallsyms_base();
	dump_kallsyms_layout();

	if (!kloffs_addr) {
		pr_info("[kallrecon] discovery failed (%d)\n",
			kallrecon_fail_reason());
		return -ENODATA;
	}

	verify_kallsyms();

	return 0;
}

static void __exit probe_exit(void)
{
	pr_info("[kallrecon] exit\n");
}

module_init(probe_init);
module_exit(probe_exit);
