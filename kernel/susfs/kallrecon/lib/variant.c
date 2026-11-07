// SPDX-License-Identifier: GPL-2.0-only
/*
 * variant.c
 *
 * Copyright (C) 2026 dere3046
 */

#include <linux/string.h>
#include "core.h"
#include "slide.h"
#include "symbol.h"
#include "variant.h"

enum ks_match {
	KS_MATCH_UNKNOWN = -1,
	KS_MATCH_NONE,
	KS_MATCH_EXACT,
	KS_MATCH_VARIANT,
	KS_MATCH_CFI,
};

/* names stream holes are not expected after discovery validation
 * jump to the first 256 symbol marker at or after want
 * markers are the verified authoritative boundaries */
static int ks_resync_marker(struct slide_win *w, unsigned int *idx,
			    unsigned int want)
{
	unsigned int m = (want + 255) / 256;

	if (!ks_markers_usable() || !klmarks_addr)
		return -1;

	for (; m * 256 < klnum_val; m++) {
		u32 mo;

		if (safe_read(&mo, (void *)(klmarks_addr + m * 4), 4))
			break;
		if (slide_init(w, klnames_addr + mo,
			       KS_WIN_SIZE, KS_WIN_MARGIN))
			continue;
		*idx = m * 256;
		return 0;
	}
	return -1;
}

#ifdef KALLRECON_VARIANT_BISECT
#define KS_BISECT_CLUSTER_MAX	4096

static int ks_bisect_on;

void kallrecon_set_variant_bisect(int enable)
{
	ks_bisect_on = enable ? 1 : 0;
}

/* strip the first LTO suffix
 * the sort space of the 6.1 and 6.6 seqs tables */
static void ks_strip_llvm(char *s)
{
	char *p = strstr(s, ".llvm.");

	if (p)
		*p = '\0';
}

/* collect the best qualifying entries of one sorted space
 * smallest address among matches and among cfi_jt entries
 * a prefix cluster is contiguous so one lower bound plus a short walk suffices
 * an error return marks the space unreliable */
static int ks_bisect_space(const char *name, size_t name_len, int cleaned,
			   unsigned long *first, unsigned long *cfi)
{
	char nbuf[256];
	int low = 0, high = (int)klnum_val;
	unsigned int pos, n;

	while (low < high) {
		int mid = low + (high - low) / 2;
		unsigned int seq = get_sym_seq(mid);
		unsigned int off = get_sym_offset(seq);

		if (!expand_sym(off, nbuf, sizeof(nbuf)))
			return -1;
		if (cleaned)
			ks_strip_llvm(nbuf);
		if (strcmp(nbuf, name) < 0)
			low = mid + 1;
		else
			high = mid;
	}

	pos = (unsigned int)low;
	for (n = 0; n < KS_BISECT_CLUSTER_MAX && pos < klnum_val; n++, pos++) {
		unsigned int seq = get_sym_seq(pos);
		unsigned int off = get_sym_offset(seq);
		char raw[256], key[256];
		unsigned long addr;
		size_t nlen;

		if (!expand_sym(off, raw, sizeof(raw)))
			return -1;
		strscpy(key, raw, sizeof(key));
		if (cleaned)
			ks_strip_llvm(key);
		if (strncmp(key, name, name_len) != 0)
			break;

		nlen = strlen(raw);
		if (strcmp(raw, name) != 0) {
			if (nlen <= name_len ||
			    strncmp(raw, name, name_len) != 0 ||
			    (raw[name_len] != '.' && raw[name_len] != '$'))
				continue;
		}
		addr = sym_addr(seq);
		if (!addr)
			continue;
		if (!*first || addr < *first)
			*first = addr;
		if (nlen >= sizeof(".cfi_jt") - 1 &&
		    !strcmp(raw + nlen - (sizeof(".cfi_jt") - 1),
			    ".cfi_jt")) {
			if (!*cfi || addr < *cfi)
				*cfi = addr;
		}
	}
	if (n >= KS_BISECT_CLUSTER_MAX && pos < klnum_val)
		return -1;
	return 0;
}

/* search both sort spaces the version families use then merge
 * the correct space finds every qualifying entry
 * the other only adds real matches since raw rules apply to both */
static int ks_bisect_try(const char *name, size_t name_len,
			 unsigned long *first, unsigned long *cfi)
{
	unsigned long f2 = 0, c2 = 0;

	*first = 0;
	*cfi = 0;
	if (ks_bisect_space(name, name_len, 0, first, cfi))
		return -1;
	if (ks_bisect_space(name, name_len, 1, &f2, &c2))
		return -1;
	if (!*first || (f2 && f2 < *first))
		*first = f2;
	if (!*cfi || (c2 && c2 < *cfi))
		*cfi = c2;
	return 0;
}
#endif

/* full expansion reference for one entry
 * exact or dot dollar boundary prefix or cfi_jt variant
 * every decode anomaly is a non match */
static int ks_match_slow(const unsigned char *enc, const char *name,
			 size_t name_len)
{
	static const char cfi_suffix[] = ".cfi_jt";
	char nbuf[256];
	size_t nlen;

	if (!ks_expand_raw(enc, ti_buf, tt_buf, nbuf, sizeof(nbuf)))
		return KS_MATCH_NONE;

	nlen = strlen(nbuf);
	if (strcmp(nbuf, name) == 0)
		return KS_MATCH_EXACT;
	if (nlen > name_len && strncmp(nbuf, name, name_len) == 0 &&
	    (nbuf[name_len] == '.' || nbuf[name_len] == '$')) {
		if (nlen >= sizeof(cfi_suffix) - 1 &&
		    !strcmp(nbuf + nlen - (sizeof(cfi_suffix) - 1),
			    cfi_suffix))
			return KS_MATCH_CFI;
		return KS_MATCH_VARIANT;
	}
	return KS_MATCH_NONE;
}

