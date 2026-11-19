// SPDX-License-Identifier: GPL-2.0-only
/*
 * discover.c
 *
 * Copyright (C) 2026 dere3046
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/kallsyms.h>
#include <linux/vmalloc.h>
#include "core.h"
#include "ks_dbg.h"
#include "access.h"
#include "slide.h"
#include "symbol.h"
#include "discover.h"
#include "hint.h"
#ifdef KALLRECON_RAANA
#include "raana/raana.h"
#endif

/* the legacy scanner is compiled for legacy builds and as the optional
 * fallback behind raana */
#if !defined(KALLRECON_RAANA) || defined(KALLRECON_RAANA_FALLBACK)
#define KS_LEGACY_SEARCH 1
#endif

#ifdef KALLRECON_RAANA_NUM
static inline u64 ks_zero_u32(u64 w)
{
	return (w - 0x0000000100000001ULL) &
		~(w | 0x7FFFFFFF7FFFFFFFULL) & 0x8000000080000000ULL;
}
#endif

#define KS_RUN_MAX	500000		/* run scan cap */
#define KS_RB_SEARCH	4096		/* rb delta search range */
#define KS_NUM_SEARCH	0x1000000	/* v3 num_syms search below names */
#define KS_PCPU_EXT_MAX	100000		/* percpu extend entry cap */
#define KS_PCPU_MAX	0x10000000	/* percpu absolute address cap */

#ifdef KALLRECON_FIRST_HIT
#define KS_FIRST_HIT(found)	do { if (found) return 1; } while (0)
#else
#define KS_FIRST_HIT(found)	do { } while (0)
#endif

static int check_ti_strong(unsigned short *ti)
{
	if (ti[0] != 0)
		return 0;
	for (int i = 1; i < 256; i++)
		if (ti[i] <= ti[i - 1])
			return 0;
	return ti['b'] - ti['a'] == 2 && ti['z'] - ti['a'] == 50;
}

/* locate token_table from token_index: walk back over trailing zeros
 * and the last token string, then step back ti255 (token_index[255]
 * holds the last token's offset inside token_table) */
static unsigned long find_token_table(unsigned long ti_addr,
				      unsigned short ti255)
{
	unsigned long pos = ti_addr - 1;
	unsigned char c;

	while (pos > kernel_base) {
		if (safe_read(&c, (void *)pos, 1) || c != 0)
			break;
		pos--;
	}
	while (pos > kernel_base) {
		if (safe_read(&c, (void *)pos, 1))
			break;
		if (c == 0)
			break;
		pos--;
	}
	if (pos + 1 > ti255)
		return pos + 1 - ti255;
	return 0;
}

static void locate_token_table(void)
{
	unsigned short ti255;
	unsigned long tt;

	if (!klindex_addr || !klnum_val)
		return;
	if (safe_read(&ti255, (void *)(klindex_addr + 255 * 2), 2))
		return;
	tt = find_token_table(klindex_addr, ti255);
	if (tt)
		kltable_addr = tt;
}

static int verify_offsets_rb(unsigned long cand, int len,
			      unsigned long *rb_out, unsigned long *rb_addr_out)
{
	int skip = 0;
	for (skip = 0; skip < 20; skip++) {
		u32 zv;
		if (safe_read(&zv, (void *)(cand + skip * 4), 4))
			break;
		if (zv != 0)
			break;
	}

	unsigned long real_cand = cand + skip * 4;
	int real_len = len - skip;

	unsigned long base_rb = (cand + len * 4 + 7) & ~7ULL;
	for (int delta = 0; delta < KS_RB_SEARCH; delta += 8) {
		for (int sgn = 0; sgn < 2; sgn++) {
			unsigned long rb_addr;
			unsigned long rb;

			if (delta == 0 && sgn == 1)
				continue;
			rb_addr = sgn ? base_rb + delta : base_rb - delta;
			if (safe_read(&rb, (void *)rb_addr, 8))
				continue;
			if (rb_addr >= real_cand &&
			    rb_addr + 8 <= real_cand + real_len * 4)
				continue;
			{
				unsigned long check = (rb_addr + 8 + 7) & ~7ULL;
				int ok = 0;
				unsigned int ns;
				if (!safe_read(&ns, (void *)check, 4) &&
				    (ns == (unsigned int)len ||
				     ns == (unsigned int)(len - 1)))
					ok = 1;
				if (!ok) {
					ok = 1;
					for (int i = 0; i < 5 && ok; i++) {
						unsigned char b[3];
						unsigned int s;
						if (safe_read(b, (void *)(check + i * 3), 3))
							ok = 0;
						else {
							s = (b[0] << 16) | (b[1] << 8) | b[2];
							if (s >= (unsigned int)len)
								ok = 0;
						}
					}
				}
				if (!ok)
					continue;
			}

			int vok = 1;
			int mode = KS_MODE_RB;

			if (real_len < 3) {
				vok = 0;
			} else {
				/* sample mid-table, like verify_offsets_selfrel:
				 * mid entries are ordinary .text symbols on every
				 * kernel. head is the x86 percpu absolute block:
				 * its small positive off resolves to hex under
				 * ABSPCPU while rb+off lands inside .text and
				 * wrongly latches KS_MODE_RB (later negative
				 * offsets then decode as zero-extended wraps).
				 * tail is _end / its aliases: is_ksym_addr()
				 * uses a strict addr < _end bound, so
				 * sprint_symbol(_end) prints hex and the first
				 * sample always fails. arm64 still latches
				 * KS_MODE_RB exactly as head sampling did */
				for (int i = 0; i < 3 && vok; i++) {
					int idx = real_len / 2 + i;
					u32 o;
					unsigned long at;

					if (idx >= real_len)
						idx = real_len - 1;
					at = real_cand + (unsigned long)idx * 4;
					if (safe_read(&o, (void *)at, 4)) {
						vok = 0;
						break;
					}
					ks_dbg("[kallrecon] mid[%d] @0x%lx = 0x%x\n",
						i, at, o);
					if (!ks_addr_try(rb, o, &mode))
						vok = 0;
				}
			}
			if (!vok)
				continue;

			kl_addr_mode = mode;	/* latch the proven formula */

			if (rb_out)
				*rb_out = rb;
			if (rb_addr_out)
				*rb_addr_out = rb_addr;
			return 1;
		}
	}

	return 0;
}

