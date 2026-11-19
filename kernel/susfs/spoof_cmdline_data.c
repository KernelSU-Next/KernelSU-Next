/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * spoof_cmdline_data.c - bootconfig spoof state (D layer)
 */

#include <linux/kernel.h>
#include <linux/mutex.h>

#include "spoof_cmdline_data.h"

/* The insmod parameter buffer; the F layer registers the parameter itself
 * (module_param_string) so this file stays pure state. */
char spoof_cmdline_param[SPOOF_CMDLINE_PARAM_LEN];

char *spoof_cmdline_orig;
char *spoof_cmdline_live;	/* heap-allocated, currently published */
bool spoof_cmdline_active;

/* Strings that saved_boot_config used to point at.  A published string must
 * NEVER be freed while saved_boot_config might reach it: /proc/bootconfig is
 * read via seq_puts with no lock of ours, so freeing the old buffer before
 * republishing let a reader touch freed memory, and a set() that then failed
 * kstrdup left saved_boot_config dangling at the buffer it had just freed.
 * Retiring instead costs one 8 KB string per update. */
LIST_HEAD(spoof_cmdline_retired);

/* Serialises publication and the retired list: two concurrent supercalls
 * could otherwise both take the !active branch and record a FAKE pointer as
 * orig, leaving /proc/bootconfig spoofed after unload. */
struct mutex spoof_cmdline_lock;

void spoof_cmdline_data_init(void)
{
	mutex_init(&spoof_cmdline_lock);
}
