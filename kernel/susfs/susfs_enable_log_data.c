/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_enable_log_data.c - logging switch (D layer)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/atomic.h>

#include "susfs_enable_log_data.h"

bool susfs_log_on = true;

/* Load-time switch, read-only in sysfs; the runtime switch is the /proc node and
 * the supercall, the interfaces upstream's userspace drives (ksud insmod ...
 * enable_log=0).  A load that starts silent is the only state in which the
 * unconditional "loaded." line of susfs_main.c matters: without it,
 * silent-but-loaded looks unloaded. */
module_param_named(enable_log, susfs_log_on, bool, 0444);

bool susfs_log_enabled(void)
{
	return READ_ONCE(susfs_log_on);
}