/* v3 (7.0+): offsets are self-relative, addr = entry + (s32)off.
 * entry[0] is the lowest-address symbol (_text on arm64, the percpu
 * block on x86) so its resolved address never exceeds kernel_base;
 * the middle of the table holds .text symbols that sprint resolves */
static int verify_offsets_selfrel(unsigned long cand, int len)
{
	u32 o0;

	if (len < 3)
		return 0;
	if (safe_read(&o0, (void *)cand, 4))
		return 0;
	if (cand + (s32)o0 > kernel_base) {
		ks_dbg("[kallrecon] selfrel reject: head 0x%lx\n",
			cand + (s32)o0);
		return 0;
	}

	for (int i = 0; i < 3; i++) {
		int idx = len / 2 + i;
		unsigned long at, a;
		u32 o;
		char name[KSYM_SYMBOL_LEN];

		if (idx >= len)
			idx = len - 1;
		at = cand + (unsigned long)idx * 4;
		if (safe_read(&o, (void *)at, 4))
			return 0;
		a = at + (s32)o;
		sprint_symbol(name, a);
		ks_dbg("[kallrecon] mid[%d] @0x%lx = 0x%x -> 0x%lx '%s'\n",
			i, at, o, a, name);
		if (name[0] == '0' && name[1] == 'x')
			return 0;
	}

	kl_addr_mode = KS_MODE_SELFREL;
	return 1;
}

static void publish_offsets(unsigned long cand, int len,
			    unsigned long *best_cand, int *best_len)
{
	*best_cand = cand;
	*best_len = len;
	kloffs_addr = cand;
	klnum_val = len;
}

/* verify a candidate offsets run and publish the globals on success */
static int commit_offsets(unsigned long cand, int len,
			  unsigned long *best_cand, int *best_len)
{
	unsigned long rb, rb_addr;

	if (len < KS_RUN_MIN)
		return 0;
	if (verify_offsets_rb(cand, len, &rb, &rb_addr)) {
		publish_offsets(cand, len, best_cand, best_len);
		klbase_addr = rb_addr;
		klbase_val = rb;
		ks_dbg("[kallrecon] hit pg=0x%lx sorted=%d\n",
			(unsigned long)(cand & ~KS_PAGE_MASK), len);
		return 1;
	}
	if (len > KS_RUN_MIN &&
	    verify_offsets_rb(cand, len - 1, &rb, &rb_addr)) {
		publish_offsets(cand, len - 1, best_cand, best_len);
		klbase_addr = rb_addr;
		klbase_val = rb;
		ks_dbg("[kallrecon] hit pg=0x%lx sorted=%d (len-1)\n",
			(unsigned long)(cand & ~KS_PAGE_MASK), len);
		return 1;
	}
	ks_dbg("[kallrecon] cand REJECT\n");
	return 0;
}

#ifdef KS_LEGACY_SEARCH
struct scan_run {
	int active;
	int len;
	int prev;
	unsigned long cand;
	unsigned long prev_addr;
};

/* close one run: same threshold and verification as before */
static void scan_run_close(struct scan_run *r, int selfrel,
			   unsigned long *best_cand, int *best_len, int *found)
{
	if (r->len >= KS_RUN_MIN && r->len > *best_len) {
		if (selfrel) {
			ks_dbg("[kallrecon] selfrel@0x%lx len=%d\n",
				r->cand, r->len);
			if (verify_offsets_selfrel(r->cand, r->len)) {
				publish_offsets(r->cand, r->len, best_cand,
						best_len);
				*found = 1;
			}
		} else {
			int len = r->len;

			ks_dbg("[kallrecon] cand@0x%lx len=%d prev=0x%x\n",
				r->cand, len, r->prev);

			if (r->prev != 0 && (r->prev & KS_2M_MASK) == 0)
				len--;

			if (commit_offsets(r->cand, len, best_cand, best_len))
				*found = 1;
		}
	}
	r->active = 0;
}

/* single scan for both segment shapes
 * v1/v2 tables are zero anchored ascending u32 runs, v3 tables are
 * negative self relative values ascending within a -4 tolerance,
 * a zero only starts the first shape and a negative only the second,
 * so both runs stay independent, longest verified run wins */