#ifdef KALLRECON_VARIANT_FAST
#define KS_CFI_TAIL	0x002e6366695f6a74ULL	/* cfi_jt */

/* decode token by token and compare as the name grows
 * a symbol usually dies at the first or second character
 * undecidable cases go back to the full expansion
 * so the result never depends on this fast path */
static int ks_match_fast(const unsigned char *enc, const char *name,
			 size_t name_len)
{
	unsigned long tail = 0;
	unsigned int len;
	size_t pos = 0;
	int skipped = 0, variant = 0;

	if (name_len > 255)
		return KS_MATCH_UNKNOWN;

	len = *enc++;
	if (len & 0x80)
		len = (len & 0x7F) | (*enc++ << 7);
	if (len > 256U)
		return KS_MATCH_UNKNOWN;

	for (unsigned int i = 0; i < len; i++) {
		unsigned short to = ti_buf[*enc++];
		const unsigned char *tp;

		if (to >= KS_TT_SIZE)
			return KS_MATCH_UNKNOWN;
		tp = tt_buf + to;
		while (*tp) {
			unsigned char ch;

			if (tp - tt_buf >= KS_TT_SIZE)
				return KS_MATCH_UNKNOWN;
			ch = *tp++;
			if (!skipped) {
				skipped = 1;
				continue;
			}
			if (!variant && pos < name_len) {
				if (ch != (unsigned char)name[pos])
					return KS_MATCH_NONE;
				pos++;
				continue;
			}
			if (!variant) {
				if (ch != '.' && ch != '$')
					return KS_MATCH_NONE;
				variant = 1;
			}
			tail = ((tail << 8) | ch) & 0x00ffffffffffffffULL;
			if (++pos > 255)
				return KS_MATCH_UNKNOWN;
		}
	}

	if (!variant)
		return pos == name_len ? KS_MATCH_EXACT : KS_MATCH_NONE;
	return tail == KS_CFI_TAIL ? KS_MATCH_CFI : KS_MATCH_VARIANT;
}

static int ks_match(const unsigned char *enc, const char *name,
		    size_t name_len)
{
	int kind = ks_match_fast(enc, name, name_len);

	if (kind == KS_MATCH_UNKNOWN)
		kind = ks_match_slow(enc, name, name_len);
	return kind;
}
#else
static int ks_match(const unsigned char *enc, const char *name,
		    size_t name_len)
{
	return ks_match_slow(enc, name, name_len);
}
#endif

/* resolve a symbol that may only exist with a suffix variant
 * exact name or prefix match at a dot or dollar boundary
 * prefer_cfi_jt keeps scanning for a cfi_jt entry and falls back to the first match
 * otherwise the first match wins immediately */
unsigned long kallrecon_find_variant(const char *name, int prefer_cfi_jt)
{
	unsigned long match = 0;
	struct slide_win w;
	size_t name_len;
	unsigned int idx = 0;
	int retried = 0;

	if (!name || !name[0] || !klnum_val || !klnames_addr)
		return 0;

	name_len = strlen(name);

#ifdef KALLRECON_VARIANT_BISECT
	if (ks_bisect_on && klseqs_addr) {
		unsigned long bfirst, bcfi;

		if (!ks_bisect_try(name, name_len, &bfirst, &bcfi)) {
			unsigned long hit = prefer_cfi_jt && bcfi ? bcfi : bfirst;

			if (hit)
				return hit;
		}
	}
#endif

	mutex_lock(&ks_linear_lock);
	if (safe_read(ti_buf, (void *)klindex_addr, sizeof(ti_buf)) ||
	    safe_read(tt_buf, (void *)kltable_addr, sizeof(tt_buf)))
		goto out;
	if (slide_init(&w, klnames_addr, KS_WIN_SIZE, KS_WIN_MARGIN))
		goto out;

	while (idx < klnum_val) {
		const unsigned char *name_start = slide_ptr(&w, slide_buf);
		unsigned int len, hdr = 1;
		unsigned long addr;
		int kind;

		len = name_start[0];
		if (len & 0x80) {
			len = (len & 0x7F) | (name_start[1] << 7);
			hdr = 2;
		}

		if (len > 256U) {
			if (ks_resync_marker(&w, &idx, idx + 1))
				break;
			retried = 0;
			continue;
		}

		if (w.off + hdr + len > w.valid) {
			if (!w.next && !retried &&
			    !slide_advance(&w, (w.valid - w.off) + 4)) {
				retried = 1;
				continue;
			}
			if (ks_resync_marker(&w, &idx, idx + 1))
				break;
			retried = 0;
			continue;
		}
		retried = 0;

		kind = ks_match(name_start, name, name_len);
		if (kind != KS_MATCH_NONE) {
			addr = sym_addr(idx);
			if (addr) {
				if (prefer_cfi_jt && kind == KS_MATCH_CFI) {
					match = addr;
					break;
				}
				if (!match) {
					match = addr;
					if (!prefer_cfi_jt)
						break;
				}
			}
		}

		if (slide_advance(&w, hdr + len)) {
			if (ks_resync_marker(&w, &idx, idx + 1))
				break;
			retried = 0;
			continue;
		}
		idx++;
	}

out:
	mutex_unlock(&ks_linear_lock);
	return match;
}
