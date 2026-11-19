// SPDX-License-Identifier: GPL-2.0-only
/*
 * symbol.c
 *
 * Copyright (C) 2026 dere3046
 */

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include "core.h"
#include "ks_dbg.h"
#include "access.h"
#include "slide.h"
#include "symbol.h"

/* kernels without the CFI backport (vanilla 5.10) do not define __nocfi */
#ifndef __nocfi
#define __nocfi
#endif

/* markers sanity: walking 256 symbols from the stream start must land
 * on the offset stored in markers[1]; only then get_sym_offset() may
 * trust markers to skip ahead */
static int kl_markers_ok;

/* KALLRECON_NO_MARKERS forces the original full names walk */
int ks_markers_usable(void)
{
#ifdef KALLRECON_NO_MARKERS
	return 0;
#else
	return kl_markers_ok == 1;
#endif
}

void kr_markers_reset(void)
{
	kl_markers_ok = 0;
}

void kr_verify_markers(void)
{
	u32 m0, m1;
	const u8 *p;
	int i;

	if (!klmarks_addr || !klnames_addr || klnum_val <= 512)
		return;
	if (safe_read(&m0, (void *)klmarks_addr, 4) ||
	    safe_read(&m1, (void *)(klmarks_addr + 4), 4) || m0 != 0) {
		kl_markers_ok = -1;
		return;
	}

	p = (const u8 *)klnames_addr;
	for (i = 0; i < 256; i++) {
		unsigned char lb;
		int len;

		if (safe_read(&lb, (void *)p, 1))
			break;
		len = lb;
		if (len & 0x80) {
			if (safe_read(&lb, (void *)(p + 1), 1))
				break;
			len = (len & 0x7F) | (lb << 7);
			p += 2 + len;
		} else {
			p += 1 + len;
		}
	}
	kl_markers_ok = (i == 256 &&
			 (unsigned int)(p - (const u8 *)klnames_addr) == m1)
		? 1 : -1;
	ks_dbg("[kallrecon] markers verify: %s\n",
		kl_markers_ok == 1 ? "OK" : "MISMATCH");
}

#define KS_CLEANUP_PROBE	8192

unsigned short ti_buf[256];
unsigned char tt_buf[KS_TT_SIZE];

/* strip one LTO suffix. SEQS (default) is the original GKI proven rule
 * and costs nothing; AUTO probes the names stream once and is the
 * opt-in setting for third party kernels; LLVM and DOLLAR force one
 * style */
static int kr_cleanup_mode = KALLRECON_CLEANUP_SEQS;
static int kr_cleanup_style;

static int kr_cleanup_probe(void)
{
	char buf[256];
	unsigned long off = 0;
	int has_llvm = 0, has_dollar = 0;

	if (!klnames_addr || !klindex_addr || !kltable_addr)
		return klseqs_addr ? 1 : 2;
	if (safe_read(ti_buf, (void *)klindex_addr, sizeof(ti_buf)) ||
	    safe_read(tt_buf, (void *)kltable_addr, sizeof(tt_buf)))
		return klseqs_addr ? 1 : 2;

	for (int i = 0; i < KS_CLEANUP_PROBE; i++) {
		unsigned char enc[258];
		unsigned int len, hdr = 1;

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
		if (!ks_expand_raw(enc, ti_buf, tt_buf, buf, sizeof(buf)))
			break;
		if (strstr(buf, ".llvm."))
			has_llvm = 1;
		else if (strchr(buf, '$'))
			has_dollar = 1;
		if (has_llvm && has_dollar)
			break;
		off += hdr + len;
	}

	if (has_llvm && !has_dollar)
		return 1;
	if (has_dollar && !has_llvm)
		return 2;
	return klseqs_addr ? 1 : 2;
}

static int kr_cleanup_get_style(void)
{
	if (kr_cleanup_mode == KALLRECON_CLEANUP_SEQS)
		return klseqs_addr ? 1 : 2;
	if (kr_cleanup_mode == KALLRECON_CLEANUP_LLVM)
		return 1;
	if (kr_cleanup_mode == KALLRECON_CLEANUP_DOLLAR)
		return 2;
	if (!kr_cleanup_style)
		kr_cleanup_style = kr_cleanup_probe();
	return kr_cleanup_style;
}