static int scan_both(unsigned long start, unsigned long end,
		     unsigned long *best_cand, int *best_len)
{
	int found = 0;
	struct scan_run r2 = {0}, r3 = {0};
	struct slide_win w;
	unsigned int n = 0;

	if (slide_init(&w, start, KS_WIN_SIZE, KS_WIN_MARGIN))
		return 0;

	for (;;) {
		unsigned long addr = slide_addr(&w);
		u32 v;

		if (addr >= end)
			break;

		/* a hole hop breaks both runs: the data does not continue */
		if (r2.active && addr != r2.prev_addr + 4)
			scan_run_close(&r2, 0, best_cand, best_len, &found);
		KS_FIRST_HIT(found);
		if (r3.active && addr != r3.prev_addr + 4)
			scan_run_close(&r3, 1, best_cand, best_len, &found);
		KS_FIRST_HIT(found);

		v = *(u32 *)slide_ptr(&w, slide_buf);

		if (r2.active) {
			if ((int)v < r2.prev) {
				scan_run_close(&r2, 0, best_cand, best_len,
					       &found);
				KS_FIRST_HIT(found);
				if (v == 0) {
					r2.active = 1;
					r2.cand = addr;
					r2.prev = 0;
					r2.len = 1;
					r2.prev_addr = addr;
				}
			} else {
				r2.prev = (int)v;
				r2.len++;
				r2.prev_addr = addr;
			}
		} else if (v == 0) {
			r2.active = 1;
			r2.cand = addr;
			r2.prev = 0;
			r2.len = 1;
			r2.prev_addr = addr;
		}

		if (r3.active) {
			if ((s32)v < r3.prev - 4) {
				scan_run_close(&r3, 1, best_cand, best_len,
					       &found);
				KS_FIRST_HIT(found);
				if ((s32)v < 0) {
					r3.active = 1;
					r3.cand = addr;
					r3.prev = (s32)v;
					r3.len = 1;
					r3.prev_addr = addr;
				}
			} else {
				r3.prev = (s32)v;
				r3.len++;
				r3.prev_addr = addr;
			}
		} else if ((s32)v < 0) {
			r3.active = 1;
			r3.cand = addr;
			r3.prev = (s32)v;
			r3.len = 1;
			r3.prev_addr = addr;
		}

		if (((++n) & 0x3FF) == 0 && kr_timeout_hit())
			break;
		if (slide_advance(&w, 4))
			break;
	}

	if (r2.active)
		scan_run_close(&r2, 0, best_cand, best_len, &found);
	KS_FIRST_HIT(found);
	if (r3.active)
		scan_run_close(&r3, 1, best_cand, best_len, &found);

	return found;
}
#endif

#if defined(CONFIG_X86_64) && defined(KS_LEGACY_SEARCH)
/* x86 5.10~6.12 ABSOLUTE_PERCPU stores normal symbols as rb-1-addr,
 * which decreases as addresses increase; the forward ascending-run
 * scanner cannot see it. walking backwards turns the same entries
 * into an ascending run. rb (2MB aligned) may lead the run and is
 * dropped by the same alignment check the forward scanner uses. */
static int rev_commit(unsigned long cand, int len, int head,
		      unsigned long *best_cand, int *best_len)
{
	unsigned long full = cand;
	int pc = 0, pv = KS_PCPU_MAX;

	/* the percpu block sits at the low end: absolute small positive
	 * addresses, non-increasing when walked backwards */
	while (full >= 4 && pc < KS_PCPU_EXT_MAX) {
		u32 v;
		int vi;

		if (safe_read(&v, (void *)(full - 4), 4))
			break;
		vi = (int)v;
		if (vi < 0 || vi > KS_PCPU_MAX || vi > pv)
			break;
		pv = vi;
		full -= 4;
		pc++;
	}

	if (head != 0 && (head & KS_2M_MASK) == 0)
		len--;

	return commit_offsets(full, len + pc, best_cand, best_len);
}

static int scan_zerou32_rev(unsigned long start, unsigned long end,
			    unsigned long *best_cand, int *best_len)
{
	unsigned long pos = end;
	unsigned long cand = 0;
	int len = 0, prev = 0, head = 0;
	unsigned long skipped = 0;

	while (pos > start) {
		unsigned long lo = pos - start > KS_WIN_SIZE ?
			pos - KS_WIN_SIZE : start;
		unsigned int n = (unsigned int)((pos - lo) / 4);
		int i;

		if (!n)
			break;
		if (kr_timeout_hit())
			return 0;
		if (safe_read(slide_buf, (void *)lo, n * 4)) {
			/* unreadable block: the run cannot continue across
			 * it, commit what was collected and skip the block */
			if (len >= KS_RUN_MIN &&
			    rev_commit(cand, len, head, best_cand, best_len))
				return 1;
			len = 0;
			skipped += pos - lo;
			if (skipped > KS_HOLE_MAX || lo <= start)
				break;
			pos = lo;
			continue;
		}

		for (i = (int)n - 1; i >= 0; i--) {
			u32 v = slide_buf[i];
			unsigned long addr = lo + (unsigned long)i * 4;

			if (len && (int)v < prev) {
				if (len >= KS_RUN_MIN &&
				    rev_commit(cand, len, head,
					       best_cand, best_len))
					return 1;
				len = 0;
			}
			if (!len)
				head = (int)v;
			prev = (int)v;
			cand = addr;
			len++;
		}
		pos = lo;
	}

	if (len >= KS_RUN_MIN && rev_commit(cand, len, head, best_cand, best_len))
		return 1;
	return 0;
}
#endif

/* walk the head of a compressed names stream: length byte(s) plus
 * token indexes must stay within sane bounds for consecutive
 * symbols */
static int names_stream_check(unsigned long names, unsigned int maxlen)
{
	unsigned long p = names;

	for (int count = 0; count < 16; count++) {
		unsigned char lb;
		unsigned int len;

		if (safe_read(&lb, (void *)p, 1))
			return 0;
		if (lb & 0x80) {
			unsigned char lb2;
			if (safe_read(&lb2, (void *)(p + 1), 1))
				return 0;
			len = (lb & 0x7F) | (lb2 << 7);
			p += 2;
		} else {
			len = lb;
			p += 1;
		}
		if (!len || len > maxlen)
			return 0;
		p += len;
	}
	return 1;
}

