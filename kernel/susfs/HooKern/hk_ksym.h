// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef LKMHOOK_HK_KSYM_H
#define LKMHOOK_HK_KSYM_H

#include <linux/types.h>

struct hk_sym {
	const char *name;
	const char *fallback;
	void **storage;
	bool required;
};

int hk_ksym_register(const struct hk_sym *table);

#endif
