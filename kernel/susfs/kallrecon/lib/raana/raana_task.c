// SPDX-License-Identifier: GPL-2.0-only
/*
 * raana_task.c
 *
 * Copyright (C) 2026 dere3046
 */

#include <asm/page.h>

#include "raana.h"
#include "../core.h"
#include "../ks_dbg.h"
#include "../hint.h"

static inline u64 raana_zero16(u64 w)
{
	return (w - 0x0001000100010001ULL) &
		~(w | 0x7FFF7FFF7FFF7FFFULL) & 0x8000800080008000ULL;
}

static inline u64 raana_zero32(u64 w)
{
	return (w - 0x0000000100000001ULL) &
		~(w | 0x7FFFFFFF7FFFFFFFULL) & 0x8000000080000000ULL;
}

struct raana_run {
	int active;
	int len;
	int prev;
	unsigned long cand;
	unsigned long prev_addr;
};

static void raana_ti_record(struct raana_ctx *ctx, unsigned long addr)
{
	spin_lock(&ctx->lock);
	if (ctx->nti < RAANA_MAX_TI)
		ctx->ti_hits[ctx->nti++] = addr;
	if (!ctx->ceil || addr < ctx->ceil)
		ctx->ceil = addr;
	spin_unlock(&ctx->lock);
}

static void raana_stop_set(struct raana_ctx *ctx)
{
	spin_lock(&ctx->lock);
	ctx->stop = 1;
	spin_unlock(&ctx->lock);
}

/* probe one word per page a page is fully readable once its first
 * word reads back returns the first unreadable page start */
static unsigned long raana_probe(unsigned long addr, unsigned long end)
{
	unsigned char tmp[8];
	unsigned long pos = addr;
	unsigned int n = 0;

	while (pos < end) {
		if (safe_read(tmp, (void *)pos, sizeof(tmp)))
			break;
		if (((++n) & 0xFF) == 0 && kr_timeout_hit())
			break;
		pos += PAGE_SIZE;
	}
	return pos;
}

static int raana_ti_cand(struct raana_ctx *ctx, unsigned long base,
			 u64 z, unsigned long scan_end,
			 unsigned long readable)
{
	while (z) {
		unsigned long o = base + (__builtin_ctzll(z) >> 3) - 1;

		z &= z - 1;
		if (o + 4 > scan_end)
			continue;
		if (*(unsigned short *)(o + 2) == 0)
			continue;
		if (o + 512 > readable)
			continue;
		if (kr_check_ti_strong((unsigned short *)o)) {
			raana_ti_record(ctx, o);
			return 1;
		}
	}
	return 0;
}

static int raana_token_window(struct raana_ctx *ctx, unsigned long addr)
{
	unsigned long chunk_end = addr + KS_WIN_SIZE;
	unsigned long readable = raana_probe(addr, chunk_end + PAGE_SIZE);
	unsigned long scan_end = readable < chunk_end ? readable : chunk_end;
	unsigned long pos = addr;

	while (pos + 16 <= scan_end) {
		u64 z0 = raana_zero16(
			*(const u64 *)pos);
		u64 z1 = raana_zero16(
			*(const u64 *)(pos + 8));

		if (raana_ti_cand(ctx, pos, z0, scan_end, readable) ||
		    raana_ti_cand(ctx, pos + 8, z1, scan_end, readable))
			return 1;
		pos += 16;
	}
	while (pos + 8 <= scan_end) {
		u64 z = raana_zero16(
			*(const u64 *)pos);

		if (raana_ti_cand(ctx, pos, z, scan_end, readable))
			return 1;
		pos += 8;
	}
	while (pos + 4 <= scan_end) {
		if (*(unsigned short *)pos == 0 &&
		    *(unsigned short *)(pos + 2) != 0 &&
		    pos + 512 <= readable &&
		    kr_check_ti_strong((unsigned short *)pos)) {
			raana_ti_record(ctx, pos);
			return 1;
		}
		pos += 2;
	}
	return 0;
}