static int names_stream_ok(unsigned long names)
{
	return names_stream_check(names, 128);
}

/* auto derived names get the same check as hints by default
 * a loose build only bounds the length to the modern KSYM_NAME_LEN
 * CHECK forces the strict bound again */
static int names_auto_ok(unsigned long names)
{
	unsigned int maxlen = 128;

#ifdef KALLRECON_NAMES_LOOSE
#ifndef KALLRECON_CHECK
	maxlen = 512;
#endif
#endif
	return names_stream_check(names, maxlen);
}

/* v3 (7.0+): kallsyms_num_syms sits right before kallsyms_names, far
 * below the offsets table. locate it by scanning backwards for the
 * count and validating the names stream that follows. */
static unsigned long find_v3_num(unsigned long below, unsigned int n)
{
	unsigned long start = below > KS_NUM_SEARCH ?
		below - KS_NUM_SEARCH : kernel_base;
	struct slide_win w;
	unsigned int m = 0;

	if (slide_init(&w, start, KS_WIN_SIZE, KS_WIN_MARGIN))
		return 0;

	for (;;) {
		unsigned long addr = slide_addr(&w);
		u32 v;

		if (addr + 4 > below)
			break;
		v = *(u32 *)slide_ptr(&w, slide_buf);
		/* the scanned run may swallow a few bytes of whatever
		 * follows the offsets table that happen to ascend with it,
		 * so the real count sits at or just below n */
		if (v <= n && v >= n - 16 && names_stream_ok(addr + 4))
			return addr;
		if (((++m) & 0x3FF) == 0 && kr_timeout_hit())
			return 0;
		if (slide_advance(&w, 4))
			break;
	}
	return 0;
}

/* count the offsets run forward from a hinted start: zero anchored
 * ascending (v1/v2) or negative self relative (v3). tail words that
 * ascend with the run may be counted in, the caller verifies every
 * candidate length anyway */
static int hint_run_len(unsigned long cand)
{
	struct slide_win w;
	unsigned int m = 0;
	int n = 0, prev = 0, selfrel = 0, first = 1;

	if (slide_init(&w, cand, KS_WIN_SIZE, KS_WIN_MARGIN))
		return 0;

	for (;;) {
		u32 v;

		if (n >= KS_RUN_MAX)
			break;
		v = *(u32 *)slide_ptr(&w, slide_buf);
		if (first) {
			selfrel = (s32)v < 0;
			if (!selfrel && v != 0)
				return 0;
			first = 0;
		} else if (selfrel) {
			if ((s32)v < prev - 4)
				break;
		} else if ((int)v < prev) {
			break;
		}
		prev = (int)v;
		n++;
		if (((++m) & 0x3FF) == 0 && kr_timeout_hit())
			return 0;
		if (slide_advance(&w, 4))
			break;
	}
	return n;
}

/* hinted offsets still need an authoritative length: try the num_syms
 * word, the v1 rb adjacency and the run walk until one verifies with
 * the same checks as the scan path */
static int hint_commit_offsets(const struct kallrecon_hint *h,
			       unsigned long *best_cand, int *best_len)
{
	int cands[3], nc = 0;
	u32 v0, ns;

	if (safe_read(&v0, (void *)h->offsets, 4)) {
		kr_fail_set(KALLRECON_HINT_INVALID);
		return 0;
	}

	if (h->num_syms) {
		if (!safe_read(&ns, (void *)h->num_syms, 4))
			cands[nc++] = (int)ns;
	}
	if (h->relative_base) {
		unsigned long at = (h->relative_base + 8 + 7) & ~7ULL;

		if (nc < 3 && !safe_read(&ns, (void *)at, 4))
			cands[nc++] = (int)ns;
	}
	if (nc < 3)
		cands[nc++] = hint_run_len(h->offsets);

	for (int i = 0; i < nc; i++) {
		if (cands[i] < KS_RUN_MIN)
			continue;
		if ((s32)v0 < 0) {
			if (verify_offsets_selfrel(h->offsets, cands[i])) {
				publish_offsets(h->offsets, cands[i],
						best_cand, best_len);
				return 1;
			}
		} else if (commit_offsets(h->offsets, cands[i], best_cand,
					  best_len)) {
			return 1;
		}
	}

	kr_fail_set(KALLRECON_HINT_INVALID);
	return 0;
}

/* ti255 read plus token_table derivation shared by both engines */
int kr_setup_token_table(unsigned long ti)
{
	const struct kallrecon_hint *h = kr_hint();
	unsigned short ti255;

	if (safe_read(&ti255, (void *)(ti + 255 * 2), 2))
		return 0;
	if (h->token_table) {
		unsigned short off0;
		unsigned char c;

		if (safe_read(&off0, (void *)(ti + '0' * 2), 2) ||
		    safe_read(&c, (void *)(h->token_table + off0), 1) ||
		    c != '0') {
			kr_fail_set(KALLRECON_HINT_INVALID);
			return 0;
		}
		kltable_addr = h->token_table;
		return 1;
	}
	kltable_addr = find_token_table(ti, ti255);
	if (!kltable_addr)
		kltable_addr = ti - KS_FALLBACK_OFF;
	return 1;
}

