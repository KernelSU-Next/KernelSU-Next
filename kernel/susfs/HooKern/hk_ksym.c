// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/module.h>
#include <linux/printk.h>

#include "hk.h"
#include "hk_ksym.h"

int hk_ksym_register(const struct hk_sym *table)
{
	const struct hk_sym *e;
	unsigned long v;
	int fail = 0;

	if (!table)
		return -EINVAL;

	for (e = table; e->name; e++) {
		v = hk_resolve(e->name);
		if (!v && e->fallback)
			v = hk_resolve(e->fallback);
		if (e->storage)
			*e->storage = (void *)v;
		if (!v && e->required) {
			pr_warn("[lkmhook] sym missing %s\n", e->name);
			fail++;
		}
	}
	return fail ? -ENOENT : 0;
}
