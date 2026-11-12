// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/xarray.h>

#include "hk.h"
#include "hk_inline.h"
#include "hk_kprobe.h"
#include "hk_kretprobe.h"
#include "hk_redir.h"

/* how long a remove waits for the calls inside a replacement to come back */
#define HK_REDIR_DRAIN_MS 100

/*
 * the calls inside a replacement and the reference they hold belong to the module
 * this library was built into, not to one hook: the module is what must not be
 * freed, and every hook of a module protects the same one
 */
static atomic_t g_redir_inflight = ATOMIC_INIT(0);
static unsigned int g_redir_stuck;

/*
 * resolved once, at install time and in process context: a kallsyms walk from a
 * breakpoint handler would be a walk on every redirected call
 */
typedef bool (*module_get_fn)(struct module *mod);
typedef void (*module_put_fn)(struct module *mod);

static module_get_fn g_module_get;
static module_put_fn g_module_put;

static int hk_redir_pin_resolve(void)
{
	if (!g_module_get)
		g_module_get = (module_get_fn)hk_resolve("try_module_get");
	if (!g_module_put)
		g_module_put = (module_put_fn)hk_resolve("module_put");
	if (!g_module_get || !g_module_put)
		return -ENODATA;
	return 0;
}

static __nocfi bool hk_redir_get_ref(void)
{
	if (!g_module_get || !THIS_MODULE)
		return false;
	return g_module_get(THIS_MODULE);
}

static __nocfi void hk_redir_put_ref(void)
{
	if (!g_module_put || !THIS_MODULE)
		return;
	g_module_put(THIS_MODULE);
}

/* the completion signal: the replacement returned, so one reference goes back */
static int hk_redir_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	(void)ri;
	(void)regs;
	atomic_dec(&g_redir_inflight);
	hk_redir_put_ref();
	return 0;
}

/*
 * the handler of every kprobe route hook. the probe is a member of the hook, so the
 * hook is what encloses it and one handler serves all of them
 */
static int hk_redir_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct hk_redir *r = container_of(p, struct hk_redir, u.kprobe.kp);
	bool pinned = false;

	if (!READ_ONCE(r->live))
		return 0;

	/*
	 * the reference comes before the marker is set: a task that cannot be pinned
	 * must not be marked as redirected either, or it would never be redirected
	 * again
	 */
	if (r->pin) {
		if (!hk_redir_get_ref())
			return 0;
		pinned = true;
	}

	if (r->guard == HK_REDIR_GUARD_CALLER) {
		/*
		 * the return address says who called: a call from the module the hook
		 * lives in is the replacement reaching for the original, which is what
		 * the original instruction has to run for
		 */
		if (within_module(regs->regs[30], THIS_MODULE)) {
			if (pinned)
				hk_redir_put_ref();
			return 0;
		}
	} else if (!hk_redir_guard_enter(r)) {
		if (pinned)
			hk_redir_put_ref();
		return 0;
	}

	if (pinned)
		atomic_inc(&g_redir_inflight);
	hk_regs_redirect(regs, READ_ONCE(r->repl));
	return 1;
}

bool hk_redir_guard_enter(struct hk_redir *r)
{
	void *old;

	if (!r || !READ_ONCE(r->live))
		return false;
	/* the caller marker keeps no state, so there is nothing to enter */
	if (r->guard != HK_REDIR_GUARD_TASK)
		return true;
	if (xa_load(&r->tasks, (unsigned long)current))
		return false;
	/*
	 * the node comes from atomic memory on purpose: the handler runs with the
	 * probed core parked, and a sleeping allocation here is the bug this replaces
	 */
	old = xa_store(&r->tasks, (unsigned long)current, r, GFP_ATOMIC);
	return !xa_is_err(old);
}

void hk_redir_leave(struct hk_redir *r)
{
	if (!r || r->guard != HK_REDIR_GUARD_TASK)
		return;
	xa_erase(&r->tasks, (unsigned long)current);
}

/*
 * wait for the calls that are already inside a replacement to come back. the probe
 * is gone by the time this runs, so no new call starts and the count can only
 * fall. a replacement that never returns is reported instead of being waited for
 */
static int hk_redir_drain(void)
{
	unsigned int i;

	for (i = 0; i < HK_REDIR_DRAIN_MS; i++) {
		if (!atomic_read(&g_redir_inflight))
			return 0;
		msleep(1);
	}
	return -EBUSY;
}

static void hk_redir_stuck(void)
{
	g_redir_stuck++;
}