void kr_layout_from_offsets(unsigned long ti)
{
	klindex_addr = ti;
	is_v1_layout = kloffs_addr < ti ? 1 : 0;
	if (kl_addr_mode == KS_MODE_SELFREL)
		kl_layout = LAYOUT_V3;
	else if (is_v1_layout)
		kl_layout = LAYOUT_V1;
	else
		kl_layout = LAYOUT_V2;
}

#ifdef KS_LEGACY_SEARCH
static int discover_kallsyms(unsigned long ti_addr)
{
	const struct kallrecon_hint *h = kr_hint();
	unsigned long best_cand = 0;
	int best_len = 0;
	unsigned long scan_start, scan_end;
	unsigned long scan_back = h->scan_back ? h->scan_back : KS_SCAN_BACK;
	unsigned long scan_fwd = h->scan_fwd ? h->scan_fwd : KS_SCAN_FWD;

	if (!kr_setup_token_table(ti_addr))
		return 0;

	if (h->offsets) {
		if (!hint_commit_offsets(h, &best_cand, &best_len))
			return 0;
		goto found;
	}

	scan_start = kltable_addr > scan_back ?
		(kltable_addr - scan_back) & ~KS_PAGE_MASK : kernel_base;
	scan_end = (ti_addr + scan_fwd + KS_PAGE_MASK) & ~KS_PAGE_MASK;

	ks_dbg("[kallrecon] scan 0x%lx-0x%lx kltable=0x%lx\n",
		scan_start, scan_end, kltable_addr);

	if (scan_both(scan_start, scan_end, &best_cand, &best_len))
		goto found;

#ifdef CONFIG_X86_64
	if (scan_zerou32_rev(scan_start, scan_end, &best_cand, &best_len))
		goto found;
#endif

	kr_fail_set(KALLRECON_NO_OFFSETS);
	ks_dbg("[kallrecon] no offsets found\n");
	return 0;

found:
	kr_layout_from_offsets(ti_addr);
	ks_dbg("[kallrecon] discovered: sorted=%u v%d\n",
		klnum_val, kl_layout);
	return 1;
}
#endif

#ifdef KS_LEGACY_SEARCH
static unsigned long find_token_index(unsigned long start)
{
	struct slide_win w;
	unsigned int n = 0;

	if (slide_init(&w, start, KS_WIN_SIZE, KS_WIN_MARGIN))
		return 0;

	for (;;) {
		unsigned short *ti = (unsigned short *)slide_ptr(&w, slide_buf);
		if (check_ti_strong(ti))
			return slide_addr(&w);
		if (((++n) & 0x3FF) == 0 && kr_timeout_hit())
			return 0;
		if (slide_advance(&w, 4))
			break;
	}
	return 0;
}
#endif

/* seqs entry width differs per build: upstream packs 3 byte big
 * endian, some vendor trees store a native u32. both are sampled the
 * same way, the winning width is latched into klseqs_stride */
static unsigned long detect_seqs_stride(unsigned long cand, unsigned int n,
					int stride)
{
	int points[] = {0, 1, 2, n/4, n/4+1, n/4+2, n/2, n/2+1, n/2+2,
			3*n/4, 3*n/4+1, 3*n/4+2, n-3, n-2, n-1};
	int np = sizeof(points) / sizeof(points[0]);

	if (!cand || !n)
		return 0;

	for (int i = 0; i < np; i++) {
		int idx = points[i];
		unsigned int seq;

		if (idx < 0 || idx >= (int)n)
			return 0;
		if (stride == 4) {
			u32 v;

			if (safe_read(&v, (void *)(cand +
						   (unsigned long)idx * 4), 4))
				return 0;
			seq = v;
		} else {
			unsigned char buf[3];

			if (safe_read(buf, (void *)(cand +
						    (unsigned long)idx * 3), 3))
				return 0;
			seq = (buf[0] << 16) | (buf[1] << 8) | buf[2];
		}
		if (seq >= n)
			return 0;
	}
	return cand;
}

#ifdef KALLRECON_CHECK
/* full table walk under CHECK every index must be in range and
 * no index may repeat that makes the table a permutation */
static int seqs_full_ok(unsigned long cand, unsigned int n, int stride)
{
	unsigned long bytes = ((unsigned long)n + 7) / 8;
	unsigned char *seen = vzalloc(bytes);
	unsigned int i;
	int ok = 1;

	if (!seen)
		return 1;
	for (i = 0; i < n; i++) {
		unsigned int seq;

		if (stride == 4) {
			u32 v;

			if (safe_read(&v, (void *)(cand +
						   (unsigned long)i * 4), 4)) {
				ok = 0;
				break;
			}
			seq = v;
		} else {
			unsigned char buf[3];

			if (safe_read(buf, (void *)(cand +
						    (unsigned long)i * 3), 3)) {
				ok = 0;
				break;
			}
			seq = (buf[0] << 16) | (buf[1] << 8) | buf[2];
		}
		if (seq >= n || (seen[seq >> 3] & (1 << (seq & 7)))) {
			ok = 0;
			break;
		}
		seen[seq >> 3] |= 1 << (seq & 7);
	}
	vfree(seen);
	ks_dbg("[kallrecon] seqs full verify: %s\n", ok ? "OK" : "BAD");
	return ok;
}
#endif

