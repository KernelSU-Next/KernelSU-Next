// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef LKMHOOK_HK_REDIR_H
#define LKMHOOK_HK_REDIR_H

#include <linux/types.h>
#include <linux/xarray.h>

#include "hk_inline.h"
#include "hk_kprobe.h"
#include "hk_kretprobe.h"

/*
 * running another function in place of a kernel function, which is what a consumer
 * that changes behaviour needs and what a probe alone cannot give it. the caller
 * names the target, the replacement and the mechanism, and the library keeps no
 * opinion about which mechanism suits which target: hk_inline_probe and
 * hk_kprobe_check answer that before anything is written
 *
 * hk_redir_text writes the entry of the target. it takes both sides by name,
 * because a branch is written to the address the resolver answers, cfi suffix
 * included, and because the entry has to be judged and relocated first. the
 * original is the trampoline hk_inline builds, reached through hk_redir.orig, and
 * nothing has to be done about entering the target again: that trampoline is a
 * copy of the entry
 *
 * hk_redir_kprobe leaves the entry alone and arms a breakpoint on it, so a target
 * another core is executing can still be redirected, and a page that cannot be
 * written at all is still a target. it takes a symbol or an address on either
 * side, so a syscall table entry the caller read itself is redirected the same way
 * a symbol is. the replacement runs with the argument registers of the target, on
 * the caller stack of the target, and returns to the caller of the target
 *
 * the marker, kprobe route only. a replacement reaches the original by calling
 * hk_redir.orig, which is the probed entry itself: the breakpoint fires again and
 * the handler has to let that call through instead of redirecting it a second
 * time. which calls are let through is the caller's choice, made before the
 * install:
 *
 *	HK_REDIR_GUARD_TASK	the handler marks the task it redirected, and the
 *				replacement clears the mark through hk_redir_leave.
 *				a replacement that never clears it stops being
 *				redirected for that task and the original runs from
 *				then on, which is a lost hook and not a crash
 *	HK_REDIR_GUARD_CALLER	the handler reads the return address and lets a
 *				call from the module this library was built into
 *				through. no state, nothing to clear, and a call the
 *				owning module makes on purpose is not redirected
 *				either. hk_redir_leave has nothing to do here
 *
 * a task marker costs one xarray entry per task that passes and needs the leave
 * call in every replacement. a caller marker is free and cannot be forgotten, and
 * it reads the return address, so it cannot tell a pass through from a deliberate
 * call by the same module
 *
 * the pin, kprobe route only, asked for before the install. while a redirected
 * call is inside a replacement, the module this library was built into is held
 * with a reference, taken in the handler and dropped by a kretprobe the library
 * arms on the replacement itself. the kernel's own delete_module refuses to
 * unload a module whose reference count is not zero, so the module cannot go away
 * under a call that is still running, and hk_redir_inflight reports how many such
 * calls are inside right now. a pin that cannot be completed is refused instead
 * of being dropped, and it costs one more trap per redirected call. the counter
 * and the reference belong to the module, not to one hook, because the module is
 * what must not be freed
 *
 *	__nocfi long my_target(int a)
 *	{
 *		long ret = my_orig(a);		my_orig is hk_redir.orig
 *
 *		hk_redir_leave(&r);
 *		return ret + 1;
 *	}
 *
 * a build that leaves the inline group out has no text route at all: the install
 * answers -EOPNOTSUPP for HK_REDIR_TEXT, the same answer the address route gives to
 * a mechanism the caller did not link in
 *
 * a refused install returns what its route reports, -EINVAL for a null argument,
 * -EOPNOTSUPP for a route that cannot take what it was given, and -ENODATA for a
 * pin whose kernel entry points cannot be resolved: the text route reports what
 * hk_inline_hook reports, the kprobe route what register_kprobe reports, which is
 * -EINVAL for every address gate
 */

enum hk_redir_how {
	HK_REDIR_TEXT = 0,	/* the entry is written, the target must not be running */
	HK_REDIR_KPROBE,	/* the entry keeps its bytes, a breakpoint carries the call */
};

enum hk_redir_guard {
	HK_REDIR_GUARD_TASK = 0,	/* the per task marker, cleared by hk_redir_leave */
	HK_REDIR_GUARD_CALLER,		/* the return address, nothing to clear */
};

struct hk_redir {
	enum hk_redir_how how;
	enum hk_redir_guard guard;	/* read before the install, zero is the task marker */
	bool pin;			/* read before the install, see the pin note */
	const char *name;		/* the target as it was named, null for an address */
	unsigned long addr;		/* the entry a call lands on */
	unsigned long orig;		/* what a replacement calls to reach the original */
	unsigned long repl;		/* what a redirected call runs instead */
	bool live;			/* set while the redirect is in place */
	bool pending;			/* armed and not taken back, counted by hk_redir_pending */
	struct hk_kretprobe retprobe;	/* the completion signal, pinned hooks only */
	struct xarray tasks;		/* the per task marker, task guard only */
	union {
		struct hk_inline text;
		struct hk_kprobe kprobe;
	} u;
};

/*
 * both sides by name, resolved here: the cfi suffix of a replacement is not
 * something a caller can spell, and a name is what the text route judges an entry by
 */
int hk_redir_install(struct hk_redir *r, const char *sym, const char *repl_sym,
		     enum hk_redir_how how);
/*
 * both sides by address, nothing resolved. only the kprobe route takes this: the
 * text route patches an entry it has to judge and relocate by name
 */
int hk_redir_install_at(struct hk_redir *r, unsigned long addr,
			unsigned long repl, enum hk_redir_how how);
/*
 * take it back. the text route disables the entry, waits one tasks rcu grace
 * period and frees its trampoline, and a hook whose entry could not be restored
 * stays live and counted by hk_inline_pending. the kprobe route unregisters the
 * probe, lets the calls that are still inside a replacement come back, then drops
 * the marker and the completion signal of a pinned hook. anything that could not
 * be taken back stays armed, is counted by hk_redir_pending and hk_kprobe_pending,
 * and is refused by the exit path, so a second call is allowed and retries what is
 * left. the waits are short and bounded: a replacement that never returns is
 * reported, not waited for
 */
int hk_redir_remove(struct hk_redir *r);

/*
 * what a consumer compiles in is its own build decision. setting the flag leaves
 * this module out of the link, and the calls the core makes into it become
 * nothing, while the entry points of the module itself stay declared: calling one
 * of those without its object is a link error, never a silent no op
 */
#ifdef HK_NO_REDIR
static inline unsigned int hk_redir_pending(void) { return 0; }
static inline unsigned int hk_redir_inflight(void) { return 0; }
#else
/* redirects the exit path refuses to unload over: what could not be taken back */
unsigned int hk_redir_pending(void);
/* calls inside a replacement right now, pinned hooks only */
unsigned int hk_redir_inflight(void);
#endif

/*
 * the task marker of the kprobe route. the handler this library installs calls the
 * enter half, a replacement calls the leave half. false from enter means the call
 * is a nested one and runs the original, which is what a handler returns 0 for.
 * the caller marker keeps no state, so both halves do nothing for it
 */
bool hk_redir_guard_enter(struct hk_redir *r);
void hk_redir_leave(struct hk_redir *r);
#endif
