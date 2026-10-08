// SPDX-License-Identifier: GPL-2.0
/*
 * spoof_cmdline.c - spoof /proc/bootconfig (SUSFS SPOOF_CMDLINE_OR_BOOTCONFIG).  boot_config_proc_show()
 * (fs/proc/bootconfig.c) does `if (saved_boot_config) seq_puts(m, saved_boot_config);`, so rewriting the static
 * pointer spoofs the whole file; that variable is a static BSS pointer resolved at load time by ksud insmod
 * (kallsyms relocation, like selinux_state in kstat) and the write is atomic, so concurrent seq reads are safe.
 * The fake string is heap-allocated (kstrdup): the supercall ABI accepts up to 8192 bytes, and the insmod
 * parameter is copied to the heap as well so a later supercall can free it safely.
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "susfs_abi.h"
#include "susfs_log.h"
#include "susfs.h"	/* susfs_abi_path_ok */

/* unexported static var; ksud insmod relocates it via kallsyms */
extern char *saved_boot_config;

/* The supercall's field is 8192 wide, but this insmod parameter cannot be: module_param_string()'s value goes through a
 * sysfs attribute and a sysfs write is capped at one page, so the parameter tops out at 4095 bytes where the supercall
 * accepts 8191. */
static char param_bootconfig[4096];
module_param_string(bootconfig, param_bootconfig, sizeof(param_bootconfig), 0644);

static char *orig_boot_config;
static char *fake_boot_config;   /* heap-allocated, currently published */
static bool spoof_active;

/* Strings that saved_boot_config used to point at.  A published string must NEVER be freed while saved_boot_config
 * might reach it: /proc/bootconfig is read via seq_puts with no lock of ours, so freeing the old buffer before
 * republishing let a reader touch freed memory, and a set() that then failed kstrdup left saved_boot_config dangling
 * at the buffer it had just freed.  Retiring instead costs one 8 KB string per update. */
struct retired_str {
	struct list_head list;
	char *s;
};

static LIST_HEAD(retired_strs);
/* Serialises publication and the retired list: two concurrent supercalls could otherwise
 * both take the !spoof_active branch and record a FAKE pointer as orig_boot_config, leaving
 * /proc/bootconfig spoofed after unload. */
static DEFINE_MUTEX(spoof_lock);

/* Free what nobody can still be reading.  Retiring without ever freeing leaked up to 8 KB
 * per set() for the module's lifetime, unbounded over repeated supercalls and unreachable
 * after unload. */
static void spoof_drain_retired(void)
{
	struct retired_str *r, *tmp;
	LIST_HEAD(doomed);

	mutex_lock(&spoof_lock);
	list_splice_init(&retired_strs, &doomed);
	mutex_unlock(&spoof_lock);

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
	list_add_tail(&r->list, &retired_strs);
}

/* Allocate first, publish second.  Returns 0 or a negative errno. */
static int spoof_set(const char *fake)
{
	char *dup;

	dup = kstrdup(fake, GFP_KERNEL);
	if (!dup)
		return -ENOMEM;

	mutex_lock(&spoof_lock);
	if (!spoof_active)
		orig_boot_config = saved_boot_config;
	else
		spoof_retire(fake_boot_config);

	fake_boot_config = dup;
	saved_boot_config = dup;
	spoof_active = true;
	mutex_unlock(&spoof_lock);

	/* Outside the lock: the wait is unbounded and only the list needs serialising. */
	spoof_drain_retired();
	return 0;
}

int susfs_spoof_cmdline_init(void)
{
	int rc;

	if (!param_bootconfig[0]) {
		SUSFS_LOGI("spoof_cmdline: no fake bootconfig, not armed\n");
		return 0;
	}
	rc = spoof_set(param_bootconfig);
	if (rc) {
		pr_err("spoof_cmdline: set failed %d, not armed\n", rc);
		return rc;
	}
	SUSFS_LOGI("spoof_cmdline armed: %s\n", param_bootconfig);
	return 0;
}

void susfs_spoof_cmdline_exit(void)
{
	char *live = NULL;

	mutex_lock(&spoof_lock);
	if (spoof_active) {
		saved_boot_config = orig_boot_config;
		spoof_active = false;
		orig_boot_config = NULL;
		live = fake_boot_config;
	}
	fake_boot_config = NULL;
	if (live)
		spoof_retire(live);
	mutex_unlock(&spoof_lock);

	/* The pointer is unpublished now, so wait out whoever may still hold it and then free
	 * everything this feature allocated - the live buffer included, which used to be
	 * dropped on the floor at unload along with the whole retired list. */
	spoof_drain_retired();
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
	/* spoof_set() kstrdup()s this, i.e. strlen()s it: an unterminated fixed-size ABI field would be read past the end. */
	if (!susfs_abi_path_ok(info->fake_cmdline_or_bootconfig,
			       sizeof(info->fake_cmdline_or_bootconfig))) {
		info->err = -ENAMETOOLONG;
		goto out;
	}

	info->err = spoof_set(info->fake_cmdline_or_bootconfig);
	if (!info->err)
		SUSFS_LOGI("spoof_cmdline: set fake bootconfig (supercall)\n");
out:
	/* upstream writes back only ->err for input-type commands */
	if (copy_to_user(&((struct st_susfs_spoof_cmdline_or_bootconfig __user *)*arg)->err,
			 &info->err, sizeof(info->err)))
		pr_warn("cmdline supercall copy_to_user failed\n");
	kfree(info);
}
