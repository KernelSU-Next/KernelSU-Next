// SPDX-License-Identifier: GPL-2.0-only
/*
 * susfs_priority_hooks.c - syscall face of priority_uid_filter.
 *
 * Upstream 2.4.0 answers -EINVAL in four syscalls when the caller is an app
 * (uid % 100000 >= 10000) and the target uid is in the filter bitmap:
 *
 *   kernel/sys.c   setpriority(PRIO_USER, uid, ...) / getpriority(PRIO_USER, uid)
 *   block/ioprio.c ioprio_set(IOPRIO_WHO_USER, uid, ...) / ioprio_get(...)
 *
 * Same entries here, replaced in sys_call_table through the shared FP layer.
 * Process/pgroup targets carry a pid, not a uid, and keep the kernel's answer -
 * exactly the branches upstream leaves alone.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/ioprio.h>

#include <asm/unistd.h>

#include "susfs_fp_hook.h"
#include "susfs_hooks.h"
#include "susfs_priority_uid.h"
#include "susfs_log.h"

#ifndef PRIO_USER
#define PRIO_USER 2
#endif
#ifndef IOPRIO_WHO_USER
#define IOPRIO_WHO_USER 3
#endif

enum priority_syscall {
	PRIORITY_SETPRIORITY,
	PRIORITY_GETPRIORITY,
	PRIORITY_IOPRIO_SET,
	PRIORITY_IOPRIO_GET,
	PRIORITY_SYSCALL_COUNT,
};

static susfs_syscall_fn_t priority_orig_slot(enum priority_syscall id)
{
	static susfs_syscall_fn_t slots[PRIORITY_SYSCALL_COUNT];

	return &slots[id];
}

static bool priority_target_filtered(int which, unsigned long who, int user_which)
{
	return which == user_which && current_uid().val >= 10000 &&
	       susfs_priority_uid_filtered((uid_t)who);
}

static __nocfi noinline long priority_wrap_setpriority(const struct pt_regs *regs)
{
	susfs_syscall_fn_t orig = READ_ONCE(*priority_orig_slot(PRIORITY_SETPRIORITY));

	if (!orig)
		return -ENOSYS;
	if (priority_target_filtered((int)regs->regs[0], regs->regs[1], PRIO_USER))
		return -EINVAL;
	return orig(regs);
}

static __nocfi noinline long priority_wrap_getpriority(const struct pt_regs *regs)
{
	susfs_syscall_fn_t orig = READ_ONCE(*priority_orig_slot(PRIORITY_GETPRIORITY));

	if (!orig)
		return -ENOSYS;
	if (priority_target_filtered((int)regs->regs[0], regs->regs[1], PRIO_USER))
		return -EINVAL;
	return orig(regs);
}

static __nocfi noinline long priority_wrap_ioprio_set(const struct pt_regs *regs)
{
	susfs_syscall_fn_t orig = READ_ONCE(*priority_orig_slot(PRIORITY_IOPRIO_SET));

	if (!orig)
		return -ENOSYS;
	if (priority_target_filtered((int)regs->regs[0], regs->regs[1], IOPRIO_WHO_USER))
		return -EINVAL;
	return orig(regs);
}

static __nocfi noinline long priority_wrap_ioprio_get(const struct pt_regs *regs)
{
	susfs_syscall_fn_t orig = READ_ONCE(*priority_orig_slot(PRIORITY_IOPRIO_GET));

	if (!orig)
		return -ENOSYS;
	if (priority_target_filtered((int)regs->regs[0], regs->regs[1], IOPRIO_WHO_USER))
		return -EINVAL;
	return orig(regs);
}

static struct susfs_fp_hook priority_hooks[] = {
	{ .nr = __NR_setpriority, .name = "priority/setpriority",
	  .sym = "__arm64_sys_setpriority", .wrapper = priority_wrap_setpriority,
	  .orig_slot = &priority_orig_slot(PRIORITY_SETPRIORITY)[0] },
	{ .nr = __NR_getpriority, .name = "priority/getpriority",
	  .sym = "__arm64_sys_getpriority", .wrapper = priority_wrap_getpriority,
	  .orig_slot = &priority_orig_slot(PRIORITY_GETPRIORITY)[0] },
	{ .nr = __NR_ioprio_set, .name = "priority/ioprio_set",
	  .sym = "__arm64_sys_ioprio_set", .wrapper = priority_wrap_ioprio_set,
	  .orig_slot = &priority_orig_slot(PRIORITY_IOPRIO_SET)[0] },
	{ .nr = __NR_ioprio_get, .name = "priority/ioprio_get",
	  .sym = "__arm64_sys_ioprio_get", .wrapper = priority_wrap_ioprio_get,
	  .orig_slot = &priority_orig_slot(PRIORITY_IOPRIO_GET)[0] },
};

static struct susfs_hook priority_reg[ARRAY_SIZE(priority_hooks)];

int susfs_priority_hooks_init(void)
{
	int i, installed = 0;

	for (i = 0; i < (int)ARRAY_SIZE(priority_hooks); i++) {
		priority_reg[i].owner = "priority";
		priority_reg[i].name = priority_hooks[i].name;
		priority_reg[i].target = SUSFS_HOOK_TARGET_FP;
		priority_reg[i].fp = &priority_hooks[i];
		if (susfs_hook_register(&priority_reg[i]) < 0) {
			pr_warn("priority: %s not installed - that syscall answers the kernel's own way\n",
				priority_hooks[i].name);
			continue;
		}
		installed++;
	}
	if (!installed)
		return -ENOSYS;
	SUSFS_LOGI("priority: %d/%zu syscall hooks armed\n",
		   installed, ARRAY_SIZE(priority_hooks));
	return 0;
}

void susfs_priority_hooks_exit(void)
{
	int i;

	for (i = 0; i < (int)ARRAY_SIZE(priority_hooks); i++)
		if (priority_reg[i].backend != SUSFS_HOOK_BACKEND_NONE)
			susfs_hook_unregister(&priority_reg[i]);
}
