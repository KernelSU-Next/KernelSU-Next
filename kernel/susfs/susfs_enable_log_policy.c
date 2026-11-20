/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_enable_log_policy.c - logging switch decision (L layer)
 */

#include <linux/kernel.h>
#include <linux/atomic.h>

#include "susfs_enable_log_data.h"
#include "susfs_enable_log_policy.h"
#include "susfs_log.h"

void susfs_enable_log_set(bool on, const char *tag)
{
	if (on) {
		WRITE_ONCE(susfs_log_on, true);
		SUSFS_LOGI("susfs: enable logging to kernel%s\n", tag ? tag : "");
	} else {
		WRITE_ONCE(susfs_log_on, false);
		/* Unconditional on purpose (upstream uses its unconditional
		 * SUSFS_LOGE here): the confirmation that silence is now in
		 * effect must not itself be silenced. */
		pr_info("susfs: disable logging to kernel%s\n", tag ? tag : "");
	}
}
