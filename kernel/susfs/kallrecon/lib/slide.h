// SPDX-License-Identifier: GPL-2.0-only
/*
 * slide.h
 *
 * Copyright (C) 2026 dere3046
 */

#ifndef SLIDE_H
#define SLIDE_H

#include <linux/types.h>

#define KS_WIN_SIZE	(64 * 1024)	/* slide window chunk */
#define KS_WIN_MARGIN	512		/* slide window overlap */
#define KS_HOLE_MAX	(8 * 1024 * 1024)	/* hole probe cap */

extern unsigned int slide_buf[];

struct slide_win {
	unsigned long addr;
	unsigned int  chunksz;
	unsigned int  margin;
	unsigned int  off;
	unsigned int  valid;	/* readable bytes in this window */
	unsigned long next;	/* hole tail to resume from, 0 = none */
};

int slide_init(struct slide_win *w, unsigned long pos,
	       unsigned int chunksz, unsigned int margin);
int slide_advance(struct slide_win *w, unsigned int n);

static inline void *slide_ptr(const struct slide_win *w, const void *buf)
{
	return (unsigned char *)buf + w->off;
}

static inline unsigned long slide_addr(const struct slide_win *w)
{
	return w->addr + w->off;
}

#endif
