// SPDX-License-Identifier: GPL-2.0
/*
 * susfs_enable_log.c - toggle SUSFS debug logging (ENABLE_LOG feature), F layer.
 *
 * Upstream gates SUSFS_LOGI() behind the static branch susfs_is_log_enabled; an
 * LKM has no static branch, so the same semantics live in a global flag with a
 * /proc node (write "1"/"0", read to query), ON at load like upstream's
 * DEFINE_STATIC_KEY_TRUE.  The flag lives in susfs_enable_log_data.c, the proc
 * node in susfs_enable_log_phys.c and the switch decision in
 * susfs_enable_log_policy.c.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#include "susfs_abi.h"
#include "susfs_log.h"
#include "susfs_enable_log_phys.h"
#include "susfs_enable_log_policy.h"

int susfs_enable_log_init(void)
{
	return susfs_enable_log_proc_init();
}

void susfs_enable_log_exit(void)
{
	susfs_enable_log_proc_exit();
}

/* supercall: CMD_SUSFS_ENABLE_LOG */
void susfs_enable_log_supercall(void __user **arg)
{
	struct st_susfs_log info = {0};

	if (copy_from_user(&info, (void __user *)*arg, sizeof(info))) {
		info.err = -EFAULT;
		goto out;
	}
	susfs_enable_log_set(info.enabled, " (supercall)");
	info.err = 0;
out:
	/* upstream writes back only ->err for input-type commands */
	if (copy_to_user(&((struct st_susfs_log __user *)*arg)->err,
			 &info.err, sizeof(info.err)))
		pr_warn("enable_log supercall copy_to_user failed\n");
}