static unsigned long detect_seqs_any(unsigned long cand, unsigned int n)
{
	unsigned long r = detect_seqs_stride(cand, n, 3);

	if (r) {
		klseqs_stride = 3;
#ifdef KALLRECON_CHECK
		if (!seqs_full_ok(r, n, 3))
			return 0;
#endif
		return r;
	}
	r = detect_seqs_stride(cand, n, 4);
	if (r) {
		klseqs_stride = 4;
#ifdef KALLRECON_CHECK
		if (!seqs_full_ok(r, n, 4))
			return 0;
#endif
	}
	return r;
}

static int resolve_layout_v3(void)
{
	const struct kallrecon_hint *h = kr_hint();
	unsigned long below, num;

	if (h->names && !names_stream_ok(h->names))
		goto invalid;

	if (h->num_syms) {
		num = h->num_syms;
	} else {
		/* v3: no rb; num_syms and names sit below the markers */
		below = kltable_addr ? kltable_addr : kloffs_addr;
		num = find_v3_num(below, klnum_val);
	}

	if (num) {
		u32 ns;
		if (!safe_read(&ns, (void *)num, 4)) {
			klnum_addr = num;
			klnum_val = ns;
			if (h->names) {
				klnames_addr = h->names;
			} else {
				klnames_addr = num + 4;
				if (!names_auto_ok(klnames_addr)) {
					kr_fail_set(KALLRECON_NO_LAYOUT);
					return 0;
				}
			}
		}
	}

	if (h->markers) {
		u32 m0;

		if (safe_read(&m0, (void *)h->markers, 4) || m0 != 0)
			goto invalid;
		klmarks_addr = h->markers;
	} else if (kltable_addr && klnum_val) {
		unsigned int markers_cnt = (klnum_val + 255) / 256;

		/* v3 labels are .balign 4 (not 8): the array start is size
		 * bytes below token_table with no padding between them */
		klmarks_addr = (kltable_addr - markers_cnt * 4) & ~3ULL;
	}

	if (h->seqs) {
		klseqs_addr = detect_seqs_any(h->seqs, klnum_val);
		if (!klseqs_addr)
			goto invalid;
	} else if (kloffs_addr && klnum_val) {
		klseqs_addr = detect_seqs_any(
			(kloffs_addr +
			 (unsigned long)klnum_val * 4 + 3) & ~3ULL,
			klnum_val);
	}
	return 1;

invalid:
	kr_fail_set(KALLRECON_HINT_INVALID);
	return 0;
}

static int resolve_layout_v1(void)
{
	const struct kallrecon_hint *h = kr_hint();

	if (h->num_syms) {
		u32 ns;

		klnum_addr = h->num_syms;
		if (safe_read(&ns, (void *)klnum_addr, 4) ||
		    (ns != klnum_val && ns != klnum_val - 1))
			goto invalid;
	} else {
		klnum_addr = (klbase_addr + 8 + 7) & ~7ULL;
		{
			u32 ns;
			if (safe_read(&ns, (void *)klnum_addr, 4) ||
			    (ns != klnum_val && ns != klnum_val - 1))
				klnum_addr = 0;
		}
	}

	if (h->names) {
		if (!names_stream_ok(h->names))
			goto invalid;
		klnames_addr = h->names;
	} else {
		klnames_addr = (klnum_addr + 4 + 7) & ~7ULL;
		if (!names_auto_ok(klnames_addr)) {
			kr_fail_set(KALLRECON_NO_LAYOUT);
			return 0;
		}
	}

	if (h->token_table)
		kltable_addr = h->token_table;
	else
		locate_token_table();

	unsigned int markers_cnt = (klnum_val + 255) / 256;
	unsigned long marks_size = markers_cnt * 4;

	if (h->seqs) {
		klseqs_addr = detect_seqs_any(h->seqs, klnum_val);
		if (!klseqs_addr)
			goto invalid;
	} else {
		unsigned long seqs_cand = kltable_addr ?
			(kltable_addr - klnum_val * 3) & ~7ULL : 0;

		klseqs_addr = detect_seqs_any(seqs_cand, klnum_val);
		if (!klseqs_addr && kltable_addr) {
			seqs_cand = (kltable_addr -
				     (unsigned long)klnum_val * 4) & ~7ULL;
			klseqs_addr = detect_seqs_any(seqs_cand, klnum_val);
		}
	}

	if (h->markers) {
		u32 m0;

		if (safe_read(&m0, (void *)h->markers, 4) || m0 != 0)
			goto invalid;
		klmarks_addr = h->markers;
	} else if (klseqs_addr) {
		klmarks_addr = (klseqs_addr - marks_size) & ~7ULL;
	} else {
		klmarks_addr = (kltable_addr - marks_size) & ~7ULL;
	}
	return 1;

invalid:
	kr_fail_set(KALLRECON_HINT_INVALID);
	return 0;
}

