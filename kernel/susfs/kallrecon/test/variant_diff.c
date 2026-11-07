// SPDX-License-Identifier: GPL-2.0-only
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/ktime.h>
#include <linux/time.h>
#include <linux/timekeeping.h>
#include "../lib/core.h"
#include "../lib/symbol.h"
#include "../lib/variant.h"
#include "dbg.h"

/* the pre slide implementation kept as the differential reference
 * one pass records the first matching entry and the first cfi_jt variant
 * reproducing both prefer_cfi_jt settings */
static void ref_find_variant(const char *name, unsigned long *first,
			     unsigned long *cfi)
{
	static const char cfi_suffix[] = ".cfi_jt";
	unsigned long off = 0;
	size_t name_len;
	unsigned int idx;

	*first = 0;
	*cfi = 0;

	if (!name || !name[0] || !klnum_val || !klnames_addr)
		return;

	name_len = strlen(name);

	mutex_lock(&ks_linear_lock);
	if (safe_read(ti_buf, (void *)klindex_addr, sizeof(ti_buf)) ||
	    safe_read(tt_buf, (void *)kltable_addr, sizeof(tt_buf))) {
		mutex_unlock(&ks_linear_lock);
		return;
	}

	for (idx = 0; idx < klnum_val; idx++) {
		unsigned char enc[2 + 256];
		char nbuf[256];
		unsigned int len, hdr = 1;
		unsigned long addr;
		size_t nlen;

		if (safe_read(enc, (void *)(klnames_addr + off), 1))
			break;
		len = enc[0];
		if (len & 0x80) {
			if (safe_read(enc + 1,
				      (void *)(klnames_addr + off + 1), 1))
				break;
			len = (len & 0x7F) | (enc[1] << 7);
			hdr = 2;
		}
		if (len > 256U ||
		    safe_read(enc + hdr,
			      (void *)(klnames_addr + off + hdr), len))
			break;
		off += hdr + len;

		if (!ks_expand_raw(enc, ti_buf, tt_buf, nbuf, sizeof(nbuf)))
			continue;

		nlen = strlen(nbuf);
		if (strcmp(nbuf, name) != 0) {
			if (nlen <= name_len ||
			    strncmp(nbuf, name, name_len) != 0 ||
			    (nbuf[name_len] != '.' && nbuf[name_len] != '$'))
				continue;
		}

		addr = sym_addr(idx);
		if (!addr)
			continue;

		if (!*first)
			*first = addr;
		if (nlen >= sizeof(cfi_suffix) - 1 &&
		    !strcmp(nbuf + nlen - (sizeof(cfi_suffix) - 1),
			    cfi_suffix)) {
			*cfi = addr;
			break;
		}
	}
	mutex_unlock(&ks_linear_lock);
}

static void vdiff_one(const char *q, int *count, int *fails)
{
	unsigned long ref0, ref1, new0, new1;

	ref_find_variant(q, &ref0, &ref1);
	if (!ref1)
		ref1 = ref0;

	new0 = kallrecon_find_variant(q, 0);
	new1 = kallrecon_find_variant(q, 1);

	(*count)++;
	if (new0 != ref0 || new1 != ref1) {
		(*fails)++;
		if (*fails <= 8)
			pr_info("[vdiff] MISMATCH '%s': exact 0x%lx/0x%lx cfi 0x%lx/0x%lx\n",
				q, new0, ref0, new1, ref1);
	}
}

void variant_diff_run(void)
{
	static const char *const fixed[] = {
		"kallsyms_lookup_name", "sprint_symbol", "init_mm",
	};
	char raw[256], base[256];
	unsigned int step;
	int count = 0, fails = 0;

	if (!klnum_val || !klnames_addr) {
		pr_info("[vdiff] no table\n");
		return;
	}

	step = klnum_val / 6 + 1;
	for (unsigned int i = step; i < klnum_val && i / step <= 6;
	     i += step) {
		unsigned int off = get_sym_offset(i);
		char *dot;

		if (!expand_sym(off, raw, sizeof(raw)))
			continue;
		vdiff_one(raw, &count, &fails);

		strscpy(base, raw, sizeof(base));
		dot = strpbrk(base, ".$");
		if (dot && dot - base >= 3) {
			*dot = '\0';
			vdiff_one(base, &count, &fails);
		}
	}

	for (int i = 0; i < 3; i++)
		vdiff_one(fixed[i], &count, &fails);

	vdiff_one("zzz_kallrecon_nonexist", &count, &fails);
	vdiff_one("kallsyms_lookup_nam", &count, &fails);

	{
		unsigned long a = 0, b = 0, c = 0;
		u64 t0 = ktime_get_ns();

		c = kallrecon_find_variant("sys_call_table", 0);
		u64 t1 = ktime_get_ns();

		ref_find_variant("sys_call_table", &a, &b);
		u64 t2 = ktime_get_ns();

		b = kallrecon_find_variant("sys_call_table", 0);
		u64 t3 = ktime_get_ns();

		pr_info("[vdiff] scan ms: new_cold=%llu ref_warm=%llu new_warm=%llu (0x%lx/0x%lx/0x%lx)\n",
			(t1 - t0) / 1000000, (t2 - t1) / 1000000,
			(t3 - t2) / 1000000, c, a, b);
	}

	pr_info("[vdiff] %d queries, %d mismatches\n", count, fails);
}