static int ks_cleanup_name(char *s)
{
	char *res;

	if (kr_cleanup_get_style() == 1)
		res = strstr(s, ".llvm.");
	else
		res = strrchr(s, '$');
	if (!res)
		return 0;
	*res = '\0';
	return 1;
}

void kallrecon_set_cleanup_mode(enum kallrecon_cleanup mode)
{
	if (mode != KALLRECON_CLEANUP_AUTO &&
	    mode != KALLRECON_CLEANUP_SEQS &&
	    mode != KALLRECON_CLEANUP_LLVM &&
	    mode != KALLRECON_CLEANUP_DOLLAR)
		return;
	kr_cleanup_mode = (int)mode;
	kr_cleanup_style = 0;
}

void kr_cleanup_reset(void)
{
	kr_cleanup_style = 0;
}

static int (*kallrecon_user_cleanup)(char *s);

static __nocfi int ks_cleanup_name_chain(char *s)
{
	int (*cb)(char *s) = READ_ONCE(kallrecon_user_cleanup);
	int r = ks_cleanup_name(s);

	if (cb)
		r |= cb(s) ? 1 : 0;
	return r;
}

void kallrecon_set_cleanup(int (*cb)(char *s))
{
	WRITE_ONCE(kallrecon_user_cleanup, cb);
}

unsigned long sym_addr(int idx)
{
	u32 off;

	if (idx < 0 || idx >= (int)klnum_val)
		return 0;
	if (safe_read(&off, (void *)(kloffs_addr + idx * 4), 4))
		return 0;
#ifdef CONFIG_X86_64
	if (kl_addr_mode == KS_MODE_ABSPCPU) {
		s32 so = (s32)off;

		if (so >= 0)
			return (unsigned long)(u32)so;
		return klbase_val - 1 - so;
	}
#endif
	if (kl_addr_mode == KS_MODE_SELFREL)
		return kloffs_addr + idx * 4 + (s32)off;
	return klbase_val + off;
}

/* decode one compressed symbol: length header plus token indexes.
 * tt points at token_table whenever the encoding sits in the slide
 * window, NULL reads tokens through safe_read instead (enc is then a
 * stack copy). ti is always a local array, loaded once per batch */
int ks_expand_raw(const unsigned char *enc, const unsigned short *ti,
		  const unsigned char *tt, char *buf, int max)
{
	unsigned int len = *enc++;
	int skipped = 0;

	*buf = '\0';
	if (len & 0x80)
		len = (len & 0x7F) | (*enc++ << 7);
	if (len > 256U)
		return 0;

	for (unsigned int i = 0; i < len && max > 1; i++) {
		unsigned char c = *enc++;
		unsigned short to = ti[c];

		if (to >= KS_TT_SIZE)
			return 0;
		if (tt) {
			const unsigned char *tp = tt + to;

			while (*tp) {
				if (tp - tt >= KS_TT_SIZE)
					return 0;
				if (skipped) {
					if (max <= 1)
						return 0;
					*buf++ = *tp;
					max--;
				} else {
					skipped = 1;
				}
				tp++;
			}
		} else {
			unsigned long tp = kltable_addr + to;

			for (;;) {
				unsigned char ch;

				if (safe_read(&ch, (void *)tp, 1) || !ch)
					break;
				if (skipped) {
					if (max <= 1)
						break;
					*buf++ = ch;
					max--;
				} else {
					skipped = 1;
				}
				tp++;
			}
		}
	}
	if (max)
		*buf = '\0';
	return 1;
}

int expand_sym(unsigned int off, char *buf, int max)
{
	unsigned short ti[256];
	unsigned char enc[2 + 256];
	unsigned int len, hdr;

	if (max <= 0)
		return 0;
	buf[0] = '\0';	/* a failed read must not leave stale data */

	if (safe_read(ti, (void *)klindex_addr, sizeof(ti)))
		return 0;
	if (safe_read(enc, (void *)(klnames_addr + off), 1))
		return 0;
	len = enc[0];
	hdr = 1;
	if (len & 0x80) {
		if (safe_read(enc + 1, (void *)(klnames_addr + off + 1), 1))
			return 0;
		len = (len & 0x7F) | (enc[1] << 7);
		hdr = 2;
	}
	if (len > 256U)
		return 0;
	if (safe_read(enc + hdr, (void *)(klnames_addr + off + hdr), len))
		return 0;
	if (!ks_expand_raw(enc, ti, NULL, buf, max)) {
		/* ks_expand_raw() may have written a partial name without
		 * the terminator, drop it so callers never see a stale or
		 * unterminated buffer
		 */
		buf[0] = '\0';
		return 0;
	}
	return (int)(hdr + len);
}