static int resolve_layout_v2(void)
{
	const struct kallrecon_hint *h = kr_hint();
	unsigned int n = 0;

	if (h->seqs) {
		klseqs_addr = detect_seqs_any(h->seqs, klnum_val);
		if (!klseqs_addr)
			goto invalid;
	} else {
		klseqs_addr = detect_seqs_any(klbase_addr + 8, klnum_val);
	}

	if (h->token_table)
		kltable_addr = h->token_table;
	else
		locate_token_table();

	if (h->markers) {
		u32 m0;

		if (safe_read(&m0, (void *)h->markers, 4) || m0 != 0)
			goto invalid;
		klmarks_addr = h->markers;
	} else if (kltable_addr && klnum_val) {
		unsigned int markers_cnt = (klnum_val + 255) / 256;
		unsigned long marks_size = markers_cnt * 4;

		/* the token_table label carries its own .balign 8, so up to
		 * 7 pad bytes sit between the markers array and the label:
		 * derive the array start from its size and floor it to the
		 * label alignment (the markers label is .balign 8 as well,
		 * so the floor lands exactly for either pad, 0 or 4) */
		klmarks_addr = (kltable_addr - marks_size) & ~7ULL;
	}

	if (h->num_syms) {
		u32 ns;

		klnum_addr = h->num_syms;
		if (safe_read(&ns, (void *)klnum_addr, 4) ||
		    (ns != klnum_val && ns != klnum_val - 1))
			goto invalid;
	} else if (klmarks_addr && klnum_val) {
		unsigned long end_addr = klmarks_addr > 0x300000 ?
			klmarks_addr - 0x300000 : kernel_base;
		unsigned long addr = klmarks_addr & ~3ULL;
		unsigned char blk[0x400];

#ifdef KALLRECON_RAANA_NUM
		const u64 num0 = 0x0000000100000001ULL *
			(unsigned int)klnum_val;
		const u64 num1 = 0x0000000100000001ULL *
			(unsigned int)(klnum_val - 1);
#endif

		end_addr &= ~3ULL;
		while (addr >= end_addr) {
			unsigned long lo = addr > end_addr + sizeof(blk) - 4 ?
				addr - (sizeof(blk) - 4) : end_addr;
			unsigned int nb = (unsigned int)(addr + 4 - lo);
			unsigned int off;
			int found = 0;

			if (((++n) & 0x3FF) == 0 && kr_timeout_hit())
				return 0;
			if (!safe_read(blk, (void *)lo, nb)) {
#ifdef KALLRECON_RAANA_NUM
				unsigned long cand = 0;

				/* ascending swar scan the highest match in
				 * the block is the walk's first match */
				for (off = 0; off + 8 <= nb; off += 8) {
					u64 w, z;

					memcpy(&w, blk + off, 8);
					z = ks_zero_u32(w ^ num0) |
					    ks_zero_u32(w ^ num1);
					while (z) {
						unsigned long a = lo + off +
							(__builtin_ctzll(z) >> 3) - 3;
						unsigned int v32;

						z &= z - 1;
						memcpy(&v32, (void *)a, 4);
						if (v32 == klnum_val ||
						    v32 == klnum_val - 1)
							cand = a;
					}
				}
				for (; off + 4 <= nb; off += 4) {
					unsigned int v32;

					memcpy(&v32, blk + off, 4);
					if (v32 == klnum_val ||
					    v32 == klnum_val - 1)
						cand = lo + off;
				}
				if (cand) {
					klnum_addr = cand;
					found = 1;
				}
#else
				for (off = nb - 4; ; off -= 4) {
					unsigned int v32;

					memcpy(&v32, blk + off, 4);
					if (v32 == klnum_val ||
					    v32 == klnum_val - 1) {
						klnum_addr = lo + off;
						found = 1;
						break;
					}
					if (off == 0)
						break;
				}
#endif
			} else {
				unsigned long a2 = addr;

				while (a2 >= lo) {
					unsigned int v32;

					if (((++n) & 0x3FF) == 0 &&
					    kr_timeout_hit())
						return 0;
					if (!safe_read(&v32, (void *)a2, 4) &&
					    (v32 == klnum_val ||
					     v32 == klnum_val - 1)) {
						klnum_addr = a2;
						found = 1;
						break;
					}
					if (a2 < lo + 4)
						break;
					a2 -= 4;
				}
			}
			if (found)
				break;
			if (lo == end_addr)
				break;
			addr = lo - 4;
		}
	}

	if (h->names) {
		if (!names_stream_ok(h->names))
			goto invalid;
		klnames_addr = h->names;
	} else if (klnum_addr) {
		klnames_addr = (klnum_addr + 4 + 7) & ~7ULL;
		if (!names_auto_ok(klnames_addr)) {
			kr_fail_set(KALLRECON_NO_LAYOUT);
			return 0;
		}
	}
	return 1;

invalid:
	kr_fail_set(KALLRECON_HINT_INVALID);
	return 0;
}

static void dump_layout(void)
{
ks_dbg("[kallrecon] kallsyms data:\n");
ks_dbg("  klbase  @ 0x%lx = 0x%lx\n", klbase_addr, klbase_val);
ks_dbg("  kloffs  @ 0x%lx\n", kloffs_addr);
ks_dbg("  klnum   @ 0x%lx = %u\n", klnum_addr, klnum_val);
ks_dbg("  klindex @ 0x%lx\n", klindex_addr);
ks_dbg("  klseqs  @ 0x%lx\n", klseqs_addr);
ks_dbg("  kltable @ 0x%lx\n", kltable_addr);
ks_dbg("  klmarks @ 0x%lx\n", klmarks_addr);
ks_dbg("  klnames @ 0x%lx\n", klnames_addr);
	ks_dbg("  layout  v%d\n", kl_layout);

#ifdef KALLRECON_CHECK
	if (kltable_addr && klindex_addr) {
		unsigned short off0;
		unsigned char c;
		if (!safe_read(&off0, (void *)(klindex_addr + '0' * 2), 2) &&
		    !safe_read(&c, (void *)(kltable_addr + off0), 1) &&
		    c == '0')
			ks_dbg("  tbl verify: '0' match\n");
		else
			ks_dbg("  tbl verify: MISMATCH\n");
	}
	if (klmarks_addr && klnum_val) {
		unsigned int m0, m1;
		int mok = !safe_read(&m0, (void *)klmarks_addr, 4) && m0 == 0;
		if (mok && (klnum_val + 255) / 256 > 1)
			mok = !safe_read(&m1, (void *)(klmarks_addr + 4), 4)
				&& m1 >= 256;
		ks_dbg("  markers verify: %s\n", mok ? "OK" : "MISMATCH");
	}
	if (klnames_addr && klmarks_addr && klnum_val) {
		{
			unsigned int count = 0;
			/* the names stream ends at or before kallsyms_markers
			 * (only .balign 8 label padding between them, read as
			 * lb==0 below); the old end_off+1024 slack truncated
			 * the final segment (15 names lost on a 165482 table) */
			for (unsigned long p = klnames_addr;
			     p < klmarks_addr; ) {
				unsigned char lb;
				unsigned int elen;
				if (safe_read(&lb, (void *)p, 1))
					break;
				if (lb & 0x80) {
					unsigned char lb2;
					if (safe_read(&lb2, (void *)(p + 1), 1))
						break;
					elen = (lb & 0x7F) | (lb2 << 7);
					p += 2;
				} else {
					if (lb == 0)
						break;
					elen = lb;
					p += 1;
				}
				p += elen;
				count++;
			}
			ks_dbg("  names count: dp=%u vs sort=%u %s\n",
				count, klnum_val,
				count == klnum_val ? "MATCH" : "MISMATCH");
		}
	}
#endif
}

