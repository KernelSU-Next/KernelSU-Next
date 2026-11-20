/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUSFS_LOG_H
#define __SUSFS_LOG_H

#include <linux/types.h>
#include <linux/printk.h>

#ifdef pr_fmt
#undef pr_fmt
#define pr_fmt(fmt) "susfs_guard_lkm: " fmt
#endif

/* ENABLE_LOG the way upstream has it: SUSFS_LOGI() is the informational channel (rule add/remove,
 * hook arming, hit paths) behind the switch CMD_SUSFS_ENABLE_LOG / /proc/susfs_enable_log toggles -
 * default ON, matching upstream's DEFINE_STATIC_KEY_TRUE(susfs_is_log_enabled).  pr_warn/pr_err stay
 * unconditional: a failure must be visible whether or not logging is on (upstream's SUSFS_LOGE is
 * unconditional too).  Measured before the fix: the flag had no readers, so `enable_log 0` plus a
 * rule add still printed "susfs_guard_lkm: sus_path: ..." in dmesg - a root-side fingerprint. */
bool susfs_log_enabled(void);

#define SUSFS_LOGI(fmt, ...)						\
	do {								\
		if (susfs_log_enabled())				\
			pr_info(fmt, ##__VA_ARGS__);			\
	} while (0)

#endif
