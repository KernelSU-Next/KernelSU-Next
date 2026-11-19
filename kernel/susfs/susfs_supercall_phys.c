/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_supercall_phys.c - reboot(2) entry hooks and task_work delivery (P layer)
 *
 * ksu_susfs/ksud call syscall(SYS_reboot, 0xDEADBEEF, 0xFAFAFAFA, cmd, &payload).
 * A pre_handler runs in interrupt context and must not copy_from_user, so the
 * command is deferred to task_work (TWA_RESUME), i.e. process context.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/task_work.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/cred.h>
#include <linux/syscalls.h>

#include "susfs_abi.h"
#include "susfs_log.h"
#include "susfs_supercall_data.h"
#include "susfs_supercall_phys.h"
#include "susfs_supercall_policy.h"

struct susfs_tw {
	struct callback_head cb;
	unsigned int cmd;
	void __user *payload;   /* points at the userspace payload struct */
};

static void susfs_tw_func(struct callback_head *cb)
{
	struct susfs_tw *tw = container_of(cb, struct susfs_tw, cb);
	void __user *arg = tw->payload;

	susfs_supercall_dispatch(tw->cmd, &arg);
	kfree(tw);

	/* Pairs with the try_module_get() in the entry hook: the callback runs
	 * from module .text, after the syscall has already returned to userspace. */
	module_put(THIS_MODULE);
}

int susfs_supercall_defer(unsigned int cmd, void __user *payload)
{
	struct susfs_tw *tw;

	/* The callback runs from module .text when this task next returns to userspace.  Pin the
	 * module until it has run: the syscall table entry is restored before that on rmmod. */
	if (!try_module_get(THIS_MODULE))
		return -EAGAIN;

	tw = kzalloc(sizeof(*tw), GFP_ATOMIC);
	if (!tw) {
		module_put(THIS_MODULE);
		return -ENOMEM;
	}
	tw->cmd = cmd;
	tw->payload = payload;
	tw->cb.func = susfs_tw_func;

	if (task_work_add(current, &tw->cb, TWA_RESUME)) {
		kfree(tw);
		module_put(THIS_MODULE);
		pr_warn("susfs supercall: task_work_add failed\n");
		return -EAGAIN;
	}
	return 0;
}

static long __nocfi reboot_wrapper(const struct pt_regs *regs)
{
	int magic1, magic2;
	unsigned int cmd;
	void __user *payload;

	magic1 = (int)regs->regs[0];
	magic2 = (int)regs->regs[1];
	cmd = (unsigned int)regs->regs[2];
	payload = (void __user *)regs->regs[3];

	if (magic1 != KSU_INSTALL_MAGIC1)
		goto orig;
	if (magic2 != SUSFS_MAGIC)
		goto orig;
	if (current_uid().val != 0)
		goto orig;

	if (!susfs_supercall_cmd_handled(cmd)) {
		SUSFS_LOGI("susfs supercall: unsupported cmd 0x%x\n", cmd);
		goto orig;
	}

	if (susfs_supercall_defer(cmd, payload))
		goto orig;

	/* A handled supercall returns 0 to userspace; the command itself runs from task_work. */
	return 0;

orig:
	return susfs_reboot_orig ? susfs_reboot_orig(regs) : -EINVAL;
}

struct susfs_fp_hook susfs_supercall_reboot_fp = {
	.nr = __NR_reboot,
	.nr_compat = 0,
	.name = "supercall/reboot",
	.sym = "__arm64_sys_reboot",
	.wrapper = reboot_wrapper,
	.orig_slot = &susfs_reboot_orig,
};

/* Fallback delivery for kernels whose sys_call_table cannot be hooked: on arm64 GKI under pKVM the
 * table page is EL2-protected and the FP backend above finds no landing pad.  This is the
 * pre-792ac97 kprobe path, still what sus_map and sus_mount use. */
static int reboot_pre(struct kprobe *kp, struct pt_regs *regs)
{
	struct pt_regs *real_regs = (struct pt_regs *)regs->regs[0];
	int magic1, magic2;
	unsigned int cmd;
	void __user *payload;

	/* A prober runs before the callee, so an argument the callee would have checked is
	 * still raw here (see the filename_lookup lesson in sus_path.c). */
	if (!real_regs)
		return 0;

	magic1 = (int)real_regs->regs[0];
	magic2 = (int)real_regs->regs[1];
	cmd = (unsigned int)real_regs->regs[2];
	payload = (void __user *)real_regs->regs[3];

	if (magic1 != KSU_INSTALL_MAGIC1)
		return 0;
	if (magic2 != SUSFS_MAGIC)
		return 0;
	if (current_uid().val != 0)
		return 0;

	/* Not ours to answer: leave the syscall alone so reboot(2) reports the -EINVAL
	 * upstream reports and payload.err keeps the caller's value. */
	if (!susfs_supercall_cmd_handled(cmd)) {
		SUSFS_LOGI("susfs supercall: unsupported cmd 0x%x\n", cmd);
		return 0;
	}

	if (susfs_supercall_defer(cmd, payload))
		return 0;

	/* Upstream's reboot.c patch makes a handled supercall return 0 instead of falling
	 * through to the real reboot path; mirror it from the kprobe. */
	regs->pc = regs->regs[30];
	regs->regs[0] = 0;
	return 1;
}

static struct kprobe reboot_kp = {
	.symbol_name = "__arm64_sys_reboot",
	.pre_handler = reboot_pre,
};

const struct susfs_hook_probe susfs_supercall_reboot_probe = { .kp = &reboot_kp };
