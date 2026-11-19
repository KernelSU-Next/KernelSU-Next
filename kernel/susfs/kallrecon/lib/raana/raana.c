// SPDX-License-Identifier: GPL-2.0-only
/*
 * raana.c
 *
 * Copyright (C) 2026 dere3046
 */

#include <linux/module.h>
#include <linux/string.h>

#include "raana.h"
#include "../core.h"
#include "../access.h"
#include "../ks_dbg.h"
#include "../hint.h"

int kr_raana_discover(void)
{
	struct raana_ctx ctx;
	const struct kallrecon_hint *h = kr_hint();
	unsigned long ti = 0;
	int i;

#if defined(CONFIG_X86_64) && defined(CONFIG_KALLSYMS_ABSOLUTE_PERCPU)
	/* pre 6.18 x86 stores rb - 1 - addr descending
	 * the forward run machine cannot see it */
	return 0;
#endif

	memset(&ctx, 0, sizeof(ctx));
	spin_lock_init(&ctx.lock);
	ctx.start = sprint_addr & ~KS_PAGE_MASK;
	ctx.end = ctx.start + RAANA_TOKEN_RANGE;
	ctx.nchunks = (int)((RAANA_TOKEN_RANGE + KS_WIN_SIZE - 1) /
			    KS_WIN_SIZE);
	ctx.sback = h->scan_back ? h->scan_back : KS_SCAN_BACK;
	ctx.sfwd = h->scan_fwd ? h->scan_fwd : KS_SCAN_FWD;
#ifdef KALLRECON_FIRST_HIT
	ctx.first_hit = 1;
#endif
#ifdef KALLRECON_SCAN_RANGE
	ctx.rlo = h->scan.lo;
	ctx.rhi = h->scan.hi;
	if (ctx.rlo && ctx.rhi > ctx.rlo)
		ctx.range_active = 1;
#endif
	if (ctx.range_active) {
		unsigned long lo = ctx.rlo > ctx.start ? ctx.rlo : ctx.start;
		unsigned long hi = ctx.rhi < ctx.end ? ctx.rhi : ctx.end;

		if (hi > lo) {
			ctx.pri_idx = (int)((lo - ctx.start) / KS_WIN_SIZE);
			ctx.pri_cnt = (int)((hi - 1 - ctx.start) /
					    KS_WIN_SIZE) - ctx.pri_idx + 1;
		} else {
			ctx.range_active = 0;
		}
	}

	if (h->token_index) {
		unsigned short tibuf[256];

		if (safe_read(tibuf, (void *)h->token_index, sizeof(tibuf)) ||
		    !kr_check_ti_strong(tibuf)) {
			kr_fail_set(KALLRECON_HINT_INVALID);
			return 0;
		}
		ti = h->token_index;
	} else {
		if (!raana_run(&ctx) || !ctx.nti) {
			kr_fail_set(KALLRECON_NO_TOKEN_INDEX);
			return 0;
		}
		ti = ctx.ti_hits[0];
		for (i = 1; i < ctx.nti; i++)
			if (ctx.ti_hits[i] < ti)
				ti = ctx.ti_hits[i];
	}
	ks_dbg("[raana] ti=0x%lx\n", ti);

	if (!kr_setup_token_table(ti))
		return 0;

	if (h->offsets) {
		unsigned long best_cand = 0;
		int best_len = 0;

		if (!kr_hint_commit_offsets(h, &best_cand, &best_len))
			return 0;
	} else {
		raana_offsets_phase(&ctx, ti, kltable_addr);
	}
	if (!kloffs_addr)
		return 0;

	kr_layout_from_offsets(ti);
	ks_dbg("[raana] offsets=0x%lx len=%u\n", kloffs_addr, klnum_val);
	return 1;
}
