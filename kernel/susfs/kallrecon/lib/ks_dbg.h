// SPDX-License-Identifier: GPL-2.0-only
/*
 * ks_dbg.h
 *
 * Copyright (C) 2026 dere3046
 */

#ifndef KS_DBG_H
#define KS_DBG_H

#include <linux/printk.h>

#ifdef KALLRECON_DEBUG
#define ks_dbg(fmt, ...) pr_info(fmt, ##__VA_ARGS__)
#else
/* keep the arguments "used" so -Wunused-but-set-variable stays quiet */
#define ks_dbg(fmt, ...) do { if (0) pr_info(fmt, ##__VA_ARGS__); } while (0)
#endif

#endif