static unsigned long raana_chunk_addr(struct raana_ctx *ctx, int idx)
{
	int pri = ctx->pri_cnt;

	if (idx < pri)
		return ctx->start + (unsigned long)(ctx->pri_idx + idx) *
			KS_WIN_SIZE;
	idx -= pri;
	if (idx < ctx->pri_idx)
		return ctx->start + (unsigned long)idx * KS_WIN_SIZE;
	idx -= ctx->pri_idx;
	return ctx->start + (unsigned long)(ctx->pri_idx + pri + idx) *
		KS_WIN_SIZE;
}

int raana_token_worker(struct raana_worker *wk)
{
	struct raana_ctx *ctx = wk->ctx;

	for (;;) {
		unsigned long addr, ceil;
		int idx, stop;

		spin_lock(&ctx->lock);
		idx = ctx->next++;
		stop = ctx->stop;
		ceil = ctx->ceil;
		spin_unlock(&ctx->lock);
		if (stop)
			break;
		if (idx >= ctx->nchunks)
			break;
		addr = raana_chunk_addr(ctx, idx);
		if (ceil && addr > ceil)
			continue;
		if (raana_token_window(ctx, addr))
			continue;
		if (kr_timeout_hit()) {
			raana_stop_set(ctx);
			break;
		}
	}
	return 0;
}

/* find the next zero dword at or after *pos advances *pos when idle
 * returns 1 on a hit 0 at the end and -1 on timeout */
static int raana_r2_idle(unsigned long *pos, unsigned long scan_end,
			 unsigned long *out)
{
	unsigned long p = *pos;
	unsigned int n = 0;

	while (p + 16 <= scan_end) {
		u64 z0 = raana_zero32(
			*(const u64 *)p);
		u64 z1 = raana_zero32(
			*(const u64 *)(p + 8));

		while (z0) {
			unsigned long o = p + (__builtin_ctzll(z0) >> 3) - 3;

			z0 &= z0 - 1;
			if (*(unsigned int *)o == 0) {
				*out = o;
				return 1;
			}
		}
		while (z1) {
			unsigned long o = p + 8 +
				(__builtin_ctzll(z1) >> 3) - 3;

			z1 &= z1 - 1;
			if (*(unsigned int *)o == 0) {
				*out = o;
				return 1;
			}
		}
		p += 16;
		if (((++n) & 0x3FF) == 0 && kr_timeout_hit())
			return -1;
	}
	while (p + 8 <= scan_end) {
		u64 z = raana_zero32(
			*(const u64 *)p);

		while (z) {
			unsigned long o = p + (__builtin_ctzll(z) >> 3) - 3;

			z &= z - 1;
			if (*(unsigned int *)o == 0) {
				*out = o;
				return 1;
			}
		}
		p += 8;
	}
	if (p + 4 <= scan_end && *(unsigned int *)p == 0) {
		*out = p;
		return 1;
	}
	*pos = p;
	return 0;
}

static int raana_r2_close(struct raana_run *r, unsigned long *best_cand,
			  int *best_len, unsigned long *hc, int *hl)
{
	int len = r->len;
	int ok = 0;

	if (len >= KS_RUN_MIN && len > *best_len) {
		int use = len;

		if (r->prev != 0 && (r->prev & KS_2M_MASK) == 0)
			use--;
		if (kr_commit_offsets(r->cand, use, best_cand, best_len)) {
			*hc = r->cand;
			*hl = *best_len;
			ok = 1;
		}
	}
	r->active = 0;
	return ok;
}

static int raana_r2_scan(struct raana_ctx *ctx, unsigned long start,
			 unsigned long end, int *stop, unsigned long confirm,
			 int clen);
static int raana_r3_scan(struct raana_ctx *ctx, unsigned long start,
			 unsigned long end, int *stop, unsigned long confirm,
			 int clen);

/* second independent derivation of a verified candidate over its own
 * span the same run machine must close the same run again */
static int raana_confirm(struct raana_ctx *ctx, unsigned long cand, int len,
			 int selfrel)
{
	unsigned long start = (cand - RAANA_CROSS_PAD) & ~KS_PAGE_MASK;
	unsigned long end = cand + (unsigned long)len * 4 + RAANA_CROSS_PAD;
	int stop = 0;

	if (selfrel)
		return raana_r3_scan(ctx, start, end, &stop, cand, len);
	return raana_r2_scan(ctx, start, end, &stop, cand, len);
}

