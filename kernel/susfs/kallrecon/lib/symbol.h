// SPDX-License-Identifier: GPL-2.0-only
/*
 * symbol.h
 *
 * Copyright (C) 2026 dere3046
 */

#ifndef SYMBOL_H
#define SYMBOL_H

#include <linux/mutex.h>

#define KS_TT_SIZE	2048

extern unsigned short ti_buf[256];
extern unsigned char tt_buf[KS_TT_SIZE];
extern struct mutex ks_linear_lock;

int ks_expand_raw(const unsigned char *enc, const unsigned short *ti,
		  const unsigned char *tt, char *buf, int max);

int ks_markers_usable(void);

void kr_verify_markers(void);
void kr_markers_reset(void);
void kr_cleanup_reset(void);

#endif
