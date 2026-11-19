// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/list.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "hk_patch.h"
#include "hk_ptr.h"

struct hk_ptr_entry {
	struct list_head list;
	void **slot;
	void *orig;
};

static LIST_HEAD(g_ptrs);
/* the entries whose restore failed, kept for hk_ptr_exit to retry */
static LIST_HEAD(g_ptr_broken);
static DEFINE_SPINLOCK(g_ptr_lock);

unsigned int hk_ptr_pending(void)
{
	unsigned int count = 0;
	struct hk_ptr_entry *ent;
	unsigned long flags;

	spin_lock_irqsave(&g_ptr_lock, flags);
	list_for_each_entry(ent, &g_ptr_broken, list)
		count++;
	spin_unlock_irqrestore(&g_ptr_lock, flags);
	return count;
}

int hk_ptr_hook(void **slot, void *replacement, void **orig_out)
{
	struct hk_ptr_entry *ent;
	struct hk_ptr_entry *new;
	unsigned long flags;
	void *orig;
	int ret;

	if (!slot || !replacement)
		return -EINVAL;

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	/*
	 * the entry is reserved before the write and the lock is dropped for it,
	 * because the write opens a fixmap and sleeps. the slot is taken from
	 * here, so a second hook cannot race the write and lose an original, and
	 * a failed write drops the reservation again
	 */
	spin_lock_irqsave(&g_ptr_lock, flags);
	list_for_each_entry(ent, &g_ptrs, list) {
		if (ent->slot != slot)
			continue;
		spin_unlock_irqrestore(&g_ptr_lock, flags);
		kfree(new);
		return -EEXIST;
	}
	list_for_each_entry(ent, &g_ptr_broken, list) {
		if (ent->slot != slot)
			continue;
		spin_unlock_irqrestore(&g_ptr_lock, flags);
		kfree(new);
		return -EEXIST;
	}
	if (copy_from_kernel_nofault(&orig, slot, sizeof(orig))) {
		spin_unlock_irqrestore(&g_ptr_lock, flags);
		kfree(new);
		return -EFAULT;
	}
	new->slot = slot;
	new->orig = orig;
	list_add(&new->list, &g_ptrs);
	spin_unlock_irqrestore(&g_ptr_lock, flags);

	ret = hk_patch_write(slot, (unsigned long)replacement);
	if (!ret) {
		void *now;

		/*
		 * the slot is read back before the hook is reported as taken: a write a
		 * platform swallowed leaves the pointer where it was, and a caller has to
		 * hear about that rather than believe the hook is in place
		 */
		if (copy_from_kernel_nofault(&now, slot, sizeof(now)) ||
		    now != replacement)
			ret = -EIO;
	}
	if (ret) {
		spin_lock_irqsave(&g_ptr_lock, flags);
		list_del(&new->list);
		spin_unlock_irqrestore(&g_ptr_lock, flags);
		kfree(new);
		return ret;
	}

	if (orig_out)
		*orig_out = orig;
	pr_info("[lkmhook] ptr hook %px -> %ps\n", slot, replacement);
	return 0;
}

void hk_ptr_unhook(void **slot)
{
	struct hk_ptr_entry *ent;
	struct hk_ptr_entry *tmp;
	struct hk_ptr_entry *found = NULL;
	unsigned long flags;
	int ret;

	if (!slot)
		return;

	/*
	 * the entry leaves the live list before the write, so a hook installed
	 * while the restore sleeps takes the slot, reads the replacement as its
	 * own original and is correct. a restore that fails is kept for the exit
	 * gate instead of being dropped, the slot still points at module memory
	 */
	spin_lock_irqsave(&g_ptr_lock, flags);
	list_for_each_entry_safe(ent, tmp, &g_ptrs, list) {
		if (ent->slot != slot)
			continue;
		list_del(&ent->list);
		found = ent;
		break;
	}
	spin_unlock_irqrestore(&g_ptr_lock, flags);

	if (!found)
		return;
	ret = hk_patch_write(slot, (unsigned long)found->orig);
	if (ret) {
		pr_warn("[lkmhook] ptr unhook %px failed %d\n", slot, ret);
		spin_lock_irqsave(&g_ptr_lock, flags);
		list_add(&found->list, &g_ptr_broken);
		spin_unlock_irqrestore(&g_ptr_lock, flags);
		return;
	}
	kfree(found);
}

void hk_ptr_exit(void)
{
	struct hk_ptr_entry *ent;
	struct hk_ptr_entry *tmp;
	LIST_HEAD(work);
	unsigned long flags;

	spin_lock_irqsave(&g_ptr_lock, flags);
	list_splice_init(&g_ptrs, &work);
	list_splice_init(&g_ptr_broken, &work);
	spin_unlock_irqrestore(&g_ptr_lock, flags);

	list_for_each_entry_safe(ent, tmp, &work, list) {
		list_del(&ent->list);
		if (hk_patch_write(ent->slot, (unsigned long)ent->orig)) {
			pr_warn("[lkmhook] ptr restore %px failed\n", ent->slot);
			spin_lock_irqsave(&g_ptr_lock, flags);
			list_add(&ent->list, &g_ptr_broken);
			spin_unlock_irqrestore(&g_ptr_lock, flags);
			continue;
		}
		kfree(ent);
	}
}