unsigned int get_sym_seq(int idx)
{
	unsigned int i, seq = 0;

	if (klseqs_addr) {
		if (klseqs_stride == 4) {
			u32 v;

			if (safe_read(&v, (const void *)(klseqs_addr +
					(unsigned long)idx * 4), 4))
				return (unsigned int)idx;
			return v;
		}
		{
			unsigned char buf[3];

			if (safe_read(buf, (const void *)(klseqs_addr +
					(unsigned long)idx * 3), 3))
				return (unsigned int)idx;
			for (i = 0; i < 3; i++)
				seq = (seq << 8) | buf[i];
			return seq;
		}
	}
	return (unsigned int)idx;
}

unsigned int get_sym_offset(unsigned int seq)
{
	const u8 *p;
	unsigned char lb;

	/* markers hold the stream offset of every 256th symbol; jump to
	 * the nearest one instead of walking the whole stream */
	if (ks_markers_usable() && seq >= 256) {
		unsigned int m = seq / 256;
		u32 mo;

		if (safe_read(&mo, (void *)(klmarks_addr + m * 4), 4))
			return UINT_MAX;
		seq -= m * 256;
		p = (const u8 *)(klnames_addr + mo);
	} else {
		p = (const u8 *)klnames_addr;
	}

	for (unsigned int i = 0; i < seq; i++) {
		if (safe_read(&lb, (void *)p, 1))
			return UINT_MAX;
		int len = lb;

		if (len & 0x80) {
			if (safe_read(&lb, (void *)(p + 1), 1))
				return UINT_MAX;
			len = ((len & 0x7F) | (lb << 7)) + 1;
		}
		p = p + len + 1;
	}
	return p - (const u8 *)klnames_addr;
}

DEFINE_MUTEX(ks_linear_lock);

static unsigned long name_to_addr_linear_locked(const char *name)
{
	unsigned short *ti = ti_buf;
	unsigned char *tt = tt_buf;
	char nbuf[256];
	int idx, hit = 0, decoded = 0, retried = 0;
	struct slide_win w;

	ks_dbg("[kallrecon] linear: search '%s' n=%u\n", name, klnum_val);

	if (safe_read(ti, (void *)klindex_addr, sizeof(ti_buf))) {
		ks_dbg("[kallrecon] linear: ti load FAIL\n");
		return 0;
	}
	if (safe_read(tt, (void *)kltable_addr, sizeof(tt_buf))) {
		ks_dbg("[kallrecon] linear: tt load FAIL\n");
		return 0;
	}

	if (slide_init(&w, klnames_addr, KS_WIN_SIZE, KS_WIN_MARGIN)) {
		ks_dbg("[kallrecon] linear: slide init FAIL\n");
		return 0;
	}

	for (idx = 0; idx < (int)klnum_val; ) {
		const unsigned char *name_start = slide_ptr(&w, slide_buf);
		int lb = *name_start;
		int elen = lb;
		int hdr = 1;

		if (lb & 0x80) {
			elen = (lb & 0x7F) | (name_start[1] << 7);
			hdr = 2;
		}
		if ((unsigned int)(hdr + elen) > 256U ||
		    w.off + hdr + elen > w.valid) {
			/* the entry extends past the readable part of
			 * this window: pull in the next window and retry */
			ks_dbg("[kallrecon] linear: window end idx=%d\n", idx);
			if (retried ||
			    slide_advance(&w, (w.valid - w.off) + 4))
				break;
			retried = 1;
			continue;
		}
		retried = 0;

		if (!ks_expand_raw(name_start, ti, tt, nbuf, sizeof(nbuf))) {
			/* a bad token index leaves nbuf partial without the
			 * terminator, never hand it to strcmp()
			 */
			ks_dbg("[kallrecon] linear: decode fail idx=%d\n", idx);
			nbuf[0] = '\0';
		} else {
			decoded++;
		}
		{
			int sample = 0;

			if (idx < 5)
				sample = 1;
			else if (idx == (int)klnum_val / 2)
				sample = 1;
			else if (idx >= (int)klnum_val - 5)
				sample = 1;
			else if (nbuf[0] == 'k' && nbuf[1] == 'a')
				sample = 1;
			if (sample)
				ks_dbg("[kallrecon] linear: [%d] '%s'\n",
					idx, nbuf);
		}
		if (strcmp(nbuf, name) == 0) {
			hit = 1;
			ks_dbg("[kallrecon] linear: HIT idx=%d\n", idx);
			return sym_addr(idx);
		}
		if (ks_cleanup_name_chain(nbuf) && strcmp(nbuf, name) == 0) {
			hit = 1;
			ks_dbg("[kallrecon] linear: HIT(cln) idx=%d\n", idx);
			return sym_addr(idx);
		}

		if (slide_advance(&w, hdr + elen)) {
			ks_dbg("[kallrecon] linear: slide fail idx=%d\n", idx);
			break;
		}
		idx++;
	}

	ks_dbg("[kallrecon] linear: done idx=%d decoded=%d hit=%d\n",
		idx, decoded, hit);
#ifdef KALLRECON_MODULE_LOOKUP
	if (kallrecon_module_klp)
		return kallrecon_module_klp(name);
#endif
	return 0;
}

