// SPDX-License-Identifier: GPL-2.0-only
/*
 * access.h
 *
 * Copyright (C) 2026 dere3046
 */

#ifndef ACCESS_H
#define ACCESS_H

#include <linux/types.h>

/* address formula proven at discovery time, latched so sym_addr()
 * always uses the formula that resolved real symbols during discovery.
 * x86 5.10~6.12 KALLSYMS_ABSOLUTE_PERCPU is covered by KS_MODE_ABSPCPU,
 * 6.18+ removed the option and uses KS_MODE_RB like arm64 */
#define KS_MODE_RB	0
#define KS_MODE_ABSPCPU	1
#define KS_MODE_SELFREL	2

extern int kl_addr_mode;

int safe_read(void *dst, const void *src, size_t sz);
int ks_addr_try(unsigned long rb, u32 off, int *mode);

#endif
