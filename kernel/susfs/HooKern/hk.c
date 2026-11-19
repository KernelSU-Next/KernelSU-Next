// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "hk.h"
#include "hk_ptr.h"
#include "hk_kprobe.h"
#include "hk_kretprobe.h"
#include "hk_redir.h"
#include "hk_sighook.h"
#include "hk_inline.h"
#include "hk_patch.h"

static struct hk_cfg g_cfg;
static bool g_exiting;

/*
 * the body of a function whose address was taken is named name$type on a clang cfi
 * build, and the suffix is not something a caller can know. the walk that used to
 * find it goes through the kernel's symbol iterator, whose callback is four
 * arguments on one kernel and three on another, and passing the wrong one is a cfi
 * failure rather than a wrong answer. the stub the plain symbol names is a branch to
 * that body, so the body is reached by following it, which costs no callback and no
 * allocation and therefore also works from a probe handler
 */
#define HK_STUB_FOLLOW 8

static u32 hk_read_insn(unsigned long addr)
{
	u32 insn = 0;

	if (copy_from_kernel_nofault(&insn, (const void *)addr, sizeof(insn)))
		return 0;
	return insn;
}

static unsigned long __nocfi hk_follow_stub(unsigned long addr)
{
	int i;

	for (i = 0; i < HK_STUB_FOLLOW; i++) {
		u32 insn = hk_read_insn(addr);
		long disp;

		if (!insn)
			return 0;
		if (hk_insn_is_bti(insn)) {
			addr += 4;
			continue;
		}
		/* a bare branch at the entry is the stub the cfi build leaves behind */
		if ((insn & 0xFC000000) != 0x14000000)
			return addr;
		disp = (long)(insn & 0x03FFFFFF) << 2;
		if (disp & (1L << 27))
			disp |= ~((1L << 28) - 1);
		addr += disp;
	}
	return 0;
}

static unsigned long __nocfi hk_resolve_cfi(const char *name)
{
	unsigned long addr = 0;

	/* asking hk_resolve here would ask this function again */
	if (g_cfg.resolve && !g_exiting)
		addr = g_cfg.resolve(name);
	if (!addr || !hk_ker_addr_ok(addr))
		return 0;
	return hk_follow_stub(addr);
}

__nocfi noinline unsigned long hk_resolve(const char *name)
{
	unsigned long addr;

	if (!name)
		return 0;
	if (g_cfg.resolve && !g_exiting) {
		addr = g_cfg.resolve(name);
		if (addr && hk_ker_addr_ok(addr))
			return addr;
	}
	addr = hk_resolve_cfi(name);
	if (addr && hk_ker_addr_ok(addr))
		return addr;
	return 0;
}

/* a compiler writes at most a bti and a pac prologue, so four words cover every
 * entry; an entry whose first four words are all pads has no body behind them */
#define HK_PAD_MAX 4

int hk_kprobe_adjust_pad(unsigned long addr, void *arg, unsigned long *out)
{
	unsigned int i;

	(void)arg;
	if (!addr || !out)
		return -EINVAL;
	for (i = 0; i < HK_PAD_MAX; i++) {
		u32 insn;

		if (copy_from_kernel_nofault(&insn, (const void *)addr,
					     sizeof(insn)))
			return -EFAULT;
		if (!hk_insn_is_pad(insn))
			break;
		addr += 4;
	}
	if (i == HK_PAD_MAX)
		return -EOPNOTSUPP;
	*out = addr;
	return 0;
}

int hk_init(const struct hk_cfg *cfg)
{
	if (!cfg || !cfg->resolve)
		return -EINVAL;
	if (g_cfg.resolve && !g_exiting)
		return -EALREADY;

	g_cfg = *cfg;
	g_exiting = false;
	hk_patch_set_write(cfg->write);
	/*
	 * resolve the write path symbols here and not on first use: a lazy resolve
	 * inside stop_machine would walk kallsyms from an atomic context
	 */
	hk_patch_init();
	pr_info("[lkmhook] init\n");
	return 0;
}

typedef void (*hk_synchronize_rcu_tasks_fn)(void);

/*
 * __nocfi on the body, not decoration: the symbol is resolved at runtime and the
 * call is indirect, and the cfi typeid the compiler would check it against is not
 * the one the kernel recorded. 5.10 panics on it, the target named in the message
 */
__nocfi /*
 * the default notice is a warning, the level this library uses for a condition it
 * refused and carried on from: the mistake belongs to the caller of the hook, not
 * to the kernel, and a caller that wants it louder escalates in its own sink
 */
static void hk_notice_print(enum hk_notice what, const char *detail)
{
	static const char *const names[] = {
		"free before disable",
		"relo table",
	};

	pr_warn("[lkmhook] %s: %s\n",
	       what < ARRAY_SIZE(names) ? names[what] : "notice",
	       detail ? detail : "");
}

static hk_notify_fn g_notify = hk_notice_print;

void hk_notice_set(hk_notify_fn fn)
{
	WRITE_ONCE(g_notify, fn);
}

void __nocfi hk_notice(enum hk_notice what, const char *detail)
{
	hk_notify_fn fn = READ_ONCE(g_notify);

	if (fn)
		fn(what, detail);
}

/*
 * the call inside is indirect and cfi is off for it, and noinline is the other
 * half of that: an inline would move the call into whatever caller lto picked, and
 * the check __nocfi turned off would come back with it
 */
__nocfi noinline void hk_wait_tasks_rcu(void)
{
	hk_synchronize_rcu_tasks_fn fn;
	unsigned long sym = hk_resolve("synchronize_rcu_tasks");

	if (!sym || !hk_ker_addr_ok(sym)) {
		pr_warn_once("[lkmhook] synchronize_rcu_tasks not found, "
			     "a freed trampoline may still be running\n");
		return;
	}
	fn = (hk_synchronize_rcu_tasks_fn)sym;
	fn();
}

void hk_exit(void)
{
	/*
	 * called by hk_exit_block in a loop, so a restore that failed is retried.
	 * the flag keeps the injected resolver out of the retry, a consumer that
	 * tears its resolver down before hk_exit_block would otherwise resolve
	 * through a freed pointer, and it is what makes a clean shutdown
	 * initialize again instead of -EALREADY
	 */
	g_exiting = true;
	hk_sighook_exit();
	hk_kprobe_exit();
	hk_kretprobe_exit();
	hk_ptr_exit();
	hk_inline_exit();
	pr_info("[lkmhook] exit\n");
}

/*
 * rmmod unmaps module text and data as soon as the exit path returns, and the
 * entry of a hook that could not be restored still branches into the trampoline
 * allocated here. the module ends its exit path through this instead of
 * returning to its caller while a hook is still live: the detour keeps its
 * bytes, so the kernel stays correct, and the rmmod request is the only thing
 * that is refused. it never returns
 */
void hk_exit_block(void)
{
	unsigned int left;

	for (;;) {
		/*
		 * the calls inside a replacement are counted here too: their code is
		 * this module's, and the reference they hold is what keeps the kernel
		 * from unmapping it, so a clean exit waits for them as well
		 */
		left = hk_inline_pending() + hk_ptr_pending() +
		       hk_kprobe_pending() + hk_kretprobe_pending() +
		       hk_redir_pending() + hk_redir_inflight();
		if (!left)
			break;
		pr_warn("[lkmhook] exit blocked, %u hook(s) still live\n", left);
		ssleep(60);
		hk_inline_exit();
		hk_ptr_exit();
		hk_kretprobe_exit();
	}
	pr_info("[lkmhook] exit clean\n");
	g_exiting = false;
}
