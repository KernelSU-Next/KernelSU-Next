// SPDX-License-Identifier: GPL-2.0-only
/*
 * variant.h
 *
 * Copyright (C) 2026 dere3046
 */

#ifndef VARIANT_H
#define VARIANT_H

#include "core.h"

unsigned long kallrecon_find_variant(const char *name, int prefer_cfi_jt);

#ifdef KALLRECON_VARIANT_BISECT
void kallrecon_set_variant_bisect(int enable);
#endif

#endif
