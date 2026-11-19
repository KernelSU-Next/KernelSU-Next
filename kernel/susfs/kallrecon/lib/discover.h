// SPDX-License-Identifier: GPL-2.0-only
/*
 * discover.h
 *
 * Copyright (C) 2026 dere3046
 */

#ifndef DISCOVER_H
#define DISCOVER_H

#define KS_RUN_MIN	5000		/* min offsets run length */
#define KS_FALLBACK_OFF	0x1000		/* token_table fallback guess */
#define KS_SCAN_BACK	0x400000	/* scan range below token_table */
#define KS_SCAN_FWD	0x200000	/* scan range above token_index */
#define KS_2M_MASK	0x1FFFFFULL	/* kernel base alignment */
#define KS_PAGE_MASK	0xFFFULL	/* page alignment */

int kr_discover_layout(void);
void kr_discover_reset(void);
int kr_setup_token_table(unsigned long ti);
void kr_layout_from_offsets(unsigned long ti);

#endif
