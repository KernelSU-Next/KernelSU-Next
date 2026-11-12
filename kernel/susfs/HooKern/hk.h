// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef LKMHOOK_HK_H
#define LKMHOOK_HK_H

#include <linux/types.h>

struct hk_cfg {
	unsigned long (*resolve)(const char *name);
	/*
	 * how the library writes text when a caller does not name a path itself,
	 * one of the hk_write_* primitives of hk_patch.h. NULL takes the kernel's
	 * own primitive, which assumes nothing about the headers this module was
	 * built with. the value is read once, in hk_init
	 */
	int (*write)(void *dst, const void *src, size_t len);
};

static inline bool hk_ker_addr_ok(unsigned long v)
{
	return v >= 0xffff000000000000UL;
}

/*
 * a probe replaces the word it lands on with brk, and an entry that an indirect
 * branch reaches starts with a landing pad, so the breakpoint takes the pad away
 * and the caller traps before the probe runs. the pac prologue is a landing pad
 * too, and behind it the link register is signed, which is the other half of the
 * same problem. arm64 leaves arch_adjust_kprobe_addr weak, so the placement stays
 * with the caller: a handle that names one of these runs it once at install time,
 * and NULL keeps the plain address the kernel would have used
 */
typedef int (*hk_probe_adjust_fn)(unsigned long addr, void *arg,
				  unsigned long *out);

/* the kernel's own predicate from 6.6 on, spelled here because 5.10 has no macro */
static inline bool hk_insn_is_bti(u32 insn)
{
	return (insn & 0xFFFFFF3Fu) == 0xD503241Fu;
}

/* the hint words an entry can start with that an indirect branch may target:
 * the bti family and the two pac prologues, which act as the pad when the
 * compiler writes one instead of a bti. bti r only answers a return, and is
 * skipped with the rest because leaving a hint behind costs nothing */
static inline bool hk_insn_is_pad(u32 insn)
{
	return hk_insn_is_bti(insn) || insn == 0xD503233Fu ||
	       insn == 0xD503237Fu;
}

/*
 * behind a pac prologue the link register is signed, so a handler that parks the
 * pc or hands the call on has to strip it first or the cpu gets an address no
 * branch can take. the strip is a hint, a no op on a cpu without pac, and an
 * address that was never signed comes back unchanged
 */
static inline unsigned long hk_pac_strip(unsigned long v)
{
	register unsigned long lr asm("x30") = v;

	asm("hint #7" : "+r"(lr));
	return lr;
}

/* the kprobe preset: skip the pads and stop on the body word, which is before the
 * frame is set up, so a handler that returns early leaves nothing to unwind. an
 * entry without a pad lands as it came, and a run of four pads is refused */
int hk_kprobe_adjust_pad(unsigned long addr, void *arg, unsigned long *out);

int hk_init(const struct hk_cfg *cfg);
void hk_exit(void);
unsigned long hk_resolve(const char *name);

/*
 * the consumers run their own exit, the library only counts the failures it was
 * told about and keeps the numbers for the next hk_exit. a module whose exit
 * path left kernel text pointing into module memory must not unload, so it calls
 * hk_exit_block at the end of its own exit and never returns
 */
void hk_exit_fail(int count);
unsigned int hk_exit_failed(void);
void hk_exit_block(void);

/*
 * the library refuses something and carries on, and says so here. a refused free
 * of a live hook and an emitter that disagrees with the length table both leave the
 * hook or the install in a state the caller wanted, so a caller that wants to count
 * them, log them its own way or trip an assertion of its own installs a notice
 * function. the default notice prints, which is the report a plain build has, and
 * hk_notice_set(NULL) silences it for a build that trusts itself
 */
enum hk_notice {
	HK_NOTICE_FREE_BEFORE_DISABLE = 0,	/* a free on a hook that is still live */
	HK_NOTICE_RELO_TABLE,			/* an emitter and the length table disagree */
};

typedef void (*hk_notify_fn)(enum hk_notice what, const char *detail);

/* called where the refusal happens: process context, and it must not sleep */
void hk_notice_set(hk_notify_fn fn);
void hk_notice(enum hk_notice what, const char *detail);

/*
 * one tasks rcu grace period: what has to pass between the moment code of this
 * module is taken out of a path another core could be running and the moment it is
 * freed. it is the flavour the kernel uses for its own text pokes, the symbol is
 * resolved here, and a kernel without it says so once instead of waiting for
 * nothing. the call inside is indirect and cfi is off for it, a direct call from a
 * consumer needs nothing
 */
void hk_wait_tasks_rcu(void);
#endif
