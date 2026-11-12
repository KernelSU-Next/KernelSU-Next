// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/errno.h>
#include <linux/kprobes.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>

#include "hk.h"
#include "hk_kprobe.h"

typedef int (*register_kprobe_fn)(struct kprobe *p);
typedef void (*unregister_kprobe_fn)(struct kprobe *p);
typedef bool (*within_blacklist_fn)(unsigned long addr);
typedef int (*core_text_fn)(unsigned long addr);
typedef bool (*module_text_fn)(unsigned long addr);
typedef unsigned long (*ftrace_location_fn)(unsigned long ip);

/* one tracking node per live probe, the list has no ceiling */
struct hk_kprobe_node {
	struct list_head list;
	struct hk_kprobe *hook;
};

static LIST_HEAD(g_kprobes);
static DEFINE_SPINLOCK(g_kprobe_lock);

/* a probe that unregister_kprobe cannot take back keeps reaching into this module */
static unsigned int g_kprobe_stuck;

struct hk_blacklist_saved {
	struct kprobe_blacklist_entry *entry;
	unsigned long start_addr;
	unsigned long end_addr;
};

static struct hk_blacklist_saved *g_blacklist_saved;
static size_t g_blacklist_count;
static bool g_blacklist_cleared;
static DEFINE_MUTEX(g_blacklist_lock);

static __nocfi int call_register_kprobe(struct kprobe *p)
{
	register_kprobe_fn fn;

	fn = (register_kprobe_fn)hk_resolve("register_kprobe");
	if (!fn)
		return -ENODATA;
	return fn(p);
}

static __nocfi int call_unregister_kprobe(struct kprobe *p)
{
	unregister_kprobe_fn fn;

	fn = (unregister_kprobe_fn)hk_resolve("unregister_kprobe");
	if (!fn)
		return -ENODATA;
	fn(p);
	return 0;
}

static __nocfi bool call_within_blacklist(unsigned long addr)
{
	within_blacklist_fn fn;

	fn = (within_blacklist_fn)hk_resolve("within_kprobe_blacklist");
	return fn ? fn(addr) : false;
}

static __nocfi bool call_core_text(unsigned long addr)
{
	core_text_fn fn;

	fn = (core_text_fn)hk_resolve("core_kernel_text");
	if (!fn)
		return hk_ker_addr_ok(addr);
	return fn(addr);
}

static __nocfi bool call_module_text(unsigned long addr)
{
	module_text_fn fn;

	fn = (module_text_fn)hk_resolve("is_module_text_address");
	return fn ? fn(addr) : false;
}

/* the kernel takes core kernel text or module text, a rodata symbol is refused */
static __nocfi bool call_text_addr(unsigned long addr)
{
	return call_core_text(addr) || call_module_text(addr);
}

static __nocfi unsigned long call_ftrace_location(unsigned long addr)
{
	ftrace_location_fn fn;

	fn = (ftrace_location_fn)hk_resolve("ftrace_location");
	return fn ? fn(addr) : 0;
}

/*
 * the address gates of register_kprobe in its own order. every one of them
 * comes back as -EINVAL, so the caller has to be told which one fired
 */
static __nocfi enum hk_kprobe_state hk_kprobe_gate_addr(unsigned long a,
							const char **reason)
{
	if (!call_text_addr(a)) {
		*reason = "not kernel text";
		return HK_KPROBE_UNSUPPORTED;
	}
	if (call_within_blacklist(a)) {
		*reason = "kprobe blacklist, clear with hk_kprobe_clear_blacklist";
		return HK_KPROBE_BLACKLISTED;
	}
#ifndef CONFIG_KPROBES_ON_FTRACE
	/* arm64 never selects it, there a recorded call site is refused */
	if (call_ftrace_location(a) == a) {
		*reason = "ftrace call site";
		return HK_KPROBE_PATCHSITE;
	}
#endif
	*reason = "no address gate matched";
	return HK_KPROBE_OK;
}

static __nocfi enum hk_kprobe_state hk_kprobe_gate(const char *sym,
						   unsigned long *addr,
						   const char **reason)
{
	unsigned long a;

	a = *sym ? hk_resolve(sym) : 0;
	if (!a) {
		*addr = 0;
		*reason = "symbol not resolved";
		return HK_KPROBE_UNRESOLVED;
	}
	*addr = a;

	if (str_has_prefix(sym, "__cfi_") || str_has_prefix(sym, "__pfx_")) {
		*reason = "cfi preamble symbol";
		return HK_KPROBE_UNSUPPORTED;
	}
	return hk_kprobe_gate_addr(a, reason);
}

int hk_kprobe_check(const char *sym, struct hk_kprobe_report *out)
{
	if (!sym || !out)
		return -EINVAL;

	memset(out, 0, sizeof(*out));
	out->state = hk_kprobe_gate(sym, &out->addr, &out->reason);
	return 0;
}

