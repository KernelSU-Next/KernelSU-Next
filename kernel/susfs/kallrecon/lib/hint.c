// SPDX-License-Identifier: GPL-2.0-only
/*
 * hint.c
 *
 * Copyright (C) 2026 dere3046
 */

#include <linux/atomic.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <linux/errno.h>
#include "core.h"
#include "hint.h"

static struct kallrecon_hint kr_hint_state;
static int kr_hint_on;
static atomic_t kr_fail = ATOMIC_INIT(KALLRECON_OK);
static unsigned long kr_deadline;

static int hint_any(const struct kallrecon_hint *h)
{
	return h->offsets || h->relative_base || h->num_syms ||
	       h->names || h->markers || h->seqs || h->token_table ||
	       h->token_index || h->scan_back || h->scan_fwd ||
	       h->timeout_ms;
}

int kallrecon_supply(const struct kallrecon_hint *hint)
{
	if (!hint)
		return -EINVAL;
	kr_hint_state = *hint;
	kr_hint_on = hint_any(hint);
	return 0;
}

const struct kallrecon_hint *kr_hint(void)
{
	return &kr_hint_state;
}

int kr_hint_active(void)
{
	return kr_hint_on;
}

void kr_fail_set(enum kallrecon_fail r)
{
	atomic_cmpxchg(&kr_fail, KALLRECON_OK, r);
}

void kr_fail_clear(void)
{
	atomic_set(&kr_fail, KALLRECON_OK);
}

enum kallrecon_fail kallrecon_fail_reason(void)
{
	return atomic_read(&kr_fail);
}

void kr_timeout_arm(unsigned int ms)
{
	kr_deadline = ms ? jiffies + msecs_to_jiffies(ms) : 0;
}

int kr_timeout_hit(void)
{
	if (!kr_deadline)
		return 0;
	if (time_after(jiffies, kr_deadline)) {
		kr_fail_set(KALLRECON_TIMEOUT);
		return 1;
	}
	return 0;
}

void kallrecon_layout_get(struct kallrecon_layout *out)
{
	if (!out)
		return;
	memset(out, 0, sizeof(*out));
	out->layout = kl_layout;
	out->kernel_base = kernel_base;
	out->offsets = kloffs_addr;
	out->relative_base = klbase_addr;
	out->relative_base_val = klbase_val;
	out->num_syms = klnum_addr;
	out->num_syms_val = klnum_val;
	out->names = klnames_addr;
	out->markers = klmarks_addr;
	out->seqs = klseqs_addr;
	out->token_table = kltable_addr;
	out->token_index = klindex_addr;
}
