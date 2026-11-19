// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/errno.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>

#include "hk.h"
#include "hk_kprobe.h"
#include "hk_kretprobe.h"

typedef int (*register_kretprobe_fn)(struct kretprobe *rp);
typedef void (*unregister_kretprobe_fn)(struct kretprobe *rp);

/* one tracking node per live probe, the list has no ceiling */
struct hk_kretprobe_node {
	struct list_head list;
	struct hk_kretprobe *hook;
};

static LIST_HEAD(g_kretprobes);
static DEFINE_SPINLOCK(g_kretprobe_lock);

/* a probe the sweep cannot take back keeps reaching into this module */
static unsigned int g_kretprobe_stuck;

static __nocfi int call_register_kretprobe(struct kretprobe *rp)
{
	register_kretprobe_fn fn;

	fn = (register_kretprobe_fn)hk_resolve("register_kretprobe");
	if (!fn)
		return -ENODATA;
	return fn(rp);
}

static __nocfi int call_unregister_kretprobe(struct kretprobe *rp)
{
	unregister_kretprobe_fn fn;

	fn = (unregister_kretprobe_fn)hk_resolve("unregister_kretprobe");
	if (!fn)
		return -ENODATA;
	fn(rp);
	return 0;
}

/* an address and a symbol read the same in a log, one of them is just longer */
static void hk_kretprobe_label(const struct hk_kretprobe *h, char *buf, size_t len)
{
	if (h->rp.kp.symbol_name)
		strscpy(buf, h->rp.kp.symbol_name, len);
	else
		snprintf(buf, len, "0x%lx", (unsigned long)h->rp.kp.addr);
}

/* register_kretprobe registers a kprobe, the kprobe gates answer for it */
static void hk_kretprobe_fail(const char *label, int ret,
			      const struct hk_kprobe_report *rep)
{
	pr_warn("[lkmhook] kretprobe %s failed %d %s\n", label, ret, rep->reason);
}

/*
 * the gate is asked only after the kernel refused, so a successful install costs
 * no resolver walk, and the label is the symbol for one route and the address for
 * the other because that is all either of them has
 */
static void hk_kretprobe_fail_route(const struct hk_kretprobe *h, int ret)
{
	struct hk_kprobe_report rep;
	char label[48];

	hk_kretprobe_label(h, label, sizeof(label));
	if (h->rp.kp.symbol_name) {
		if (!hk_kprobe_check(h->rp.kp.symbol_name, &rep))
			hk_kretprobe_fail(label, ret, &rep);
		return;
	}
	if (!hk_kprobe_check_at((unsigned long)h->rp.kp.addr, &rep))
		hk_kretprobe_fail(label, ret, &rep);
}

/* an unregistered probe on module text is a use after free, say so */
static int hk_kretprobe_drop(struct hk_kretprobe *h)
{
	char label[48];

	if (!call_unregister_kretprobe(&h->rp))
		return 0;

	hk_kretprobe_label(h, label, sizeof(label));
	pr_warn("[lkmhook] kretprobe %s left registered, unregister unresolved\n",
		label);
	return -ENODATA;
}

static void hk_kretprobe_track(struct hk_kretprobe_node *node,
			       struct hk_kretprobe *h)
{
	unsigned long flags;

	node->hook = h;
	spin_lock_irqsave(&g_kretprobe_lock, flags);
	list_add(&node->list, &g_kretprobes);
	spin_unlock_irqrestore(&g_kretprobe_lock, flags);
}

/* a tracking node is allocated first, so a probe is never left untracked */
static struct hk_kretprobe_node *hk_kretprobe_node_alloc(const char *label)
{
	struct hk_kretprobe_node *node;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		pr_warn("[lkmhook] kretprobe %s failed -ENOMEM, no tracking node\n",
			label);
	return node;
}

