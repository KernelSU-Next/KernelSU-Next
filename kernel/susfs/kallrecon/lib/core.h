// SPDX-License-Identifier: GPL-2.0-only
/*
 * core.h
 *
 * Copyright (C) 2026 dere3046
 */

#ifndef CORE_H
#define CORE_H

#include <linux/types.h>

int safe_read(void *dst, const void *src, size_t sz);

unsigned long kr_get_sprint_addr(void);

extern unsigned long sprint_addr;
extern unsigned long kernel_base;
extern unsigned long klbase_addr;
extern unsigned long klbase_val;
extern unsigned long kloffs_addr;
extern unsigned long klindex_addr;
extern unsigned long klseqs_addr;
extern int klseqs_stride;	/* seqs entry width, 3 or 4 bytes */
extern unsigned int  klnum_val;
extern unsigned long klmarks_addr;
extern unsigned long kltable_addr;
extern unsigned long klnames_addr;
extern unsigned long klnum_addr;

enum layout_v {
	LAYOUT_V1 = 1,
	LAYOUT_V2 = 2,
	LAYOUT_V3 = 3,
};

extern enum layout_v kl_layout;
extern int is_v1_layout;	/* mirror of kl_layout == LAYOUT_V1 */

/* absolute kernel address window both zero means unset
 * consumers may fill it to steer searches the range is only
 * a priority order */
struct kallrecon_scan_range {
	unsigned long lo;
	unsigned long hi;
};

extern unsigned long (*kallrecon_klp)(const char *name);
#ifdef KALLRECON_MODULE_LOOKUP
extern unsigned long (*kallrecon_module_klp)(const char *name); /* experimental, may be unstable */
#endif

void find_kallsyms_base(void);
int kallrecon_retry(void);

enum kallrecon_cleanup {
	KALLRECON_CLEANUP_AUTO = 0,
	KALLRECON_CLEANUP_SEQS,
	KALLRECON_CLEANUP_LLVM,
	KALLRECON_CLEANUP_DOLLAR,
};

void kallrecon_set_cleanup(int (*cb)(char *s)); /* NULL detaches user cleanup hook */
void kallrecon_set_cleanup_mode(enum kallrecon_cleanup mode);
unsigned long sym_addr(int idx);
int expand_sym(unsigned int off, char *buf, int max);
unsigned int get_sym_seq(int idx);
unsigned int get_sym_offset(unsigned int seq);
unsigned long kallsyms_name_to_addr(const char *name);
int sym_name_at(unsigned long addr, char *buf, int max);

#endif
