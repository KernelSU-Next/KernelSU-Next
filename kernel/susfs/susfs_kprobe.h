/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Arming a probe a second time needs the struct handed back the way the kernel left it the
 * first time.
 *
 * register_kprobe() resolves .symbol_name only while .addr is NULL: kprobe_addr() ->
 * _kprobe_addr() (kernel/kprobes.c) answers ERR_PTR(-EINVAL) as soon as both are set -
 *
 *     if ((symbol_name && addr) || (!symbol_name && !addr))
 *             goto invalid;
 *     ...
 * invalid:
 *     return ERR_PTR(-EINVAL);
 *
 * - and register_kprobe() stores the address it resolved BEFORE the checks that can still
 * fail it (`p->addr = addr;` sits above warn_kprobe_rereg()/check_kprobe_address_safe()), so
 * the field is written even when the call returns an error.  unregister_kprobes() clears it
 * only for a probe that was never on a list:
 *
 *     for (i = 0; i < num; i++)
 *             if (__unregister_kprobe_top(kps[i]) < 0)
 *                     kps[i]->addr = NULL;    (only on failure)
 *
 * which means a probe that DID arm keeps its address after it is removed.
 *
 * Put together: a struct kprobe that has armed once - or whose registration failed after the
 * symbol was resolved - can never be registered again.  Every later register_kprobe() on that
 * struct returns -EINVAL without touching the address, so the feature it carries stays off for
 * the rest of the module's life, and a failed retry looks exactly like a symbol that is not
 * probeable.
 *
 * Both sequences are ordinary here: every enable/disable control surface disarms and arms
 * again (the avc node, the sus_mount supercall), and every rule-management path retries a
 * registration that failed earlier (kstat's maps hook, open_redirect's forward and reverse
 * hooks, uname's set, sus_map's probes).  So both sides clear the field: the arm side before
 * it returns an error, the disarm side after unregistering.  sus_mount_fdinfo_arm()/disarm()
 * already did that by hand for its dynamically resolved addresses; these two helpers are the
 * same statement, so the next probe does not have to rediscover why it is needed.
 *
 * The struct also keeps KPROBE_FLAG_DISABLED after an unregister (__disable_kprobe() sets it on
 * the way out) and register_kprobe() preserves that one bit from the caller, so the same reuse
 * that needs .addr cleared needs the flag cleared too, or the probe is registered and never armed.
 *
 * A kretprobe embeds its kprobe, hence the second helper.
 */
#ifndef _SUSFS_KPROBE_H
#define _SUSFS_KPROBE_H

#include <linux/kprobes.h>

/* Call AFTER unregister_kprobe()/unregister_kretprobe() returns, never before: unregister_kprobes()
 * runs each probe's bottom half only `if (kps[i]->addr)`, so clearing the field first would skip
 * __unregister_kprobe_bottom() and leave the probe's instruction slot behind.  Call it on every
 * registration failure too, for the reason in the block above. */
static inline void susfs_kp_forget_addr(struct kprobe *kp)
{
	kp->addr = NULL;
	/* An unregistered probe keeps KPROBE_FLAG_DISABLED - __disable_kprobe() sets it, and
	 * register_kprobe() preserves exactly that one bit (`p->flags &= KPROBE_FLAG_DISABLED`)
	 * before it decides whether to arm.  Left set, the next registration succeeds, is listed,
	 * and never fires: the layer reports itself armed while its handler is never called. */
	kp->flags &= ~KPROBE_FLAG_DISABLED;
}

static inline void susfs_krp_forget_addr(struct kretprobe *krp)
{
	krp->kp.addr = NULL;
	krp->kp.flags &= ~KPROBE_FLAG_DISABLED;
}

#endif /* _SUSFS_KPROBE_H */
