// SPDX-License-Identifier: GPL-2.0
/* uname entry adapter: decode, errno and the original syscall only. */
#include <linux/errno.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <asm/ptrace.h>
#include "susfs_abi.h"
#include "susfs_log.h"
#include "susfs_hooks.h"
#include "susfs_uname.h"
#include "susfs_uname_data.h"
#include "susfs_uname_policy.h"
#include "susfs_uname_phys.h"

long __visible __nocfi noinline
susfs_uname_wrap(const struct pt_regs *regs)
{
    susfs_syscall_fn_t orig = susfs_uname_orig_get();
    struct susfs_uname_values values;
    void __user *name;
    long ret;

    if (!orig)
        return -ENOSYS;
    ret = orig(regs);
    if (ret)
        return ret;
    /* Inline receives the syscall wrapper's argument directly. */
    name = (void __user *)regs->regs[0];
    if (susfs_uname_snapshot(ret, !!name, &values))
        susfs_uname_write_user(name, &values, false);
    return ret;
}

/* Called by susfs_uname_stub (susfs_lookup_stub.S) after the original
 * __arm64_sys_newuname returned: saved[0] is the syscall's pt_regs argument,
 * saved[20] the real return value.  Same rewrite the kretprobe path does; the
 * stub exists because the entry has a PAC prologue, which the C wrapper form
 * cannot hook. */
long __nocfi susfs_uname_decide(unsigned long *saved)
{
    const struct pt_regs *regs = (const struct pt_regs *)saved[0];
    long ret = (long)saved[20];
    struct susfs_uname_values values;
    void __user *name;

    if (ret)
        return ret;
    name = regs ? (void __user *)regs->regs[0] : NULL;
    if (susfs_uname_snapshot(ret, !!name, &values))
        susfs_uname_write_user(name, &values, false);
    return ret;
}

static struct susfs_hook uname_hook = {
    .owner = "uname",
    .name = "newuname",
    .target = SUSFS_HOOK_TARGET_ARM64_SYS_NEWUNAME,
    .probes = &susfs_uname_probe,
    .nprobes = 1,
};

int susfs_uname_init(void)
{
    susfs_uname_policy_init(&uname_hook);
    return 0;
}

void susfs_uname_exit(void)
{
    susfs_uname_policy_exit(&uname_hook);
}

void susfs_uname_supercall(void __user **arg)
{
    struct st_susfs_uname info = {0};
    struct susfs_uname_values input;

    if (copy_from_user(&info, (void __user *)*arg, sizeof(info))) {
        info.err = -EFAULT;
        goto out;
    }
    memcpy(input.release, info.release, sizeof(input.release));
    memcpy(input.version, info.version, sizeof(input.version));
    switch (susfs_uname_validate(&input)) {
    case SUSFS_UNAME_EMPTY:
        info.err = -EINVAL;
        goto out;
    case SUSFS_UNAME_UNTERMINATED:
        info.err = -ENAMETOOLONG;
        goto out;
    case SUSFS_UNAME_VALID:
        break;
    }
    info.err = susfs_uname_set(&uname_hook, &input);
out:
    /* Input commands write only err, never the payload fields. */
    if (copy_to_user(&((struct st_susfs_uname __user *)*arg)->err,
                     &info.err, sizeof(info.err)))
        pr_warn("uname supercall copy_to_user failed\n");
}
