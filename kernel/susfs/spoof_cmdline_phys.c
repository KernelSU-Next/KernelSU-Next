/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * spoof_cmdline_phys.c - bootconfig pointer publication (P layer)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "spoof_cmdline_data.h"
#include "spoof_cmdline_phys.h"
#include "susfs_core.h"

/* unexported static var; ksud insmod relocates it via kallsyms */
extern char *saved_boot_config;

/* Free what nobody can still be reading.  Retiring without ever freeing leaked
 * up to 8 KB per set() for the module's lifetime, unbounded over repeated
 * supercalls and unreachable after unload. */
static void spoof_drain_retired(void)
{
	struct retired_str *r, *tmp;
	LIST_HEAD(doomed);

	mutex_lock(&spoof_cmdline_lock);
	list_splice_init(&spoof_cmdline_retired, &doomed);
	mutex_unlock(&spoof_cmdline_lock);

	if (list_empty(&doomed))
		return;

	susfs_wait_for_readers();

	list_for_each_entry_safe(r, tmp, &doomed, list) {
		list_del(&r->list);
		kfree(r->s);
		kfree(r);
	}
}

static void spoof_retire(char *s)
{
	struct retired_str *r;

	if (!s)
		return;
	r = kmalloc(sizeof(*r), GFP_KERNEL);
	if (!r)
		return;		/* leak rather than free: never free a published string */
	r->s = s;
	list_add_tail(&r->list, &spoof_cmdline_retired);
}

void spoof_cmdline_publish(char *dup)
{
	mutex_lock(&spoof_cmdline_lock);
	if (!spoof_cmdline_active)
		spoof_cmdline_orig = saved_boot_config;
	else
		spoof_retire(spoof_cmdline_live);

	spoof_cmdline_live = dup;
	saved_boot_config = dup;
	spoof_cmdline_active = true;
	mutex_unlock(&spoof_cmdline_lock);

	/* Outside the lock: the wait is unbounded and only the list needs serialising. */
	spoof_drain_retired();
}

void spoof_cmdline_restore(void)
{
	char *live = NULL;

	mutex_lock(&spoof_cmdline_lock);
	if (spoof_cmdline_active) {
		saved_boot_config = spoof_cmdline_orig;
		spoof_cmdline_active = false;
		spoof_cmdline_orig = NULL;
		live = spoof_cmdline_live;
	}
	spoof_cmdline_live = NULL;
	if (live)
		spoof_retire(live);
	mutex_unlock(&spoof_cmdline_lock);

	/* The pointer is unpublished now, so wait out whoever may still hold it
	 * and then free everything this feature allocated - the live buffer
	 * included, which used to be dropped on the floor at unload along with
	 * the whole retired list. */
	spoof_drain_retired();
}
