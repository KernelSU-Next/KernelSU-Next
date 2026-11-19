// SPDX-License-Identifier: GPL-2.0
/*
 * spoof_cmdline.c - spoof /proc/bootconfig (SUSFS SPOOF_CMDLINE_OR_BOOTCONFIG).
 *
 * F layer: the insmod parameter, init/exit orchestration and the supercall.
 * The string publication is in spoof_cmdline_phys.c, the state in
 * spoof_cmdline_data.c and the allocation/decision in spoof_cmdline_policy.c.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "susfs_abi.h"
#include "susfs_log.h"
#include "spoof_cmdline.h"
#include "spoof_cmdline_data.h"
#include "spoof_cmdline_phys.h"
#include "spoof_cmdline_policy.h"
#include "susfs_helpers.h"

/* The supercall's field is 8192 wide, but this insmod parameter cannot be:
 * module_param_string()'s value goes through a sysfs attribute and a sysfs
 * write is capped at one page, so the parameter tops out at 4095 bytes where
 * the supercall accepts 8191. */
module_param_string(bootconfig, spoof_cmdline_param,
		    sizeof(spoof_cmdline_param), 0644);

int susfs_spoof_cmdline_init(void)
{
	int rc;

	spoof_cmdline_data_init();

	if (!spoof_cmdline_param[0]) {
		SUSFS_LOGI("spoof_cmdline: no fake bootconfig, not armed\n");
		return 0;
	}
	rc = spoof_cmdline_set(spoof_cmdline_param);
	if (rc) {
		pr_err("spoof_cmdline: set failed %d, not armed\n", rc);
		return rc;
	}
	SUSFS_LOGI("spoof_cmdline armed: %s\n", spoof_cmdline_param);
	return 0;
}

void susfs_spoof_cmdline_exit(void)
{
	spoof_cmdline_restore();
}

/* supercall: CMD_SUSFS_SET_CMDLINE_OR_BOOTCONFIG */
void susfs_spoof_cmdline_supercall(void __user **arg)
{
	struct st_susfs_spoof_cmdline_or_bootconfig *info;
	int err;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		/* The kprobe has already claimed the syscall and answered 0, so returning silently leaves the
		 * caller's pre-seeded 126 (ERR_CMD_NOT_SUPPORTED) in place: the C tool then reports "please
		 * enable SUSFS in kernel" for a command this kernel implements, and ksud's err==126 check turns
		 * it into a silent success.  Upstream writes -ENOMEM here (fs/susfs.c:707-713); task_work
		 * context, so the writeback is safe. */
		err = -ENOMEM;
		if (copy_to_user(&((struct st_susfs_spoof_cmdline_or_bootconfig __user *)*arg)->err,
				 &err, sizeof(err)))
			pr_warn("cmdline supercall copy_to_user failed\n");
		pr_warn_ratelimited("spoof_cmdline: kzalloc failed, reported -ENOMEM\n");
		return;
	}

	if (copy_from_user(info, (void __user *)*arg, sizeof(*info))) {
		info->err = -EFAULT;
		goto out;
	}

	/* Empty string is rejected upstream (-EINVAL); report the real result instead of always claiming success. */
	if (!info->fake_cmdline_or_bootconfig[0]) {
		info->err = -EINVAL;
		goto out;
	}
	/* spoof_cmdline_set() kstrdup()s this, i.e. strlen()s it: an unterminated fixed-size ABI field would be read past the end. */
	if (!susfs_abi_path_ok(info->fake_cmdline_or_bootconfig,
			       sizeof(info->fake_cmdline_or_bootconfig))) {
		info->err = -ENAMETOOLONG;
		goto out;
	}

	info->err = spoof_cmdline_set(info->fake_cmdline_or_bootconfig);
	if (!info->err)
		SUSFS_LOGI("spoof_cmdline: set fake bootconfig (supercall)\n");
out:
	/* upstream writes back only ->err for input-type commands */
	if (copy_to_user(&((struct st_susfs_spoof_cmdline_or_bootconfig __user *)*arg)->err,
			 &info->err, sizeof(info->err)))
		pr_warn("cmdline supercall copy_to_user failed\n");
	kfree(info);
}