int hk_kprobe_check_at(unsigned long addr, struct hk_kprobe_report *out)
{
	if (!addr || !out)
		return -EINVAL;

	memset(out, 0, sizeof(*out));
	out->addr = addr;
	out->state = hk_kprobe_gate_addr(addr, &out->reason);
	return 0;
}

/* an address and a symbol read the same in a log, one of them is just longer */
static void hk_kprobe_label(const struct hk_kprobe *h, char *buf, size_t len)
{
	if (h->kp.symbol_name)
		strscpy(buf, h->kp.symbol_name, len);
	else
		snprintf(buf, len, "0x%lx", (unsigned long)h->kp.addr);
}

/* a refused install names the symbol and the gate, never a bare errno */
static void hk_kprobe_fail(const char *label, int ret,
			   const struct hk_kprobe_report *rep)
{
	pr_warn("[lkmhook] kprobe %s failed %d %s\n", label, ret, rep->reason);
}

/*
 * the gate is asked only after the kernel refused, so a successful install costs
 * no resolver walk, and the label is the symbol for one route and the address for
 * the other because that is all either of them has
 */
static void hk_kprobe_fail_route(const struct hk_kprobe *h, int ret)
{
	struct hk_kprobe_report rep;
	char label[48];

	hk_kprobe_label(h, label, sizeof(label));
	if (h->kp.symbol_name) {
		if (!hk_kprobe_check(h->kp.symbol_name, &rep))
			hk_kprobe_fail(label, ret, &rep);
		return;
	}
	if (!hk_kprobe_check_at((unsigned long)h->kp.addr, &rep))
		hk_kprobe_fail(label, ret, &rep);
}

/* an unregistered probe on module text is a use after free, say so */
static int hk_kprobe_drop(struct hk_kprobe *h)
{
	char label[48];

	if (!call_unregister_kprobe(&h->kp))
		return 0;

	hk_kprobe_label(h, label, sizeof(label));
	pr_warn("[lkmhook] kprobe %s left registered, unregister_kprobe unresolved\n",
		label);
	return -ENODATA;
}

/* allocate the node, register and track: the two installs differ in one field */
static int hk_kprobe_add(struct hk_kprobe *h, unsigned long addr)
{
	struct hk_kprobe_node *node;
	unsigned long flags;
	char label[48];
	int ret;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node) {
		hk_kprobe_label(h, label, sizeof(label));
		pr_warn("[lkmhook] kprobe %s failed -ENOMEM, no tracking node\n",
			label);
		return -ENOMEM;
	}

	ret = call_register_kprobe(&h->kp);
	if (ret < 0) {
		kfree(node);
		hk_kprobe_fail_route(h, ret);
		return ret;
	}
	h->orig = addr ? addr : (unsigned long)h->kp.addr;

	node->hook = h;
	spin_lock_irqsave(&g_kprobe_lock, flags);
	list_add(&node->list, &g_kprobes);
	spin_unlock_irqrestore(&g_kprobe_lock, flags);

	hk_kprobe_label(h, label, sizeof(label));
	pr_info("[lkmhook] kprobe %s @ 0x%lx\n", label, h->orig);
	return 0;
}

/*
 * the caller named a landing rule, so the address the kernel would have used is
 * resolved here and handed to it once. a handle without one keeps the kernel's own
 * lookup and takes no new path
 */
static int hk_kprobe_adjust_route(struct hk_kprobe *h, const char *sym,
				  unsigned long *requested)
{
	unsigned long addr;
	unsigned long out;
	int ret;

	if (!h->adjust)
		return 0;

	addr = sym ? hk_resolve(sym) : *requested;
	if (!addr)
		return -ENOENT;

	out = addr;
	ret = h->adjust(addr, h->arg, &out);
	if (ret)
		return ret;

	*requested = addr;
	h->kp.symbol_name = NULL;
	h->kp.addr = (kprobe_opcode_t *)out;
	return 0;
}

int hk_kprobe_install(struct hk_kprobe *h, const char *sym,
		      kprobe_pre_handler_t pre)
{
	hk_probe_adjust_fn adjust;
	unsigned long requested = 0;
	void *arg;
	int ret;

	if (!h || !sym || !pre)
		return -EINVAL;

	/* what the caller named before the call, everything else is set here */
	adjust = h->adjust;
	arg = h->arg;

	memset(h, 0, sizeof(*h));
	h->adjust = adjust;
	h->arg = arg;
	h->kp.symbol_name = sym;
	h->kp.pre_handler = pre;

	ret = hk_kprobe_adjust_route(h, sym, &requested);
	if (ret)
		return ret;

	return hk_kprobe_add(h, requested);
}

