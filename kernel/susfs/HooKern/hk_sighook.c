// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/errno.h>
#include <linux/kprobes.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "hk.h"
#include "hk_kprobe.h"
#include "hk_sighook.h"

/* entries one delivery may fire, the lock is dropped before they run */
#define HK_SIGHOOK_FIRE_MAX 8

struct hk_sighook_entry {
	struct list_head list;
	unsigned long start;
	unsigned long end;
	hk_sighook_fn fn;
	void *priv;
};

static LIST_HEAD(g_sighook);
static DEFINE_SPINLOCK(g_sighook_lock);

static struct hk_kprobe g_sighook_rt;
static bool g_sighook_armed;

int hk_sighook_add(unsigned long start, unsigned long end, hk_sighook_fn fn,
		   void *priv)
{
	struct hk_sighook_entry *new;
	struct hk_sighook_entry *ent;

	if (!fn || end <= start)
		return -EINVAL;
	if (!hk_ker_addr_ok(start) || !hk_ker_addr_ok(end))
		return -EINVAL;

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	spin_lock(&g_sighook_lock);
	list_for_each_entry(ent, &g_sighook, list) {
		if (start < ent->end && ent->start < end) {
			unsigned long other = ent->start;

			spin_unlock(&g_sighook_lock);
			kfree(new);
			pr_warn("[lkmhook] sighook range 0x%lx conflicts with 0x%lx\n",
				start, other);
			return -EEXIST;
		}
	}
	new->start = start;
	new->end = end;
	new->fn = fn;
	new->priv = priv;
	list_add(&new->list, &g_sighook);
	spin_unlock(&g_sighook_lock);
	return 0;
}

void hk_sighook_del(hk_sighook_fn fn)
{
	struct hk_sighook_entry *ent;
	struct hk_sighook_entry *tmp;

	if (!fn)
		return;

	spin_lock(&g_sighook_lock);
	list_for_each_entry_safe(ent, tmp, &g_sighook, list) {
		if (ent->fn != fn)
			continue;
		list_del(&ent->list);
		spin_unlock(&g_sighook_lock);
		kfree(ent);
		return;
	}
	spin_unlock(&g_sighook_lock);
}

/*
 * the interrupted register file of the delivery, which is argument four of
 * setup_rt_frame. the kernel builds the signal frame around this same pointer
 * and enters the handler with it, so editing it here is editing the handler
 * entry state, and that is the only way to change what the handler sees
 */
static int hk_sighook_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct hk_sighook_entry *fire[HK_SIGHOOK_FIRE_MAX];
	struct hk_sighook_entry *ent;
	struct pt_regs *user;
	unsigned long pc;
	unsigned int count = 0;
	unsigned int i;
	int signal;

	(void)p;
	user = (struct pt_regs *)hk_regs_arg(regs, 3);
	if (!user || !hk_ker_addr_ok((unsigned long)user))
		return 0;
	pc = hk_regs_ip(user);
	signal = (int)hk_regs_arg(regs, 0);

	/*
	 * the entries are collected under the lock and the consumers run after
	 * it is dropped: a consumer may take its own lock and has to be able to
	 * unregister itself, and the interval table is a short list
	 */
	spin_lock(&g_sighook_lock);
	list_for_each_entry(ent, &g_sighook, list) {
		if (pc < ent->start || pc >= ent->end)
			continue;
		if (count >= HK_SIGHOOK_FIRE_MAX) {
			pr_warn_once("[lkmhook] sighook over %d hits in one delivery\n",
				     HK_SIGHOOK_FIRE_MAX);
			break;
		}
		fire[count++] = ent;
	}
	spin_unlock(&g_sighook_lock);

	for (i = 0; i < count; i++) {
		struct hk_sighook_context ctx;

		ctx.signal = signal;
		ctx.pc = pc;
		ctx.regs = user;
		ctx.priv = fire[i]->priv;
		fire[i]->fn(&ctx);
	}
	return 0;
}

/*
 * the dispatch target is read at install time instead of being chosen by a
 * version test: the first instruction of a function built for this series is a
 * landing pad or a paciasp prologue. an entry that is neither means the symbol
 * resolved to something else, and that is worth a line before the probe is
 * armed on it
 */
int hk_sighook_probe(char *out, size_t len)
{
	unsigned long addr;
	u32 insn[2];

	if (!out || !len)
		return -EINVAL;
	out[0] = 0;
	addr = hk_resolve("setup_rt_frame");
	if (!addr || !hk_ker_addr_ok(addr))
		return -ENOENT;
	if (copy_from_kernel_nofault(insn, (const void *)addr, sizeof(insn))) {
		scnprintf(out, len, "setup_rt_frame unreadable");
		return 0;
	}
	scnprintf(out, len, "setup_rt_frame 0x%lx entry %08x %08x", addr,
		  insn[0], insn[1]);
	return 0;
}

int hk_sighook_install(void)
{
	char report[96];
	int ret;

	if (g_sighook_armed)
		return 0;
	if (hk_sighook_probe(report, sizeof(report)))
		scnprintf(report, sizeof(report), "setup_rt_frame unresolved");
	pr_info("[lkmhook] sighook %s\n", report);

	ret = hk_kprobe_install(&g_sighook_rt, "setup_rt_frame",
				hk_sighook_pre);
	if (ret) {
		pr_warn("[lkmhook] sighook probe refused %d\n", ret);
		return ret;
	}
	g_sighook_armed = true;
	return 0;
}

void hk_sighook_exit(void)
{
	struct hk_sighook_entry *ent;
	struct hk_sighook_entry *tmp;

	if (g_sighook_armed) {
		hk_kprobe_remove(&g_sighook_rt);
		g_sighook_armed = false;
	}
	spin_lock(&g_sighook_lock);
	list_for_each_entry_safe(ent, tmp, &g_sighook, list) {
		list_del(&ent->list);
		kfree(ent);
	}
	spin_unlock(&g_sighook_lock);
}
