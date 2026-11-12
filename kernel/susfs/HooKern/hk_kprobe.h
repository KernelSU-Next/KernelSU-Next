// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef LKMHOOK_HK_KPROBE_H
#define LKMHOOK_HK_KPROBE_H

#include <linux/kprobes.h>
#include <linux/types.h>

#include "hk.h"

struct hk_kprobe {
	struct kprobe kp;	/* kp.addr is where the probe landed */
	unsigned long orig;	/* the address asked for */
	hk_probe_adjust_fn adjust;	/* read before the install */
	void *arg;			/* read before the install */
};

/*
 * use kprobe instead of hk_inline_hook when
 *   the target can run on another core while the detour is written, a hot path
 *   like device_add. register_kprobe arms the breakpoint under text_mutex and
 *   cpus_read_lock, the inline primitive writes text with no such cover
 *   the entry window holds an ftrace patch site, hk_inline_probe reports
 *   HK_INLINE_PATCHSITE. the relocator would move a patched call site into the
 *   trampoline and lose it, kprobe leaves the call site to ftrace
 *   the relocator refuses the first instructions, hk_inline_probe reports
 *   HK_INLINE_UNSUPPORTED
 *
 * the price is the handler contract: a probe observes a call, it does not
 * replace the function. a pre handler runs at the entry with the arguments in
 * regs, it can rewrite them, or return 1 and park the pc, see hk_regs_return
 * below. the kernel does not step over the probed instruction by itself, so a
 * handler that returns 1 has to move the pc itself. calling the target from a
 * handler re-enters the same probe, so a full replacement stays an inline hook
 * on a target other cores do not execute during installation
 *
 * every install is tracked in a list and removed again by hk_kprobe_exit, there
 * is no ceiling on the number of live probes and no -ENOSPC. a refused install
 * logs the symbol and the gate and returns the kernel code, -EINVAL for every
 * address gate and -ENOENT for a symbol that does not resolve, -ENODATA when
 * register_kprobe cannot be reached, -ENOMEM when the tracking node cannot be
 * allocated, in which case nothing was registered
 */
int hk_kprobe_install(struct hk_kprobe *h, const char *sym,
		      kprobe_pre_handler_t pre);
/*
 * the same for a target the caller holds as an address: a table entry it read
 * itself, or a function whose name it does not want the resolver to see. addr has
 * to be kernel or module text, which is the same gate the kernel asks, and every
 * other gate, the count of live probes and the exit path are the ones above
 */
int hk_kprobe_install_at(struct hk_kprobe *h, unsigned long addr,
			 kprobe_pre_handler_t pre);
/*
 * unregister and drop the tracking node, a second remove is a no op. 0 when the
 * probe is gone, -ENODATA when unregister_kprobe cannot be reached and the probe
 * is still armed, which is what a caller that owns the code behind it has to know
 */
int hk_kprobe_remove(struct hk_kprobe *h);
/*
 * what a consumer compiles in is its own build decision. setting the flag leaves
 * this module out of the link, and the calls the core makes into it become
 * nothing, while the entry points of the module itself stay declared: calling one
 * of those without its object is a link error, never a silent no op
 */
#ifdef HK_NO_KPROBE
static inline void hk_kprobe_exit(void) { }
#else
/* unregister every tracked probe, called by hk_exit */
void hk_kprobe_exit(void);
#endif
/*
 * probes the exit sweep could not take back, which are the ones whose handler is
 * still reached from kernel text: a module that owns such a probe must not be
 * unmapped, and this is what hk_exit_block counts. a remove the caller runs itself
 * answers the same question in its return value and does not add to this
 */
/*
 * what a consumer compiles in is its own build decision. setting the flag leaves
 * this module out of the link, and the calls the core makes into it become
 * nothing, while the entry points of the module itself stay declared: calling one
 * of those without its object is a link error, never a silent no op
 */
#ifdef HK_NO_KPROBE
static inline unsigned int hk_kprobe_pending(void) { return 0; }
#else
unsigned int hk_kprobe_pending(void);
#endif


/* drop the blacklist entries, every probe refused with HK_KPROBE_BLACKLISTED
 * needs this first, hk_kprobe_exit restores them */
int hk_kprobe_clear_blacklist(void);
void hk_kprobe_restore_blacklist(void);

/*
 * what the address gates of register_kprobe make of a symbol, see
 * hk_inline_state for the inline counterpart
 */