static int raana_hit_stop(struct raana_ctx *ctx, unsigned long hc, int hl,
			  int selfrel)
{
	ctx->hit_cand = hc;
	ctx->hit_len = hl;
	if (ctx->first_hit || !ctx->range_active) {
		ctx->hit_ok = 1;
		return 1;
	}
	if (raana_confirm(ctx, hc, hl, selfrel)) {
		ctx->hit_ok = 1;
		return 1;
	}
	ks_dbg("[raana] confirm miss 0x%lx len=%d\n", hc, hl);
	return 0;
}

static int raana_r2_scan(struct raana_ctx *ctx, unsigned long start,
			 unsigned long end, int *stop, unsigned long confirm,
			 int clen)
{
	struct raana_run r = {0};
	unsigned long best_cand = 0;
	int best_len = 0;
	unsigned long readable, scan_end, pos, o;
	unsigned int m = 0;

	start &= ~KS_PAGE_MASK;
	if (!confirm)
		end = (end + KS_PAGE_MASK) & ~KS_PAGE_MASK;
	readable = raana_probe(start, end + PAGE_SIZE);
	if (kr_timeout_hit()) {
		*stop = 1;
		return 0;
	}
	scan_end = readable < end ? readable : end;
	pos = start;

	while (pos + 4 <= scan_end) {
		unsigned int v;

		if (((++m) & 0x3FF) == 0 && kr_timeout_hit()) {
			*stop = 1;
			return 0;
		}
		if (!r.active) {
			int rc = raana_r2_idle(&pos, scan_end, &o);

			if (rc < 0) {
				*stop = 1;
				return 0;
			}
			if (!rc)
				break;
			r.active = 1;
			r.cand = o;
			r.prev = 0;
			r.len = 1;
			r.prev_addr = o;
			pos = o + 4;
			continue;
		}
		v = *(unsigned int *)pos;
		if ((int)v < r.prev) {
			unsigned long hc;
			int hl;

			if (raana_r2_close(&r, &best_cand, &best_len, &hc, &hl)) {
				if (confirm) {
					if (hc == confirm && hl - clen <= 1 &&
					    clen - hl <= 1)
						return 1;
				} else if (raana_hit_stop(ctx, hc, hl, 0)) {
					*stop = 1;
					return 1;
				}
			}
			if (v == 0) {
				r.active = 1;
				r.cand = pos;
				r.prev = 0;
				r.len = 1;
				r.prev_addr = pos;
			}
		} else {
			r.prev = (int)v;
			r.len++;
			r.prev_addr = pos;
		}
		pos += 4;
	}

	if (r.active) {
		unsigned long hc;
		int hl;

		if (raana_r2_close(&r, &best_cand, &best_len, &hc, &hl)) {
			if (confirm) {
				if (hc == confirm && hl - clen <= 1 &&
				    clen - hl <= 1)
					return 1;
			} else if (raana_hit_stop(ctx, hc, hl, 0)) {
				*stop = 1;
			}
		}
	}
	return confirm ? 0 : ctx->hit_ok;
}

static int raana_r2_seg(struct raana_ctx *ctx, unsigned long start,
			unsigned long end, int *stop)
{
	return raana_r2_scan(ctx, start, end, stop, 0, 0);
}

static int raana_r3_close(struct raana_run *r, unsigned long *best_cand,
			  int *best_len, unsigned long *hc, int *hl)
{
	int ok = 0;

	if (r->len >= KS_RUN_MIN && r->len > *best_len &&
	    kr_commit_selfrel(r->cand, r->len, best_cand, best_len)) {
		*hc = r->cand;
		*hl = *best_len;
		ok = 1;
	}
	r->active = 0;
	return ok;
}

/* self relative layout v3 the run ascends within a four byte
 * tolerance and starts on any negative dword */
