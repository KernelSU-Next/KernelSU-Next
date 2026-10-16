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
#include <linux/workqueue.h>	/* the delayed canary recheck */
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/cred.h>
#include <linux/syscalls.h>

#include "susfs_abi.h"
#include "susfs_kprobe.h"	/* susfs_sc_arg reads the real regs; the skip goes through susfs_kp_skip_return */
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

	magic1 = (int)susfs_sc_arg(regs, 0);
	magic2 = (int)susfs_sc_arg(regs, 1);
	cmd = (unsigned int)susfs_sc_arg(regs, 2);
	payload = (void __user *)susfs_sc_arg(regs, 3);

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
	.sym = SUSFS_SC_WRAPPER_NAME(reboot),
	.wrapper = reboot_wrapper,
	.orig_slot = &susfs_reboot_orig,
};

/* Set by the skip mechanism canary below; reboot_pre reads it. */
static bool sc_canary_ok;

/* Fallback delivery for kernels whose sys_call_table cannot be hooked: on arm64 GKI under pKVM the
 * table page is EL2-protected and the FP backend above finds no landing pad.  This is the
 * pre-792ac97 kprobe path, still what sus_map and sus_mount use. */
static int reboot_pre(struct kprobe *kp, struct pt_regs *regs)
{
	struct pt_regs *real_regs;
	int magic1, magic2;
	unsigned int cmd;
	void __user *payload;

	/* A failed delayed canary recheck disarms us here instead of letting
	 * the supercall silently stop answering. */
	if (!sc_canary_ok)
		return 0;
	real_regs = (struct pt_regs *)susfs_sc_arg(regs, 0);

	/* A prober runs before the callee, so an argument the callee would have checked is
	 * still raw here (see the filename_lookup lesson in sus_path.c). */
	if (!real_regs)
		return 0;

	magic1 = (int)susfs_sc_arg(real_regs, 0);
	magic2 = (int)susfs_sc_arg(real_regs, 1);
	cmd = (unsigned int)susfs_sc_arg(real_regs, 2);
	payload = (void __user *)susfs_sc_arg(real_regs, 3);

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
	susfs_kp_skip_return(regs, 0);
	return 1;
}
NOKPROBE_SYMBOL(reboot_pre);

static struct kprobe reboot_kp = {
	.symbol_name = SUSFS_SC_WRAPPER_NAME(reboot),
	.pre_handler = reboot_pre,
	.post_handler = susfs_kp_noopt_post,
};

const struct susfs_hook_probe susfs_supercall_reboot_probe = { .kp = &reboot_kp };

/* ---- skip mechanism canary ----
 * The supercall's contract with userspace rests on the pre_handler skip.
 * Prove it before relying on it: probe one of our own functions, force its
 * return through the same helper, and refuse the load if the mechanism does
 * not answer.  Re-checked from delayed work because kprobe jump optimization
 * arms asynchronously, about 5 jiffies after registration, and an optimized
 * probe ignores the ip write.  The noopt post_handler vetoes optimization;
 * the recheck is the proof the veto held. */
#define SUSFS_SC_CANARY_NATURAL 0x13579BDF
#define SUSFS_SC_CANARY_FORCED  0x2468ACE0

/* The natural value must live in a volatile.  Returning the constant
 * directly lets the compiler fold the canary call away entirely
 * (measured on the emulator: the int3 sat armed at fn+4 while the
 * "call" returned the constant without ever executing), and the check
 * would then always see the natural value and refuse every load. */
static volatile int susfs_sc_canary_natural = SUSFS_SC_CANARY_NATURAL;

static noinline int susfs_sc_canary_fn(void)
{
	return susfs_sc_canary_natural;
}

static int susfs_sc_canary_pre(struct kprobe *kp, struct pt_regs *regs)
{
	susfs_kp_skip_return(regs, SUSFS_SC_CANARY_FORCED);
	return 1;
}
NOKPROBE_SYMBOL(susfs_sc_canary_pre);

static struct kprobe canary_kp = {
	.pre_handler = susfs_sc_canary_pre,
	.post_handler = susfs_kp_noopt_post,
};

static int susfs_sc_canary_run(const char *when)
{
	int got = susfs_sc_canary_fn();

	if (got == SUSFS_SC_CANARY_FORCED)
		return 0;
	pr_err("susfs supercall: skip mechanism check FAILED at %s: canary returned 0x%x, expected 0x%x - the supercall would answer nothing, refusing this load\n",
	       when, got, SUSFS_SC_CANARY_FORCED);
	return -EINVAL;
}

static void susfs_sc_canary_recheck(struct work_struct *work)
{
	if (!sc_canary_ok)
		return;
	if (susfs_sc_canary_run("delayed recheck")) {
		/* reboot_pre now passes through: the magics reach the real
		 * reboot, which rejects them with -EINVAL and nothing else. */
		sc_canary_ok = false;
	}
}
static DECLARE_DELAYED_WORK(canary_dw, susfs_sc_canary_recheck);

int susfs_supercall_canary_arm(void)
{
	int rc;

	canary_kp.addr = (kprobe_opcode_t *)susfs_sc_canary_fn;
	rc = register_kprobe(&canary_kp);
	if (rc) {
		pr_warn("susfs supercall: register_kprobe(canary) failed %d\n", rc);
		susfs_kp_forget_addr(&canary_kp);
		return rc;
	}
	rc = susfs_sc_canary_run("init");
	if (rc) {
		unregister_kprobe(&canary_kp);
		susfs_kp_forget_addr(&canary_kp);
		return rc;
	}
	sc_canary_ok = true;
	schedule_delayed_work(&canary_dw, msecs_to_jiffies(1000));
	return 0;
}

void susfs_supercall_canary_disarm(void)
{
	sc_canary_ok = false;
	cancel_delayed_work_sync(&canary_dw);
	if (canary_kp.addr) {
		unregister_kprobe(&canary_kp);
		susfs_kp_forget_addr(&canary_kp);
	}
}
