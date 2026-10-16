// SPDX-License-Identifier: GPL-2.0
/*
 * susfs_uname.c - spoof uname release/version (SUSFS SPOOF_UNAME).  Upstream patches the body of
 * SYSCALL_DEFINE1(newuname) between the memcpy(utsname) and copy_to_user; the LKM equivalent hooks
 * __arm64_sys_newuname with a kretprobe and rewrites the release/version fields of the user buffer just before
 * the syscall returns - the target pages were just faulted in by the original copy_to_user, so the handler's
 * copy_to_user cannot fault, and "default" copies the device's CURRENT utsname()->release/version at runtime.
 * Upstream semantics: OFF by default, enabled by CMD_SUSFS_SET_UNAME alone, kretprobe registered lazily.
 */
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/utsname.h>
#include <linux/string.h>
#include <linux/mutex.h>	/* uname_ctl_lock: serialises the arm/disarm pair */
#include <linux/version.h>
#include "susfs_abi.h"
#include "susfs_log.h"
#include "susfs.h"	/* susfs_abi_path_ok */
#include "susfs_kprobe.h"

#define SUSFS_UNAME_LEN (__NEW_UTS_LEN + 1)

static char fake_release[SUSFS_UNAME_LEN];
static char fake_version[SUSFS_UNAME_LEN];
static bool uname_spoof_enabled;

/* optional insmod-time override; supercall is the primary interface */
module_param_string(release, fake_release, sizeof(fake_release), 0644);
module_param_string(version, fake_version, sizeof(fake_version), 0644);
module_param(uname_spoof_enabled, bool, 0644);
MODULE_PARM_DESC(release, "fake uname release (insmod override)");
MODULE_PARM_DESC(version, "fake uname version (insmod override)");
MODULE_PARM_DESC(uname_spoof_enabled, "enable uname spoofing (insmod override)");

struct uname_args {
    struct new_utsname __user *name;
};

static int kr_newuname_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct uname_args *a = (struct uname_args *)ri->data;
    struct pt_regs *user = (struct pt_regs *)susfs_sc_arg(regs, 0);

    /* this GKI kernel does NOT auto-adjust syscall-wrapper probe regs:
     * susfs_sc_arg(regs, 0) is the struct pt_regs* argument, not the user arg */
    if (!user)
        return 1;   /* no return handler: ri->data would be left stale */

    a->name = (struct new_utsname __user *)susfs_sc_arg(user, 0);
    return 0;
}

static int kr_newuname_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct uname_args *a = (struct uname_args *)ri->data;

    if (regs_return_value(regs) != 0)
        return 0;
    if (!uname_spoof_enabled || !a->name)
        return 0;

    if (copy_to_user((char __user *)a->name + offsetof(struct new_utsname, release),
                     fake_release, SUSFS_UNAME_LEN))
        return 0;
    if (copy_to_user((char __user *)a->name + offsetof(struct new_utsname, version),
                     fake_version, SUSFS_UNAME_LEN))
        return 0;
    return 0;
}

static struct kretprobe krp = {
    .kp.symbol_name = SUSFS_SC_WRAPPER_NAME(newuname),
    .entry_handler = kr_newuname_entry,
    .handler = kr_newuname_ret,
    .data_size = sizeof(struct uname_args),
    .maxactive = 32,
};

static bool uname_registered;

/* Serialises the arm/disarm pair - the full reasoning is in susfs_avc_spoof.c's avc_ctl_lock:
 * register_kprobe() answers -EINVAL once another task has resolved .addr, and the failure branch
 * below clears .addr - which, without this lock, disarms a probe that is LIVE without ever
 * unregistering it, leaving its breakpoint installed after the module is freed.  Two tasks can
 * send CMD_SUSFS_SET_UNAME at the same time (each supercall runs in its own task_work). */
static DEFINE_MUTEX(uname_ctl_lock);

static int uname_register(void)
{
    int rc;

    mutex_lock(&uname_ctl_lock);
    if (uname_registered) {
        mutex_unlock(&uname_ctl_lock);
        return 0;
    }
    rc = register_kretprobe(&krp);
    if (rc) {
        /* CMD_SUSFS_SET_UNAME can be sent again after a failure, and the address the kernel
         * resolved is already in the struct: without this the retry is refused with -EINVAL
         * and the spoof can never be turned on - see susfs_kprobe.h.  Safe under the lock. */
        susfs_krp_forget_addr(&krp);
        mutex_unlock(&uname_ctl_lock);
        return rc;
    }
    uname_registered = true;
    mutex_unlock(&uname_ctl_lock);
    SUSFS_LOGI("uname spoof armed: release=%s version=%s\n",
            fake_release, fake_version);
    return 0;
}

static void uname_unregister(void)
{
    mutex_lock(&uname_ctl_lock);
    if (!uname_registered) {
        mutex_unlock(&uname_ctl_lock);
        return;
    }
    unregister_kretprobe(&krp);
    susfs_krp_forget_addr(&krp);
    uname_registered = false;
    mutex_unlock(&uname_ctl_lock);
}

int susfs_uname_init(void)
{
    if (uname_spoof_enabled && fake_release[0] && fake_version[0]) {
        int rc = uname_register();

        if (rc)
            pr_warn("register_kretprobe(newuname) failed %d\n", rc);
    } else {
        SUSFS_LOGI("uname spoof: disabled (enable via CMD_SUSFS_SET_UNAME)\n");
    }
    return 0;
}

void susfs_uname_exit(void)
{
    uname_unregister();
}

/* supercall: CMD_SUSFS_SET_UNAME */
void susfs_uname_supercall(void __user **arg)
{
    struct st_susfs_uname info = {0};
    int rc;

    if (copy_from_user(&info, (void __user *)*arg, sizeof(info))) {
        info.err = -EFAULT;
        goto out;
    }
    if (*info.release == '\0' || *info.version == '\0') {
        /* -EINVAL: the buffer came across fine, the VALUE is what is wrong. */
        info.err = -EINVAL;
        goto out;
    }
    /* char[65] ABI fields the caller need not terminate; strcmp() and strscpy() below would run past the end. */
    if (!susfs_abi_path_ok(info.release, sizeof(info.release)) ||
        !susfs_abi_path_ok(info.version, sizeof(info.version))) {
        info.err = -ENAMETOOLONG;
        goto out;
    }

    /* "default" copies the device's CURRENT uname at runtime */
    if (!strcmp(info.release, "default"))
        strscpy(fake_release, utsname()->release, sizeof(fake_release));
    else
        strscpy(fake_release, info.release, sizeof(fake_release));
    if (!strcmp(info.version, "default"))
        strscpy(fake_version, utsname()->version, sizeof(fake_version));
    else
        strscpy(fake_version, info.version, sizeof(fake_version));

    uname_spoof_enabled = true;
    rc = uname_register();
    if (rc) {
        info.err = rc;
        goto out;
    }
    info.err = 0;
    SUSFS_LOGI("uname spoof set: release=%s version=%s\n", fake_release, fake_version);
out:
    /* upstream writes back only ->err for input-type commands */
    if (copy_to_user(&((struct st_susfs_uname __user *)*arg)->err,
                     &info.err, sizeof(info.err)))
        pr_warn("uname supercall copy_to_user failed\n");
}
