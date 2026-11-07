// SPDX-License-Identifier: GPL-2.0-only
/*
 * hint.h
 *
 * Copyright (C) 2026 dere3046
 */

#ifndef HINT_H
#define HINT_H

#include <linux/types.h>
#include "core.h"

enum kallrecon_fail {
	KALLRECON_OK = 0,
	KALLRECON_NO_ANCHOR,
	KALLRECON_NO_TOKEN_INDEX,
	KALLRECON_NO_OFFSETS,
	KALLRECON_NO_LAYOUT,
	KALLRECON_HINT_INVALID,
	KALLRECON_TIMEOUT,
};

/* runtime addresses only, a static value has to be slid by the caller.
 * zero means auto discovery for that field */
struct kallrecon_hint {
	unsigned long offsets;
	unsigned long relative_base;
	unsigned long num_syms;
	unsigned long names;
	unsigned long markers;
	unsigned long seqs;
	unsigned long token_table;
	unsigned long token_index;

	unsigned long scan_back;
	unsigned long scan_fwd;
	unsigned int  timeout_ms;

	struct kallrecon_scan_range scan;
};

struct kallrecon_layout {
	enum layout_v layout;
	unsigned long kernel_base;
	unsigned long offsets;
	unsigned long relative_base;
	unsigned long relative_base_val;
	unsigned long num_syms;
	unsigned long num_syms_val;
	unsigned long names;
	unsigned long markers;
	unsigned long seqs;
	unsigned long token_table;
	unsigned long token_index;
};

int kallrecon_supply(const struct kallrecon_hint *hint);
void kallrecon_layout_get(struct kallrecon_layout *out);
enum kallrecon_fail kallrecon_fail_reason(void);

const struct kallrecon_hint *kr_hint(void);
int kr_hint_active(void);
void kr_fail_set(enum kallrecon_fail r);
void kr_fail_clear(void);
void kr_timeout_arm(unsigned int ms);
int kr_timeout_hit(void);

#endif
