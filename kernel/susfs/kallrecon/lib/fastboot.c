// SPDX-License-Identifier: GPL-2.0-only
/*
 * fastboot.c
 *
 * Copyright (C) 2026 dere3046
 */

#ifdef KALLRECON_FAST_BOOT

#include <linux/kallsyms.h>
#include <linux/string.h>
#include "core.h"
#include "fastboot.h"

/* sprint walk both ways from the anchor in turn, LTO decides which
 * side holds the target, best effort only and a miss falls back to
 * the full lookup */

/* one step from *addr: 1 found 0 advanced -1 dead end
 * found leaves *addr at the symbol start */
static int fast_step(unsigned long *addr, int up)
{
	static const char klp_name[] = "kallsyms_lookup_name";
	char buf[KSYM_SYMBOL_LEN];
	char *plus;
	unsigned long off = 0, size = 0;
	const char *q;

	sprint_symbol(buf, *addr);

	plus = strrchr(buf, '+');
	if (!plus)
		return -1;

	/* hand parse +0xoff/0xsize, sscanf is not exported */
	q = plus + 1;
	if (q[0] == '0' && (q[1] == 'x' || q[1] == 'X'))
		q += 2;
	while (*q && *q != '/') {
		char c = *q++;
		unsigned long d;

		if (c >= '0' && c <= '9')
			d = c - '0';
		else if (c >= 'a' && c <= 'f')
			d = c - 'a' + 10;
		else
			return -1;
		off = (off << 4) | d;
	}
	if (*q++ != '/')
		return -1;
	if (q[0] == '0' && (q[1] == 'x' || q[1] == 'X'))
		q += 2;
	while (*q) {
		char c = *q++;
		unsigned long d;

		if (c >= '0' && c <= '9')
			d = c - '0';
		else if (c >= 'a' && c <= 'f')
			d = c - 'a' + 10;
		else
			break;
		size = (size << 4) | d;
	}

	/* exact match only, module_kallsyms_lookup_name and
	 * kallsyms_lookup_names must not hit, names may carry an
	 * LTO suffix before the offset marker */
	if (!strncmp(buf, klp_name, sizeof(klp_name) - 1)) {
		char e = buf[sizeof(klp_name) - 1];

		if (e == '+' || e == '$' || e == '.') {
			*addr -= off;	/* symbol start */
			return 1;
		}
	}

	if (up) {
		/* next function start, size is the distance to the
		 * following symbol */
		if (!size || size <= off)
			return -1;
		*addr = (*addr - off) + size;
	} else {
		/* step to the previous function boundary */
		if (off)
			*addr = (*addr - off) - 1;
		else if (*addr > 4)
			*addr -= 4;
		else
			return -1;
	}
	return 0;
}

unsigned long fast_find_klp(void)
{
	unsigned long up = sprint_addr, down = sprint_addr;
	int up_dead = 0, down_dead = 0;

	for (int i = 0; i < 200000 && (!up_dead || !down_dead); i++) {
		if (!up_dead) {
			int r = fast_step(&up, 1);

			if (r > 0)
				return up;
			up_dead = r < 0;
		}

		if (!down_dead) {
			int r = fast_step(&down, 0);

			if (r > 0)
				return down;
			down_dead = r < 0;
		}
	}
	return 0;
}

#endif
