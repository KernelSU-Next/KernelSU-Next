// SPDX-License-Identifier: GPL-2.0-only
/*
 * access.c
 *
 * Copyright (C) 2026 dere3046
 */

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/uaccess.h>
#include <linux/kallsyms.h>
#include "core.h"
#include "ks_dbg.h"
#include "access.h"

int kl_addr_mode = KS_MODE_RB;

int safe_read(void *dst, const void *src, size_t sz)
{
	return copy_from_kernel_nofault(dst, src, sz);
}

/* empirically pick the address formula: try candidates in priority
 * order, the first one resolving to a real symbol wins. entries sampled
 * by the caller must sit inside [_stext, _end): head.text (arm64) and
 * the percpu block (x86) resolve to hex under every formula */
int ks_addr_try(unsigned long rb, u32 off, int *mode)
{
	char name[KSYM_SYMBOL_LEN];

#ifdef CONFIG_X86_64
	{
		s32 so = (s32)off;
		unsigned long a = so >= 0 ? (unsigned long)(u32)so
					  : rb - 1 - so;

		sprint_symbol(name, a);
		ks_dbg("[kallrecon] try abs 0x%x -> 0x%lx '%s'\n", off, a, name);
		if (!(name[0] == '0' && name[1] == 'x')) {
			*mode = KS_MODE_ABSPCPU;
			return 1;
		}
	}
#endif
	sprint_symbol(name, rb + off);
	ks_dbg("[kallrecon] try rb  0x%x -> 0x%lx '%s'\n", off, rb + off, name);
	if (!(name[0] == '0' && name[1] == 'x')) {
		*mode = KS_MODE_RB;
		return 1;
	}
	/* rb+off may land in the .head.text gap; kernel_base fixes the
	 * verification while the rb+off value itself stays correct */
	sprint_symbol(name, kernel_base + off);
	ks_dbg("[kallrecon] try kb  0x%x -> 0x%lx '%s'\n", off, kernel_base + off,
		name);
	if (!(name[0] == '0' && name[1] == 'x')) {
		*mode = KS_MODE_RB;
		return 1;
	}
	return 0;
}
