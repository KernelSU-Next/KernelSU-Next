// SPDX-License-Identifier: GPL-2.0-only
/*
 * fastboot.h
 *
 * Copyright (C) 2026 dere3046
 *
 * reference: https://xcellerator.github.io/posts/linux_rootkits_11/
 */

#ifndef FASTBOOT_H
#define FASTBOOT_H

#ifdef KALLRECON_FAST_BOOT
#ifndef KALLRECON_FAST_BOOT_ALL
#define KALLRECON_FAST_BOOT_ALL 0
#endif

unsigned long fast_find_klp(void);
#endif

#endif