/* linear scan shares slide_buf/ti_buf/tt_buf globals, serialize it */
static unsigned long name_to_addr_linear(const char *name)
{
	unsigned long ret;

	mutex_lock(&ks_linear_lock);
	ret = name_to_addr_linear_locked(name);
	mutex_unlock(&ks_linear_lock);
	return ret;
}

unsigned long kallsyms_name_to_addr(const char *name)
{
	if (!klseqs_addr) {
#ifdef KALLRECON_MODULE_LOOKUP
		unsigned long addr = name_to_addr_linear(name);

		if (addr || !kallrecon_module_klp)
			return addr;
		return kallrecon_module_klp(name);
#else
		return name_to_addr_linear(name);
#endif
	}

	int low = 0, high = (int)klnum_val - 1;
	char nbuf[256];

	while (low <= high) {
		int mid = low + (high - low) / 2;
		unsigned int seq = get_sym_seq(mid);
		unsigned int off = get_sym_offset(seq);

		/* a decode failure ends the search here, but control still
		 * reaches the module lookup fallback below
		 */
		if (!expand_sym(off, nbuf, sizeof(nbuf)))
			break;
		ks_cleanup_name_chain(nbuf);

		int r = strcmp(name, nbuf);

		if (r > 0)
			low = mid + 1;
		else if (r < 0)
			high = mid - 1;
		else {
			/* walk left to first matching entry, same cleaned
			 * name resolves to smallest address */
			unsigned int first = mid;

			while (first > 0) {
				unsigned int pseq = get_sym_seq(first - 1);
				unsigned int poff = get_sym_offset(pseq);

				if (!expand_sym(poff, nbuf, sizeof(nbuf)))
					break;
				ks_cleanup_name_chain(nbuf);
				if (strcmp(name, nbuf))
					break;
				first--;
			}
			return sym_addr(get_sym_seq(first));
		}
	}
#ifdef KALLRECON_MODULE_LOOKUP
	if (kallrecon_module_klp)
		return kallrecon_module_klp(name);
#endif
	return 0;
}

int sym_name_at(unsigned long addr, char *buf, int max)
{
	int low = 0, high = (int)klnum_val;

	if (max <= 0)
		return -1;
	buf[0] = '\0';
	if (!klnum_val)
		return -1;

	while (high - low > 1) {
		int mid = low + (high - low) / 2;

		if (sym_addr(mid) <= addr)
			low = mid;
		else
			high = mid;
	}

	unsigned int off = get_sym_offset(low);

	if (!expand_sym(off, buf, max))
		return -1;
	ks_cleanup_name_chain(buf);
	return low;
}
