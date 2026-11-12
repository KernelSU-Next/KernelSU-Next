// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef LKMHOOK_HK_SIGHOOK_H
#define LKMHOOK_HK_SIGHOOK_H

#include <linux/types.h>

struct hk_sighook_context {
	int signal;
	unsigned long pc;
	struct pt_regs *regs;
	void *priv;
};

typedef void (*hk_sighook_fn)(const struct hk_sighook_context *ctx);

/*
 * register an address interval. a process whose interrupted pc falls inside
 * [start, end) has fn called for every signal delivered to it before the kernel
 * builds the frame, so ctx->regs is the interrupted register file the signal
 * will be entered with and editing it redirects the handler. the delivery runs
 * afterwards either way
 *
 * intervals may not overlap, -EEXIST names the one in the way, the table is a
 * list and has no fixed capacity. fn runs from the kprobe pre handler of the
 * delivery path: preemption is off and sleeping is not allowed there, a
 * consumer that has to block queues work instead. the registration alone arms
 * nothing, hk_sighook_install registers the probes
 */
int hk_sighook_add(unsigned long start, unsigned long end, hk_sighook_fn fn,
		   void *priv);
void hk_sighook_del(hk_sighook_fn fn);

/*
 * the target of the dispatch is setup_rt_frame, and its entry is judged at
 * runtime instead of through a compile time version test: the probe reads the
 * first two instructions of the resolved symbol and answers whether the
 * argument convention is the four argument one this library reads. 0 when the
 * report was filled, -ENOENT when the symbol does not resolve
 */
int hk_sighook_probe(char *out, size_t len);

/* register the probes, 0 when the dispatch is armed */
int hk_sighook_install(void);
/*
 * what a consumer compiles in is its own build decision. setting the flag leaves
 * this module out of the link, and the calls the core makes into it become
 * nothing, while the entry points of the module itself stay declared: calling one
 * of those without its object is a link error, never a silent no op
 */
#ifdef HK_NO_SIGHOOK
static inline void hk_sighook_exit(void) { }
#else
/* unregister the probes and drop the table, called by hk_exit */
void hk_sighook_exit(void);
#endif
#endif