int hk_redir_install_at(struct hk_redir *r, unsigned long addr,
			unsigned long repl, enum hk_redir_how how)
{
	enum hk_redir_guard guard;
	bool pin;
	int ret;

	if (!r || !addr || !repl)
		return -EINVAL;
	/* the text route writes an entry it has to read and relocate first */
	if (how != HK_REDIR_KPROBE)
		return -EOPNOTSUPP;
	if (r->guard != HK_REDIR_GUARD_TASK && r->guard != HK_REDIR_GUARD_CALLER)
		return -EINVAL;

	/* what the caller named before the call, everything else is set here */
	pin = r->pin;
	guard = r->guard;

	memset(r, 0, sizeof(*r));
	r->how = how;
	r->guard = guard;
	r->pin = pin;
	r->addr = addr;
	r->orig = addr;
	r->repl = repl;
	xa_init(&r->tasks);

	if (pin) {
		/*
		 * a pin that cannot be completed is refused, not dropped: a hook that
		 * redirects without holding the reference would let the module be freed
		 * under a call that is still running
		 */
		ret = hk_redir_pin_resolve();
		if (ret)
			return ret;
		/*
		 * the signal is armed before the redirect, so no call can be redirected
		 * without the reference that accounts for it
		 */
		ret = hk_kretprobe_install_at(&r->retprobe, repl, hk_redir_ret);
		if (ret)
			return ret;
	}

	/*
	 * live before the probe is armed: a call can be redirected the moment
	 * register_kprobe returns, and the handler has to see a hook it may enter
	 */
	r->live = true;
	ret = hk_kprobe_install_at(&r->u.kprobe, addr, hk_redir_pre);
	if (ret) {
		r->live = false;
		if (pin)
			hk_kretprobe_remove(&r->retprobe);
		xa_destroy(&r->tasks);
		return ret;
	}
	return 0;
}

int hk_redir_install(struct hk_redir *r, const char *sym, const char *repl_sym,
		     enum hk_redir_how how)
{
	unsigned long addr;
	unsigned long repl;
	int ret;

	if (!r || !sym || !repl_sym)
		return -EINVAL;

	if (how == HK_REDIR_TEXT) {
#ifdef HK_NO_INLINE
		/* the entry writer is not part of this build, so there is no text route */
		return -EOPNOTSUPP;
#else
		/* neither of the two is a mechanism the entry writer has */
		if (r->pin || r->guard != HK_REDIR_GUARD_TASK)
			return -EOPNOTSUPP;

		memset(r, 0, sizeof(*r));
		r->how = how;
		r->name = sym;
		r->repl = hk_resolve(repl_sym);
		if (!r->repl)
			return -ENOENT;
		ret = hk_inline_hook(&r->u.text, sym, repl_sym);
		if (ret)
			return ret;
		r->addr = r->u.text.addr;
		r->orig = r->u.text.orig;
		r->live = true;
		return 0;
#endif
	}

	addr = hk_resolve(sym);
	if (!addr)
		return -ENOENT;
	repl = hk_resolve(repl_sym);
	if (!repl)
		return -ENOENT;

	ret = hk_redir_install_at(r, addr, repl, how);
	if (ret)
		return ret;
	r->name = sym;
	return 0;
}

int hk_redir_remove(struct hk_redir *r)
{
	int ret;

	/* a hook that could not be taken back is retried, one that is gone is not */
	if (!r || (!r->live && !r->pending))
		return -EINVAL;

	/* from here on a breakpoint that fires runs the original */
	WRITE_ONCE(r->live, false);

	if (r->how == HK_REDIR_TEXT) {
#ifndef HK_NO_INLINE
		/* the disable, the grace period and the free, in that order */
		hk_inline_unhook(&r->u.text);
#endif
		return 0;
	}

	ret = hk_kprobe_remove(&r->u.kprobe);
	if (ret) {
		r->pending = true;
		hk_redir_stuck();
		return ret;
	}

	if (r->pin) {
		/*
		 * the signal stays armed until the calls it accounts for are done: a
		 * signal unregistered with an instance still in flight would drop the
		 * reference that instance still owes
		 */
		ret = hk_redir_drain();
		if (!ret)
			ret = hk_kretprobe_remove(&r->retprobe);
		if (ret) {
			r->pending = true;
			hk_redir_stuck();
			return ret;
		}
	} else {
		/*
		 * the probe is gone, so no new call can be redirected, but a core that
		 * was already inside the replacement is still there and still owns an
		 * entry in the marker. the grace period is what it takes for that core
		 * to be out, the same wait hk_inline does before it frees a trampoline
		 */
		hk_wait_tasks_rcu();
	}

	xa_destroy(&r->tasks);
	if (r->pending) {
		r->pending = false;
		if (g_redir_stuck)
			g_redir_stuck--;
	}
	return 0;
}

unsigned int hk_redir_pending(void)
{
	return READ_ONCE(g_redir_stuck);
}

unsigned int hk_redir_inflight(void)
{
	return (unsigned int)atomic_read(&g_redir_inflight);
}
