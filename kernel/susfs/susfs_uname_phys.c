/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/utsname.h>
#include <linux/string.h>
#include "susfs_hooks.h"
#include "susfs_uname_data.h"
#include "susfs_uname_policy.h"
#include "susfs_uname_phys.h"

/* Parameter transport is physical; names, modes and storage semantics are unchanged. */
module_param_string(release, susfs_uname_state_values.release, SUSFS_UNAME_LEN, 0644);
module_param_string(version, susfs_uname_state_values.version, SUSFS_UNAME_LEN, 0644);
module_param_named(uname_spoof_enabled, susfs_uname_enabled, bool, 0644);
MODULE_PARM_DESC(release, "fake uname release (insmod override)");
MODULE_PARM_DESC(version, "fake uname version (insmod override)");
MODULE_PARM_DESC(uname_spoof_enabled, "enable uname spoofing (insmod override)");

void susfs_uname_read_current(struct susfs_uname_values *out)
{
	BUILD_BUG_ON(SUSFS_UNAME_LEN != sizeof(utsname()->release));
	BUILD_BUG_ON(SUSFS_UNAME_LEN != sizeof(utsname()->version));
	strscpy_pad(out->release, utsname()->release, SUSFS_UNAME_LEN);
	strscpy_pad(out->version, utsname()->version, SUSFS_UNAME_LEN);
}

void susfs_uname_write_user(void __user *name, const struct susfs_uname_values *values,
			    bool stop_on_fault)
{
	unsigned long uncopied;

	uncopied = copy_to_user((char __user *)name + offsetof(struct new_utsname, release),
			       values->release, SUSFS_UNAME_LEN);
	if (uncopied && stop_on_fault)
		return;
	(void)copy_to_user((char __user *)name + offsetof(struct new_utsname, version),
			   values->version, SUSFS_UNAME_LEN);
}

struct uname_args {
	struct new_utsname __user *name;
};

static int kr_newuname_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct uname_args *a = (struct uname_args *)ri->data;
	struct pt_regs *user = (struct pt_regs *)regs->regs[0];

	/* Probe registers wrap the syscall's pt_regs argument, one extra level. */
	if (!user)
		return 1;
	a->name = (struct new_utsname __user *)user->regs[0];
	return 0;
}

static int kr_newuname_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct uname_args *a = (struct uname_args *)ri->data;
	struct susfs_uname_values values;

	if (susfs_uname_snapshot(regs_return_value(regs), !!a->name, &values))
		susfs_uname_write_user(a->name, &values, true);
	return 0;
}

static struct kretprobe krp = {
	.kp.symbol_name = "__arm64_sys_newuname",
	.entry_handler = kr_newuname_entry,
	.handler = kr_newuname_ret,
	.data_size = sizeof(struct uname_args),
	.maxactive = 32,
};

/* Registration stays in the centre; this file supplies the raw probe only. */
const struct susfs_hook_probe susfs_uname_probe = { .kr = &krp };