enum hk_kprobe_state {
	HK_KPROBE_UNRESOLVED = 0,	/* the resolver did not find the symbol */
	HK_KPROBE_UNSUPPORTED,		/* not text, or a cfi preamble symbol */
	HK_KPROBE_BLACKLISTED,		/* kprobe_blacklist, __kprobes or noinstr */
	HK_KPROBE_PATCHSITE,		/* a recorded ftrace call site */
	HK_KPROBE_OK,			/* no address gate matched */
};

struct hk_kprobe_report {
	unsigned long addr;	/* resolver result, 0 when unresolved */
	enum hk_kprobe_state state;
	const char *reason;	/* static string, the caller never owns it */
};

/*
 * the kernel folds every reason it refuses an address into -EINVAL, so a bare
 * return code cannot tell a blacklisted symbol from an ftrace call site or a
 * symbol the resolver never found. this asks the same gates in the same order
 * and names the one that fired. nothing is written, nothing is allocated and
 * it does not sleep, so a live hot path can be judged before it is probed
 *
 * 0 when the report was filled, -EINVAL on a NULL argument. HK_KPROBE_OK means
 * no address gate matched, an install can still fail on a kernel symbol the
 * resolver cannot reach or on a tracking node that cannot be allocated. the
 * same gates guard a kretprobe, register_kretprobe registers a kprobe
 */
int hk_kprobe_check(const char *sym, struct hk_kprobe_report *out);
/* the same gates on an address, for a target that was never a symbol */
int hk_kprobe_check_at(unsigned long addr, struct hk_kprobe_report *out);

/*
 * argument and return helpers for a handler on arm64. x0 to x7 carry the first
 * eight arguments at a function entry, x0 carries the return value when a
 * kretprobe handler runs, and the kernel writes the registers back after the
 * handler, so a store to them takes effect
 */
static inline unsigned long hk_regs_arg(const struct pt_regs *regs,
					unsigned int n)
{
	if (n >= 8)
		return 0;
	return regs->regs[n];
}

static inline void hk_regs_set_arg(struct pt_regs *regs, unsigned int n,
				   unsigned long v)
{
	if (n < 8)
		regs->regs[n] = v;
}

static inline unsigned long hk_regs_ret(const struct pt_regs *regs)
{
	return regs->regs[0];
}

static inline void hk_regs_set_ret(struct pt_regs *regs, unsigned long v)
{
	regs->regs[0] = v;
}

static inline unsigned long hk_regs_ip(const struct pt_regs *regs)
{
	return regs->pc;
}

static inline unsigned long hk_regs_lr(const struct pt_regs *regs)
{
	return regs->regs[30];
}

/* a pre handler that returns 1 must step the pc over the probed instruction */
static inline void hk_regs_skip(struct pt_regs *regs)
{
	regs->pc += 4;
}

/*
 * the replacement idiom for a hot target: a pre handler that returns 1 stops the
 * kernel from running the probed instruction, so parking the pc on the link
 * register returns from the target at once with x0 as the result, the way a
 * wrapper would. the pad walk places the probe on the body word, before the frame
 * is set up, so nothing is left to unwind, and a probe that sits behind a pac
 * prologue reads a signed link register, which is stripped and written back here,
 * the way the epilogue the target skipped would have left it
 *
 *	hk_regs_set_ret(regs, my_device_add(hk_regs_arg(regs, 0)));
 *	hk_regs_return(regs);
 *	return 1;
 */
static inline void hk_regs_return(struct pt_regs *regs)
{
	regs->regs[30] = hk_pac_strip(regs->regs[30]);
	regs->pc = regs->regs[30];
}

/*
 * hand the call to another function instead of running the probed instruction: the
 * replacement is entered with the argument registers of the target, on the target's
 * caller stack, and its return goes to the caller of the target, so what it returns
 * is what the target returns. it is reached by the return from the breakpoint
 * exception and not by a branch, so no bti landing pad and no cfi check stands at
 * its entry, and the target never runs unless the replacement calls it
 *
 * a handler that redirects returns 1, exactly like one that parks the pc. the link
 * register is stripped before the hand over, so the replacement returns to the
 * caller and not to an address the pac prologue signed. reaching the original
 * again is the address of the probed entry: the breakpoint fires a second time and
 * the handler has to let that call through, which is what the guard of hk_redir is
 * for
 */
static inline void hk_regs_redirect(struct pt_regs *regs, unsigned long fn)
{
	regs->regs[30] = hk_pac_strip(regs->regs[30]);
	regs->pc = fn;
}
#endif
