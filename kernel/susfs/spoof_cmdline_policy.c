/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * spoof_cmdline_policy.c - bootconfig spoof decision (L layer)
 */

#include <linux/kernel.h>
#include <linux/slab.h>

#include "spoof_cmdline_phys.h"
#include "spoof_cmdline_policy.h"

/* Allocate first, publish second.  Returns 0 or a negative errno. */
int spoof_cmdline_set(const char *fake)
{
	char *dup = kstrdup(fake, GFP_KERNEL);

	if (!dup)
		return -ENOMEM;
	spoof_cmdline_publish(dup);
	return 0;
}