static int raana_r3_scan(struct raana_ctx *ctx, unsigned long start,
			 unsigned long end, int *stop, unsigned long confirm,
			 int clen)
{
	struct raana_run r = {0};
	unsigned long best_cand = 0;
	int best_len = 0;
	unsigned long readable, scan_end, pos;
	unsigned int m = 0;

	start &= ~KS_PAGE_MASK;
	if (!confirm)
		end = (end + KS_PAGE_MASK) & ~KS_PAGE_MASK;
	readable = raana_probe(start, end + PAGE_SIZE);
	if (kr_timeout_hit()) {
		*stop = 1;
		return 0;
	}
	scan_end = readable < end ? readable : end;
	pos = start;

	while (pos + 8 <= scan_end && !r.active) {
		u64 w = *(const u64 *)pos;

		if (((++m) & 0x3FF) == 0 && kr_timeout_hit()) {
			*stop = 1;
			return 0;
		}
		if (!(w & 0x8000000080000000ULL)) {
			pos += 8;
			continue;
		}
		break;
	}
	while (pos + 4 <= scan_end) {
		unsigned int v = *(unsigned int *)pos;

		if (((++m) & 0x3FF) == 0 && kr_timeout_hit()) {
			*stop = 1;
			return 0;
		}
		if (r.active) {
			if ((int)v < r.prev - 4) {
				unsigned long hc;
				int hl;

				if (raana_r3_close(&r, &best_cand, &best_len,
						   &hc, &hl)) {
					if (confirm) {
						if (hc == confirm &&
						    hl - clen <= 1 &&
						    clen - hl <= 1)
							return 1;
					} else if (raana_hit_stop(ctx, hc, hl,
								  1)) {
						*stop = 1;
						return 1;
					}
				}
				if ((int)v < 0) {
					r.active = 1;
					r.cand = pos;
					r.prev = (int)v;
					r.len = 1;
				}
			} else {
				r.prev = (int)v;
				r.len++;
			}
		} else if ((int)v < 0) {
			r.active = 1;
			r.cand = pos;
			r.prev = (int)v;
			r.len = 1;
		}
		pos += 4;
	}

	if (r.active) {
		unsigned long hc;
		int hl;

		if (raana_r3_close(&r, &best_cand, &best_len, &hc, &hl)) {
			if (confirm) {
				if (hc == confirm && hl - clen <= 1 &&
				    clen - hl <= 1)
					return 1;
			} else if (raana_hit_stop(ctx, hc, hl, 1)) {
				*stop = 1;
			}
		}
	}
	return confirm ? 0 : ctx->hit_ok;
}

static int raana_r3_seg(struct raana_ctx *ctx, unsigned long start,
			unsigned long end, int *stop)
{
	return raana_r3_scan(ctx, start, end, stop, 0, 0);
}

typedef int (*raana_seg_fn)(struct raana_ctx *ctx, unsigned long start,
			    unsigned long end, int *stop);

static void raana_segments(struct raana_ctx *ctx, unsigned long sw,
			   unsigned long se, int *stop, raana_seg_fn seg)
{
	if (ctx->range_active) {
		unsigned long pl = ctx->rlo > sw ? ctx->rlo : sw;
		unsigned long ph = ctx->rhi < se ? ctx->rhi : se;

		pl &= ~KS_PAGE_MASK;
		if (ph > pl) {
			/* one boundary only the priority part runs to the
			 * sweep end so runs starting inside it complete */
			seg(ctx, pl, se, stop);
			if (*stop)
				return;
			if (pl > sw)
				seg(ctx, sw, pl, stop);
			return;
		}
	}
	seg(ctx, sw, se, stop);
}

void raana_offsets_phase(struct raana_ctx *ctx, unsigned long ti,
			 unsigned long kltable)
{
	unsigned long sw, se;
	int stop = 0;

	sw = kltable > ctx->sback ?
		(kltable - ctx->sback) & ~KS_PAGE_MASK : kernel_base;
	se = (ti + ctx->sfwd + KS_PAGE_MASK) & ~KS_PAGE_MASK;

	raana_segments(ctx, sw, se, &stop, raana_r2_seg);
	if (stop || ctx->hit_len)
		return;
	raana_segments(ctx, sw, se, &stop, raana_r3_seg);
}