/* one attempt does not share state with the next: a failed discovery
 * may be retried with a changed hint, the consumer decides whether to
 * widen the window or not */
void kr_discover_reset(void)
{
	sprint_addr = 0;
	kernel_base = 0;
	klbase_addr = 0;
	klbase_val = 0;
	kloffs_addr = 0;
	klindex_addr = 0;
	klseqs_addr = 0;
	klseqs_stride = 3;
	klmarks_addr = 0;
	kltable_addr = 0;
	klnames_addr = 0;
	klnum_addr = 0;
	klnum_val = 0;
	kl_layout = LAYOUT_V2;
	is_v1_layout = 0;
	kl_addr_mode = KS_MODE_RB;
	kr_markers_reset();
	kr_cleanup_reset();
	kr_fail_clear();
}

int kr_discover_layout(void)
{
	const struct kallrecon_hint *h = kr_hint();
#ifdef KS_LEGACY_SEARCH
	unsigned long ti_addr;
#endif

	kr_timeout_arm(h->timeout_ms);

	sprint_addr = kr_get_sprint_addr();
	if (!sprint_addr) {
		kr_fail_set(KALLRECON_NO_ANCHOR);
		return 0;
	}
	kernel_base = sprint_addr & ~KS_2M_MASK;
	klbase_val = kernel_base;

	ks_dbg("[kallrecon] sprint=0x%lx kernel_base=0x%lx\n",
		sprint_addr, kernel_base);

#ifdef KALLRECON_RAANA
	if (kr_raana_discover())
		goto resolved;
#ifndef KALLRECON_RAANA_FALLBACK
	return 0;
#endif
#endif

#ifdef KS_LEGACY_SEARCH
	if (h->token_index) {
		unsigned short ti[256];

		if (safe_read(ti, (void *)h->token_index, sizeof(ti)) ||
		    !check_ti_strong(ti)) {
			kr_fail_set(KALLRECON_HINT_INVALID);
			return 0;
		}
		ti_addr = h->token_index;
	} else {
		ti_addr = find_token_index(sprint_addr & ~KS_PAGE_MASK);
		if (!ti_addr) {
			kr_fail_set(KALLRECON_NO_TOKEN_INDEX);
			return 0;
		}
	}
	ks_dbg("[kallrecon] ti=0x%lx\n", ti_addr);
	klindex_addr = ti_addr;

	if (!discover_kallsyms(ti_addr)) {
		ks_dbg("[kallrecon] layout: offsets not found\n");
		return 0;
	}
#endif

#ifdef KALLRECON_RAANA
resolved:
#endif
	switch (kl_layout) {
	case LAYOUT_V3:
		if (!resolve_layout_v3())
			return 0;
		break;
	case LAYOUT_V1:
		if (!resolve_layout_v1())
			return 0;
		break;
	default:
		if (!resolve_layout_v2())
			return 0;
		break;
	}

	/*
	 * sorted-run cand may sit on a leading zero u32 before
	 * kallsyms_offsets, shifting sym_addr() by one entry.
	 * recompute offsets start from kallsyms_num_syms.
	 */
	if (klbase_addr && klnum_addr) {
		u32 ns;
		if (!safe_read(&ns, (void *)klnum_addr, 4) && ns) {
			kloffs_addr =
				(klbase_addr - (unsigned long)ns * 4) & ~7ULL;
			klnum_val = ns;
		}
	}

	kr_verify_markers();
	dump_layout();
	return 1;
}

#ifdef KALLRECON_RAANA
int kr_check_ti_strong(unsigned short *ti)
{
	return check_ti_strong(ti);
}

int kr_commit_offsets(unsigned long cand, int len,
		      unsigned long *best_cand, int *best_len)
{
	return commit_offsets(cand, len, best_cand, best_len);
}

int kr_commit_selfrel(unsigned long cand, int len,
		      unsigned long *best_cand, int *best_len)
{
	if (!verify_offsets_selfrel(cand, len))
		return 0;
	publish_offsets(cand, len, best_cand, best_len);
	return 1;
}

int kr_hint_commit_offsets(const struct kallrecon_hint *h,
			   unsigned long *best_cand, int *best_len)
{
	return hint_commit_offsets(h, best_cand, best_len);
}
#endif