int hk_kprobe_install_at(struct hk_kprobe *h, unsigned long addr,
			 kprobe_pre_handler_t pre)
{
	hk_probe_adjust_fn adjust;
	unsigned long requested = addr;
	void *arg;
	int ret;

	if (!h || !addr || !pre)
		return -EINVAL;

	adjust = h->adjust;
	arg = h->arg;

	memset(h, 0, sizeof(*h));
	h->adjust = adjust;
	h->arg = arg;
	h->kp.addr = (kprobe_opcode_t *)addr;
	h->kp.pre_handler = pre;

	ret = hk_kprobe_adjust_route(h, NULL, &requested);
	if (ret)
		return ret;

	return hk_kprobe_add(h, requested);
}

int hk_kprobe_remove(struct hk_kprobe *h)
{
	struct hk_kprobe_node *node;
	struct hk_kprobe_node *found = NULL;
	unsigned long flags;

	if (!h)
		return -EINVAL;

	spin_lock_irqsave(&g_kprobe_lock, flags);
	list_for_each_entry(node, &g_kprobes, list) {
		if (node->hook != h)
			continue;
		list_del(&node->list);
		found = node;
		break;
	}
	spin_unlock_irqrestore(&g_kprobe_lock, flags);

	/* not tracked means not registered, a second remove is a no op */
	if (!found)
		return 0;

	/* unregister sleeps, it stays outside the list lock */
	kfree(found);
	return hk_kprobe_drop(h);
}

int hk_kprobe_clear_blacklist(void)
{
	struct kprobe_blacklist_entry *ent;
	struct list_head *head;
	size_t count = 0;
	size_t i = 0;

	mutex_lock(&g_blacklist_lock);
	if (g_blacklist_cleared) {
		mutex_unlock(&g_blacklist_lock);
		return 0;
	}

	head = (struct list_head *)hk_resolve("kprobe_blacklist");
	if (!head || !hk_ker_addr_ok((unsigned long)head)) {
		mutex_unlock(&g_blacklist_lock);
		return -ENOENT;
	}

	list_for_each_entry(ent, head, list) {
		if (ent->start_addr && ent->end_addr)
			count++;
	}

	if (count) {
		g_blacklist_saved = kcalloc(count, sizeof(*g_blacklist_saved),
					    GFP_KERNEL);
		if (!g_blacklist_saved) {
			mutex_unlock(&g_blacklist_lock);
			return -ENOMEM;
		}
	}

	list_for_each_entry(ent, head, list) {
		if (!ent->start_addr || !ent->end_addr)
			continue;
		g_blacklist_saved[i].entry = ent;
		g_blacklist_saved[i].start_addr = ent->start_addr;
		g_blacklist_saved[i].end_addr = ent->end_addr;
		WRITE_ONCE(ent->start_addr, 0);
		WRITE_ONCE(ent->end_addr, 0);
		i++;
	}

	g_blacklist_count = count;
	g_blacklist_cleared = true;
	pr_info("[lkmhook] kprobe blacklist cleared %zu entries\n", count);
	mutex_unlock(&g_blacklist_lock);
	return 0;
}

void hk_kprobe_restore_blacklist(void)
{
	struct hk_blacklist_saved *s;
	size_t i;

	mutex_lock(&g_blacklist_lock);
	if (!g_blacklist_cleared) {
		mutex_unlock(&g_blacklist_lock);
		return;
	}

	for (i = 0; i < g_blacklist_count; i++) {
		s = &g_blacklist_saved[i];

		if (!s->entry)
			continue;
		WRITE_ONCE(s->entry->start_addr, s->start_addr);
		WRITE_ONCE(s->entry->end_addr, s->end_addr);
	}

	kfree(g_blacklist_saved);
	g_blacklist_saved = NULL;
	g_blacklist_count = 0;
	g_blacklist_cleared = false;
	pr_info("[lkmhook] kprobe blacklist restored\n");
	mutex_unlock(&g_blacklist_lock);
}

void hk_kprobe_exit(void)
{
	struct hk_kprobe_node *node;
	struct hk_kprobe_node *tmp;
	LIST_HEAD(pending);
	unsigned long flags;

	/* take the list off the lock first, unregister sleeps */
	spin_lock_irqsave(&g_kprobe_lock, flags);
	list_splice_init(&g_kprobes, &pending);
	spin_unlock_irqrestore(&g_kprobe_lock, flags);

	list_for_each_entry_safe(node, tmp, &pending, list) {
		if (hk_kprobe_drop(node->hook))
			g_kprobe_stuck++;
		list_del(&node->list);
		kfree(node);
	}

	hk_kprobe_restore_blacklist();
}

unsigned int hk_kprobe_pending(void)
{
	return READ_ONCE(g_kprobe_stuck);
}
