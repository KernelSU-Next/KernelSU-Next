// SPDX-License-Identifier: GPL-2.0-only
#include <linux/printk.h>
#include "../lib/core.h"
#include "dbg.h"

void sprint_symbol(char *buf, unsigned long addr);

static void dbg_ti_info(void)
{
	unsigned short ti255 = 0;

	pr_info("[dbg] sprint=0x%lx kernel_base=0x%lx\n",
		sprint_addr, kernel_base);

	if (!klindex_addr) {
		pr_info("[dbg] ti: NOT FOUND\n");
		return;
	}

	safe_read(&ti255, (void *)(klindex_addr + 255 * 2), 2);
	pr_info("[dbg] ti=0x%lx ti255=%u kltable=0x%lx\n",
		klindex_addr, ti255, kltable_addr);
}

static void dbg_scan_range(void)
{
	unsigned long scan_start, scan_end;

	if (!kltable_addr) {
		pr_info("[dbg] scan: no kltable\n");
		return;
	}

	scan_start = kltable_addr > 0x400000 ?
		(kltable_addr - 0x400000) & ~0xFFFULL : kernel_base;
	if (klindex_addr)
		scan_end = (klindex_addr + 0x200000 + 0xFFF) & ~0xFFFULL;
	else
		scan_end = (kltable_addr + 0x200000 + 0xFFF) & ~0xFFFULL;

	pr_info("[dbg] scan=0x%lx-0x%lx (%luKB)\n",
		scan_start, scan_end, (scan_end - scan_start) / 1024);

	pr_info("[dbg] scan samples:\n");
	for (int i = 0; i < 3; i++) {
		unsigned long pt = scan_start + i * (scan_end - scan_start) / 3;
		u32 v;
		if (safe_read(&v, (void *)pt, 4))
			continue;
		pr_info("[dbg]   u32@0x%lx = %u (0x%x)\n", pt, v, v);
	}
}

static void dbg_discovery(void)
{
	pr_info("[dbg] sorted=%u layout=v%d kloffs=0x%lx\n",
		klnum_val, is_v1_layout ? 1 : 2, kloffs_addr);

	if (klnum_val)
		pr_info("[dbg] klbase=0x%lx=0x%lx\n", klbase_addr, klbase_val);

	pr_info("[dbg] klnum@0x%lx klnames@0x%lx\n", klnum_addr, klnames_addr);
	pr_info("[dbg] klmarks@0x%lx klseqs@0x%lx\n", klmarks_addr, klseqs_addr);
}

static void dbg_sprint_verify(void)
{
	char name[256];

	if (!kloffs_addr || !klbase_val) {
		pr_info("[dbg] sprint: no offsets or rb\n");
		return;
	}

	pr_info("[dbg] sprint verify (first 3):\n");
	for (int i = 0; i < 3; i++) {
		u32 o;
		if (safe_read(&o, (void *)(kloffs_addr + i * 4), 4))
			break;
		sprint_symbol(name, klbase_val + o);
		pr_info("[dbg]   offs[%d]=0x%x -> '%s'\n", i, o, name);
		if (name[0] == '0' && name[1] == 'x') {
			sprint_symbol(name, (u64)o);
			pr_info("[dbg]   retry abs: '%s'\n", name);
		}
	}
}

static void dbg_sample_names(void)
{
	char nbuf[256];
	int pts[] = {0, 1, 2, 3, 4, -1};
	unsigned int marker_off = 0;

	if (!klnames_addr || !klnum_val || !kltable_addr || !klindex_addr)
		return;

	pr_info("[dbg] name samples:\n");
	for (int p = 0; p < 6; p++) {
		int idx = pts[p];
		if (idx < 0)
			idx = (int)klnum_val / 2;

		unsigned int seq = get_sym_seq(idx);
		unsigned int off = get_sym_offset(seq);
		int sz = expand_sym(off, nbuf, sizeof(nbuf));
		if (!sz)
			continue;
		pr_info("[dbg]   [%d] '%s'\n", idx, nbuf);
	}
}

void dbg_dump(void)
{
	pr_info("[dbg] === diagnostic dump ===\n");
	dbg_ti_info();
	dbg_scan_range();
	dbg_discovery();
	dbg_sprint_verify();
	dbg_sample_names();
}