/* register, track and report: the two installs differ in one field */
static int hk_kretprobe_add(struct hk_kretprobe *h)
{
	struct hk_kretprobe_node *node;
	char label[48];
	int ret;

	hk_kretprobe_label(h, label, sizeof(label));
	node = hk_kretprobe_node_alloc(label);
	if (!node)
		return -ENOMEM;

	ret = call_register_kretprobe(&h->rp);
	if (ret < 0) {
		kfree(node);
		hk_kretprobe_fail_route(h, ret);
		return ret;
	}
	if (!h->orig)
		h->orig = (unsigned long)h->rp.kp.addr;

	hk_kretprobe_track(node, h);
	pr_info("[lkmhook] kretprobe %s @ 0x%lx\n", label,
		(unsigned long)h->rp.kp.addr);
	return 0;
}

int hk_kretprobe_install(struct hk_kretprobe *h, const char *sym,
			 kretprobe_handler_t handler)
{
	if (!h || !sym || !handler)
		return -EINVAL;

	memset(h, 0, sizeof(*h));
	h->rp.kp.symbol_name = sym;
	h->rp.handler = handler;
	return hk_kretprobe_add(h);
}

int hk_kretprobe_install_at(struct hk_kretprobe *h, unsigned long addr,
			    kretprobe_handler_t handler)
{
	if (!h || !addr || !handler)
		return -EINVAL;

	memset(h, 0, sizeof(*h));
	h->rp.kp.addr = (kprobe_opcode_t *)addr;
	h->rp.handler = handler;
	h->orig = addr;
	return hk_kretprobe_add(h);
}

int hk_kretprobe_install_ex(struct hk_kretprobe *h, const char *sym,
			    kretprobe_handler_t entry,
			    kretprobe_handler_t handler,
			    size_t data_size)
{
	struct hk_kretprobe_node *node;
	int ret;

	if (!h || !sym || !handler)
		return -EINVAL;

	node = hk_kretprobe_node_alloc(sym);
	if (!node)
		return -ENOMEM;

	memset(h, 0, sizeof(*h));
	h->rp.kp.symbol_name = sym;
	h->rp.entry_handler = entry;
	h->rp.handler = handler;
	h->rp.data_size = data_size;

	ret = call_register_kretprobe(&h->rp);
	if (ret < 0) {
		kfree(node);
		hk_kretprobe_fail_route(h, ret);
		return ret;
	}
	if (!h->orig)
		h->orig = (unsigned long)h->rp.kp.addr;

	hk_kretprobe_track(node, h);
	pr_info("[lkmhook] kretprobe %s\n", sym);
	return 0;
}

int hk_kretprobe_remove(struct hk_kretprobe *h)
{
	struct hk_kretprobe_node *node;
	struct hk_kretprobe_node *found = NULL;
	unsigned long flags;

	if (!h)
		return -EINVAL;

	spin_lock_irqsave(&g_kretprobe_lock, flags);
	list_for_each_entry(node, &g_kretprobes, list) {
		if (node->hook != h)
			continue;
		list_del(&node->list);
		found = node;
		break;
	}
	spin_unlock_irqrestore(&g_kretprobe_lock, flags);

	/* not tracked means not registered, a second remove is a no op */
	if (!found)
		return 0;

	/* unregister sleeps, it stays outside the list lock */
	kfree(found);
	return hk_kretprobe_drop(h);
}

void hk_kretprobe_exit(void)
{
	struct hk_kretprobe_node *node;
	struct hk_kretprobe_node *tmp;
	LIST_HEAD(pending);
	unsigned long flags;

	/* take the list off the lock first, unregister sleeps */
	spin_lock_irqsave(&g_kretprobe_lock, flags);
	list_splice_init(&g_kretprobes, &pending);
	spin_unlock_irqrestore(&g_kretprobe_lock, flags);

	list_for_each_entry_safe(node, tmp, &pending, list) {
		if (hk_kretprobe_drop(node->hook))
			g_kretprobe_stuck++;
		list_del(&node->list);
		kfree(node);
	}
}

unsigned int hk_kretprobe_pending(void)
{
	return READ_ONCE(g_kretprobe_stuck);
}
