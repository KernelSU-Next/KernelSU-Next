// SPDX-License-Identifier: GPL-2.0-only
/*
 * verify.c
 *
 * Copyright (C) 2026 dere3046
 */

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/kprobes.h>
#include <linux/kallsyms.h>
#include "../lib/core.h"
#include "verify.h"

/* kernels without the CFI backport (vanilla 5.10) do not define __nocfi */
#ifndef __nocfi
#define __nocfi
#endif

typedef int (*reg_kp_t)(struct kprobe *);
typedef void (*unreg_kp_t)(struct kprobe *);

static reg_kp_t reg_kp;
static unreg_kp_t unreg_kp;
static int kprobe_ok;

__nocfi
static int call_reg_kp(reg_kp_t fn, struct kprobe *kp)
{
	return fn(kp);
}

__nocfi
static void call_unreg_kp(unreg_kp_t fn, struct kprobe *kp)
{
	fn(kp);
}

typedef int (*sno_t)(char *, unsigned long);

__nocfi
static int call_sno(sno_t fn, char *buf, unsigned long addr)
{
	return fn(buf, addr);
}

static int probe_kprobe(void)
{
	reg_kp = (reg_kp_t)kallsyms_name_to_addr("register_kprobe");
	unreg_kp = (unreg_kp_t)kallsyms_name_to_addr("unregister_kprobe");

	if (!reg_kp || !unreg_kp) {
		kprobe_ok = -1;
		return 0;
	}

	unsigned long addr = kallsyms_name_to_addr("sprint_symbol");
	struct kprobe kp = {
		.addr = (kprobe_opcode_t *)addr,
		.flags = KPROBE_FLAG_DISABLED,
	};
	if (call_reg_kp(reg_kp, &kp) < 0) {
		kprobe_ok = -1;
		return 0;
	}
	call_unreg_kp(unreg_kp, &kp);
	kprobe_ok = 1;
	return 1;
}

static unsigned long resolve_addr(const char *name)
{
	if (kprobe_ok == 0)
		probe_kprobe();
	if (kprobe_ok < 0)
		return 0;

	unsigned long addr = kallsyms_name_to_addr(name);
	if (!addr)
		return 0;

	struct kprobe kp = {
		.addr = (kprobe_opcode_t *)addr,
		.flags = KPROBE_FLAG_DISABLED,
	};
	if (call_reg_kp(reg_kp, &kp) < 0)
		return 0;
	unsigned long ret = (unsigned long)kp.addr;
	call_unreg_kp(unreg_kp, &kp);
	return ret;
}

void verify_kallsyms(void)
{
	unsigned long test_addr;
	char truth[256], our[256];

	test_addr = resolve_addr("kallsyms_lookup_name");
	if (!test_addr) {
		pr_info("[kallrecon] verify: kprobe unavailable\n");
		return;
	}

	if (!klnum_val || !kloffs_addr ||
	    !klnames_addr || !kltable_addr || !klindex_addr) {
		pr_info("[kallrecon] verify: kallsyms data incomplete\n");
		return;
	}

	pr_info("[kallrecon] verify: bootstrapping...\n");

	sno_t sno = (sno_t)kallsyms_name_to_addr("sprint_symbol_no_offset");
	if (sno)
		call_sno(sno, truth, test_addr);
	else
		strcpy(truth, "(no sprint_symbol_no_offset)");

	int idx = sym_name_at(test_addr, our, sizeof(our));
	pr_info("[kallrecon] verify: addr->name [%d] '%s' %s\n", idx,
		our, strcmp(truth, our) == 0 ? "MATCH" : "MISMATCH");

	unsigned long lookup = kallsyms_name_to_addr("kallsyms_lookup_name");
	unsigned int op;
	int match = lookup == test_addr;

	/* x86 IBT: register_kprobe() lands4 bytes past the symbol
	 * start when the function opens with ENDBR64
	 * (arch_adjust_kprobe_addr), while sprint_symbol_no_offset
	 * reports the plain name; accept lookup+4 with an ENDBR64
	 * opcode (or its ftrace poison) at lookup as an exact hit */
	if (!match && lookup && test_addr == lookup + 4 &&
	    !safe_read(&op, (void *)lookup, 4) &&
	    (op == 0xFA1E0FF3U || op == 0x001F0F66U))
		match = 1;
	pr_info("[kallrecon] verify: name->addr 0x%lx %s\n",
		lookup, match ? "MATCH" : "MISMATCH");

}

void dump_kallsyms_layout(void)
{
	unsigned int num;

	if (!kloffs_addr || !klnum_val) {
		pr_info("[kallrecon] layout: insufficient data\n");
		return;
	}

	if (klbase_addr > kloffs_addr)
		num = (unsigned int)((klbase_addr - kloffs_addr) / 4);
	else
		num = klnum_val;

	pr_info("[kallrecon] layout: %u symbols, %u markers\n",
		num, (num + 255) / 256);
}
