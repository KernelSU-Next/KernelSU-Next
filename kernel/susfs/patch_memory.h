/* SPDX-License-Identifier: GPL-2.0-only */
/* patch_memory.h - arbitrary kernel text patching (ported from KernelSU/SukiSU).
 * Copyright (C) 2023 bmax121. All Rights Reserved. */

#ifndef __SUSFS_PATCH_MEMORY_H
#define __SUSFS_PATCH_MEMORY_H

#include <linux/types.h>
#include <linux/version.h>

#ifdef __aarch64__
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
#include "asm/text-patching.h"
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 14, 0)
#include "asm/patching.h"
#else
#include "asm/insn.h"
#endif
#elif __x86_64__
#include <asm/ptrace.h>
#else
#error "Unsupported arch"
#endif

#define KSU_PATCH_TEXT_FLUSH_DCACHE 1
#define KSU_PATCH_TEXT_FLUSH_ICACHE 2

unsigned long phys_from_virt(unsigned long addr, int *err);
int ksu_patch_memory_init(void);
int ksu_patch_text(void *dst, void *src, size_t len, int flags);
void *scan_call_to(void *start, size_t size, void *target);

#endif
