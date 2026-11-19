// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef LKMHOOK_HK_KRETPROBE_H
#define LKMHOOK_HK_KRETPROBE_H

#include <linux/kprobes.h>
#include <linux/types.h>

#include "hk.h"

struct hk_kretprobe {
	struct kretprobe rp;	/* rp.kp.addr is where the probe landed */
	unsigned long orig;	/* the address asked for */
};

/*
 * a kretprobe fires after the target returns, the return value is in x0 and a
 * store to regs through hk_regs_set_ret rewrites it, which a kprobe cannot do
 * the handler runs on the returning core with the registers of the caller, so
 * the call has already happened and cannot be suppressed. use the entry and
 * return pair when the handler needs per call state, the entry handler returns
 * 1 to accept the instance and fills ri->data with data_size bytes
 *
 * the address gates are the kprobe ones, register_kretprobe registers a kprobe
 * first, so hk_kprobe_check answers for a kretprobe target too and a refused
 * install logs the symbol and the gate. installs are tracked in a list and
 * removed again by hk_kretprobe_exit, there is no ceiling and no -ENOSPC,
 * -ENOMEM when the tracking node cannot be allocated and nothing is registered
 *
 * the target has to return. a function that never returns leaks one instance
 * per call until maxactive runs out, and the probe then misses returns instead
 * of failing the install. a hot target other cores execute during
 * installation still belongs here, never to hk_inline_hook
 *
 * the probe sits on the function entry and the kernel enforces it:
 * kprobe_on_func_entry refuses every other address, and arm64 has no
 * arch_adjust_kprobe_addr override to present a shifted word as the entry. the
 * breakpoint takes that first word, so a function that starts with a landing pad
 * loses it and an indirect caller then traps bti. the kprobe route with its pad
 * walk is for those, a kretprobe has no landing to give
 */
int hk_kretprobe_install(struct hk_kretprobe *h, const char *sym,
			 kretprobe_handler_t handler);
int hk_kretprobe_install_ex(struct hk_kretprobe *h, const char *sym,
			    kretprobe_handler_t entry,
			    kretprobe_handler_t handler,
			    size_t data_size);
/*
 * the same for a target the caller holds as an address, which is what a function
 * of its own needs: a completion signal has to sit on the replacement, and a
 * replacement is a pointer, not a name. the address has to be the function entry,
 * the kernel refuses every other one
 */
int hk_kretprobe_install_at(struct hk_kretprobe *h, unsigned long addr,
			    kretprobe_handler_t handler);
/*
 * unregister and drop the tracking node, a second remove is a no op. 0 when the
 * probe is gone, -ENODATA when unregister_kretprobe cannot be reached and the
 * probe is still armed
 */
int hk_kretprobe_remove(struct hk_kretprobe *h);
/*
 * what a consumer compiles in is its own build decision. setting the flag leaves
 * this module out of the link, and the calls the core makes into it become
 * nothing, while the entry points of the module itself stay declared: calling one
 * of those without its object is a link error, never a silent no op
 */
#ifdef HK_NO_KRETPROBE
static inline void hk_kretprobe_exit(void) { }
#else
/* unregister every tracked probe, called by hk_exit */
void hk_kretprobe_exit(void);
#endif
/*
 * probes the exit sweep could not take back, the counterpart of
 * hk_kprobe_pending and counted by the same gate
 */
/*
 * what a consumer compiles in is its own build decision. setting the flag leaves
 * this module out of the link, and the calls the core makes into it become
 * nothing, while the entry points of the module itself stay declared: calling one
 * of those without its object is a link error, never a silent no op
 */
#ifdef HK_NO_KRETPROBE
static inline unsigned int hk_kretprobe_pending(void) { return 0; }
#else
unsigned int hk_kretprobe_pending(void);
#endif

#endif
