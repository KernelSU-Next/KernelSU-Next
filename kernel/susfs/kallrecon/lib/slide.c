// SPDX-License-Identifier: GPL-2.0-only
/*
 * slide.c
 *
 * Copyright (C) 2026 dere3046
 */

#include <linux/types.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/build_bug.h>
#include "core.h"
#include "slide.h"

#ifdef KALLRECON_DEBUG
#define slide_dbg(fmt, ...) pr_info("[slide] " fmt, ##__VA_ARGS__)
#else
#define slide_dbg(fmt, ...) do { if (0) pr_info("[slide] " fmt, ##__VA_ARGS__); } while (0)
#endif

#define SLIDE_BUF_WORDS (18 * 1024)
unsigned int slide_buf[SLIDE_BUF_WORDS];

/*
 * The window reads chunksz + margin bytes, and check_ti_strong() reads
 * 256 u16 past slide_ptr() at any offset inside the window.
 */
static_assert(sizeof(slide_buf) >= KS_WIN_SIZE + KS_WIN_MARGIN);
static_assert(KS_WIN_MARGIN >= 256 * sizeof(unsigned short));

/* load one window at addr: try the whole read first, on failure fall
 * back to page by page so the readable prefix is still salvaged; the
 * unreadable tail is zeroed and next records where it starts */
static void slide_read(struct slide_win *w, unsigned long addr)
{
	unsigned int total = w->chunksz + w->margin;
	unsigned int got = 0;

	w->addr = addr;
	w->off = 0;

	if (!safe_read(slide_buf, (void *)addr, total)) {
		w->valid = total;
		w->next = 0;
		return;
	}

	while (got < total) {
		unsigned int n = total - got;

		if (n > 0x1000)
			n = 0x1000;
		if (safe_read((unsigned char *)slide_buf + got,
			      (void *)(addr + got), n))
			break;
		got += n;
	}
	w->valid = got;
	w->next = got < total ? addr + got : 0;
	if (got < total)
		memset((unsigned char *)slide_buf + got, 0, total - got);
}

/* load a window, walking forward page by page while it starts inside
 * a hole. kallsyms tables live in the kernel image, so a gap beyond
 * KS_HOLE_MAX means there is nothing left to find */
static int slide_load(struct slide_win *w, unsigned long addr)
{
	unsigned long cap = addr + KS_HOLE_MAX;

	slide_read(w, addr);
	while (!w->valid && w->next && w->next < cap)
		slide_read(w, w->next + 0x1000);
	return w->valid != 0;
}

int slide_init(struct slide_win *w, unsigned long pos,
	       unsigned int chunksz, unsigned int margin)
{
	w->chunksz = chunksz;
	w->margin = margin;

	slide_dbg("init pos=0x%lx chunksz=%u margin=%u\n", pos, chunksz, margin);

	if (!slide_load(w, pos)) {
		slide_dbg("init FAIL read @ 0x%lx\n", pos);
		return -1;
	}

	slide_dbg("init ok addr=0x%lx valid=%u\n", w->addr, w->valid);
	return 0;
}

int slide_advance(struct slide_win *w, unsigned int n)
{
	unsigned long old_addr = w->addr;
	unsigned int old_off = w->off;
	unsigned int limit = w->next ? w->valid : w->chunksz;
	unsigned long cursor, new_addr;

	w->off += n;
	if (w->off < limit)
		return 0;

	cursor = w->addr + w->off;
	new_addr = w->next ? w->next : (cursor - w->margin) & ~0xFFFULL;

	slide_dbg("slide cursor=0x%lx old=0x%lx new=0x%lx\n",
		  cursor, w->addr, new_addr);

	if (!slide_load(w, new_addr)) {
		slide_dbg("slide FAIL read @ 0x%lx\n", new_addr);
		w->addr = old_addr;
		w->off = old_off;
		return -1;
	}

	/* a regular slide keeps the cursor position (margin overlap),
	 * a window loaded after a hole is searched from its head */
	if (cursor >= w->addr && cursor - w->addr < w->valid)
		w->off = cursor - w->addr;
	else
		w->off = 0;

	slide_dbg("slide ok addr=0x%lx valid=%u next=0x%lx off=%u\n",
		  w->addr, w->valid, w->next, w->off);
	return 0;
}
