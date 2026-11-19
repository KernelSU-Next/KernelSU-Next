// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/lsm_hooks.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/rcupdate.h>
#include <linux/string.h>

#include <linux/static_call.h>

#include "hk.h"
#include "hk_lsm.h"
#include "hk_patch.h"

struct hk_lsm_track {
	struct list_head list;
	struct hk_lsm_hook *hook;
};

static DEFINE_MUTEX(hk_lsm_lock);
static LIST_HEAD(hk_lsm_tracked);
static struct hk_lsm_layout g_layout_store;
static const struct hk_lsm_layout *g_layout;
static int hk_lsm_count;

static unsigned long hk_lsm_resolve_one(const char *name)
{
	if (g_layout && g_layout->resolve)
		return g_layout->resolve(name);
	return hk_resolve(name);
}

static unsigned long hk_lsm_resolve(const char *name)
{
	char buf[128];
	unsigned long addr;

	snprintf(buf, sizeof(buf), "%s.cfi_jt", name);
	addr = hk_lsm_resolve_one(buf);
	if (addr)
		return addr;

	snprintf(buf, sizeof(buf), "%s$", name);
	addr = hk_lsm_resolve_one(buf);
	if (addr)
		return addr;

	return hk_lsm_resolve_one(name);
}

static int hk_lsm_size(const char *type, size_t *size)
{
	u32 id;

	if (!g_layout || !g_layout->type_by_name || !g_layout->type_size)
		return -EOPNOTSUPP;
	if (g_layout->type_by_name(type, &id))
		return -ENOENT;
	*size = g_layout->type_size(id);
	return *size ? 0 : -ENOENT;
}

static int hk_lsm_off(const char *type, const char *member, size_t *off)
{
	u32 bit_off;
	u32 bit_sz;
	u32 id;
	int ret;

	if (!g_layout || !g_layout->type_by_name || !g_layout->member_off)
		return -EOPNOTSUPP;
	ret = g_layout->type_by_name(type, &id);
	if (ret)
		return ret;
	ret = g_layout->member_off(id, member, &bit_off, &bit_sz);
	if (ret)
		return ret;
	*off = bit_off / 8;
	return 0;
}

static bool hk_lsm_is_tracked(struct hk_lsm_hook *hook)
{
	struct hk_lsm_track *track;

	list_for_each_entry(track, &hk_lsm_tracked, list) {
		if (track->hook == hook)
			return true;
	}
	return false;
}

/* a node per live hook, the list has no ceiling */
static int hk_lsm_track(struct hk_lsm_hook *hook)
{
	struct hk_lsm_track *track;

	if (hk_lsm_is_tracked(hook))
		return 0;
	track = kzalloc(sizeof(*track), GFP_KERNEL);
	if (!track)
		return -ENOMEM;
	track->hook = hook;
	list_add(&track->list, &hk_lsm_tracked);
	hk_lsm_count++;
	return 0;
}

static void hk_lsm_untrack(struct hk_lsm_hook *hook)
{
	struct hk_lsm_track *track;
	struct hk_lsm_track *tmp;

	list_for_each_entry_safe(track, tmp, &hk_lsm_tracked, list) {
		if (track->hook != hook)
			continue;
		list_del(&track->list);
		kfree(track);
		hk_lsm_count--;
		return;
	}
}

static int hk_lsm_patch_slot(void **slot, void *value)
{
	void *patched = value;
	int ret;

	ret = hk_write_text(slot, &patched, sizeof(patched));
	if (!ret)
		smp_wmb();
	return ret;
}

static int hk_lsm_update_scall(void *scall,
			       size_t key_off, size_t tramp_off, void *value)
{
	void *key;
	void *tramp;

	key = *(void **)((char *)scall + key_off);
	tramp = *(void **)((char *)scall + tramp_off);
	__static_call_update((struct static_call_key *)key, tramp, value);
	smp_wmb();
	return 0;
}

/*
 * the offset of a member inside the head object is not something a kernel without a
 * type source can answer, and a kernel that answers nothing here still publishes the
 * object itself. the head is then found by walking the candidates and asking which
 * one carries the target: the hook list keeps its shape across the kernels this
 * library supports, and every read goes through the nofault copy so a wrong guess
 * costs a return value and not a fault
 */
#define HK_LSM_SCAN_HEADS 256
#define HK_LSM_SCAN_DEPTH 64
/* slots of one hook, the kernel ceiling is its configured lsm count */
#define HK_LSM_SCAN_SLOTS 32

static struct hlist_head *hk_lsm_scan_head(unsigned long heads, void *target,
					   size_t hook_off)
{
	size_t i;

	for (i = 0; i < HK_LSM_SCAN_HEADS; i++) {
		unsigned long head = heads + i * sizeof(struct hlist_head);
		unsigned long node;
		unsigned int j;

		if (copy_from_kernel_nofault(&node, (void *)head, sizeof(node)))
			return NULL;
		for (j = 0; j < HK_LSM_SCAN_DEPTH && node; j++) {
			struct hlist_node *list = (struct hlist_node *)node;
			struct hlist_node next;
			void *hook;

			if (copy_from_kernel_nofault(&next, list, sizeof(next)))
				break;
			if (copy_from_kernel_nofault(&hook, (char *)node + hook_off,
						     sizeof(hook)))
				break;
			if (hook == target)
				return (struct hlist_head *)head;
			node = (unsigned long)next.next;
		}
	}
	return NULL;
}

int hk_lsm_hook(struct hk_lsm_hook *hook)
{
	int ret = 0;
	struct security_hook_list *entry;
	struct hlist_head *head;
	struct hlist_head *head_end;
	struct hlist_node *node;
	struct hk_lsm_track *track;
	struct security_hook_list *selected_entry = NULL;
	void **selected_slot = NULL;
	void *selected_origin = NULL;
	void *selected_scall = NULL;
	unsigned long heads_addr;
	unsigned long scalls_addr;
	unsigned long active_addr;
	const char *target_name;
	size_t heads_size = 0;
	size_t scalls_size = 0;
	size_t scall_key_off = 0;
	size_t scall_tramp_off = 0;
	size_t scall_hl_off = 0;
	size_t scall_active_off = 0;
	size_t scall_size = 0;
	size_t scalls_count = 0;
	size_t head_off;
	size_t hook_off;
	size_t lsm_max_cnt = 5;
	u32 active_cnt = 5;
	void *target;
	void *scalls = NULL;
	size_t i;

	if (!hook || !hook->replacement)
		return -EINVAL;

	mutex_lock(&hk_lsm_lock);
	if (hook->entry) {
		ret = -EALREADY;
		goto out_unlock;
	}

	target_name = hook->target_name;
	if (!target_name) {
		ret = -EINVAL;
		goto out_unlock;
	}

	target = hook->original;
	if (!target)
		target = (void *)hk_lsm_resolve(target_name);
	if (!target) {
		ret = -ENOENT;
		goto out_unlock;
	}

	hook_off = hook->hook_offset;
	hk_lsm_off("security_hook_list", "hook", &hook_off);

	/*
	 * which route this kernel has is a runtime question: one that publishes the
	 * head object still walks a list, one that does not has moved to static
	 * calls. the two are exclusive and nothing here asks the version
	 */
	heads_addr = hk_lsm_resolve("security_hook_heads");
	if (heads_addr) {
		head_off = hook->head_offset;
		if (hk_lsm_off("security_hook_heads", hook->head_name, &head_off)) {
			/*
			 * no type source answered, so the head is the one that carries
			 * the target. the layout stays the first choice because it names
			 * the member, and this is the state a kernel without one runs in
			 */
			head = hk_lsm_scan_head(heads_addr, target, hook_off);
			if (!head) {
				ret = -ENODATA;
				goto out_unlock;
			}
			head_end = head + 1;
		} else {
			head = (struct hlist_head *)(heads_addr + head_off);
			/*
			 * the walk is bounded by the object's size when the layout knows
			 * it, and by a fixed ceiling when it does not: the last head is
			 * reached by walking rather than by knowing where the object ends
			 */
			if (hk_lsm_size("security_hook_heads", &heads_size) ||
			    !heads_size)
				heads_size = HK_LSM_SCAN_HEADS *
					     sizeof(struct hlist_head);
			head_end = (struct hlist_head *)(heads_addr + heads_size);
		}

		for (; head < head_end; head++) {
			/*
			 * the walk is over the node type rather than the kernel's entry
			 * type: the entry's first word is the list node on every kernel
			 * this library supports, and its second is the hook
			 */
			hlist_for_each(node, head) {
				void **slot = (void **)((char *)node + hook_off);
				void *current_origin = READ_ONCE(*slot);

				list_for_each_entry(track, &hk_lsm_tracked, list) {
					if (track->hook->replacement ==
					    current_origin) {
						current_origin =
							track->hook->original;
						break;
					}
				}
				if (current_origin == hook->replacement) {
					ret = -EALREADY;
					goto out_unlock;
				}
				if (current_origin == target) {
					selected_entry =
						(struct security_hook_list *)node;
					selected_slot = slot;
					selected_origin = current_origin;
					break;
				}
			}
			if (!selected_entry)
				continue;
			if (!hook->offset)
				break;
			head += hook->offset;
			if (head < (struct hlist_head *)heads_addr ||
			    head >= head_end) {
				ret = -EINVAL;
				goto out_unlock;
			}
			hlist_for_each(node, head) {
				void **slot = (void **)((char *)node + hook_off);

				if (READ_ONCE(*slot) == hook->replacement) {
					ret = -EALREADY;
					goto out_unlock;
				}
			}
			if (head->first) {
				/* the node is the first word of the entry */
				selected_entry =
					(struct security_hook_list *)head->first;
				selected_slot = (void **)((char *)selected_entry +
							  hook_off);
				selected_origin = *selected_slot;
			} else {
				selected_entry =
					(struct security_hook_list *)&hook->ours;
				hook->ours.node.next = NULL;
				hook->ours.node.pprev = &head->first;
				hook->ours.hook = hook->replacement;
				selected_slot = &hook->ours.hook;
				selected_origin = NULL;
			}
			break;
		}
		if (!selected_entry) {
			ret = -ENOENT;
			goto out_unlock;
		}
	} else {
		/*
		 * the static call route needs the shape of two structures. a kernel
		 * without a type source cannot give it, so the route is not offered
		 * there instead of guessed at. the answers are what is checked, key
		 * sits at offset zero and a zero offset is not a missing one
		 */
		if (hk_lsm_off("lsm_static_call", "key", &scall_key_off) ||
		    hk_lsm_off("lsm_static_call", "trampoline",
			       &scall_tramp_off) ||
		    hk_lsm_off("lsm_static_call", "hl", &scall_hl_off) ||
		    hk_lsm_off("lsm_static_call", "active", &scall_active_off) ||
		    hk_lsm_size("lsm_static_call", &scall_size) ||
		    hk_lsm_size("lsm_static_calls_table", &scalls_size)) {
			ret = -EOPNOTSUPP;
			goto out_unlock;
		}
		scalls_addr = hk_lsm_resolve("static_calls_table");
		if (!scalls_addr) {
			ret = -ENOSYS;
			goto out_unlock;
		}
		active_addr = hk_lsm_resolve("lsm_active_cnt");
		if (active_addr) {
			active_cnt = *(u32 *)active_addr;
			if (!active_cnt || active_cnt > 20) {
				ret = -EINVAL;
				goto out_unlock;
			}
		}
		scalls = (void *)scalls_addr;
		scalls_count = scalls_size / scall_size;
		lsm_max_cnt = active_cnt;

		for (i = 0; i < scalls_count; i++) {
			void *scall = (char *)scalls + i * scall_size;
			void **slot;
			void *current_origin;

			entry = READ_ONCE(*(struct security_hook_list **)
					  ((char *)scall + scall_hl_off));
			if (!entry)
				continue;

			slot = (void **)((char *)entry + hook_off);
			current_origin = READ_ONCE(*slot);

			list_for_each_entry(track, &hk_lsm_tracked, list) {
				if (track->hook->replacement == current_origin) {
					current_origin = track->hook->original;
					break;
				}
			}
			if (current_origin == hook->replacement) {
				ret = -EALREADY;
				goto out_unlock;
			}
			if (current_origin != target)
				continue;

			if (hook->offset) {
				size_t hook_idx = (i / lsm_max_cnt +
						   hook->offset) * lsm_max_cnt;

				if (hook_idx >= scalls_count) {
					ret = -EINVAL;
					goto out_unlock;
				}
				scall = (char *)scalls + hook_idx * scall_size;
				entry = READ_ONCE(*(struct security_hook_list **)
						  ((char *)scall + scall_hl_off));
				if (!entry) {
					/* an empty slot is what an insert takes */
					ret = -EOPNOTSUPP;
					goto out_unlock;
				}
				slot = (void **)((char *)entry + hook_off);
				current_origin = READ_ONCE(*slot);
				if (current_origin == hook->replacement) {
					ret = -EALREADY;
					goto out_unlock;
				}
			}

			selected_entry = entry;
			selected_scall = scall;
			selected_slot = slot;
			selected_origin = current_origin;
			break;
		}
		if (!selected_scall) {
			ret = -ENOENT;
			goto out_unlock;
		}
	}

	ret = hk_lsm_track(hook);
	if (ret)
		goto out_unlock;

	if (selected_origin)
		ret = hk_lsm_patch_slot(selected_slot, hook->replacement);
	else
		ret = hk_lsm_patch_slot(selected_slot, &hook->ours);
	if (ret) {
		ret = -EFAULT;
		goto out_untrack;
	}
	if (selected_scall &&
	    hk_lsm_update_scall(selected_scall, scall_key_off, scall_tramp_off,
				hook->replacement)) {
		hk_lsm_patch_slot(selected_slot, selected_origin);
		ret = -EFAULT;
		goto out_untrack;
	}
	if (selected_scall && !selected_origin)
		static_branch_enable(*(struct static_key_false **)
				     ((char *)selected_scall +
				      scall_active_off));

	hook->entry = selected_entry;
	hook->scall = selected_scall;
	hook->original = selected_origin;
	pr_info("[lkmhook] lsm patched %s\n", hook->head_name);
	goto out_unlock;

out_untrack:
	hk_lsm_untrack(hook);

out_unlock:
	mutex_unlock(&hk_lsm_lock);
	return ret;
}

/* the table is ro_after_init, so a taken slot is remembered here and not in it */
static bool hk_lsm_scall_taken(void *scall)
{
	struct hk_lsm_track *track;

	list_for_each_entry(track, &hk_lsm_tracked, list) {
		if (track->hook->inserted && track->hook->scall == scall)
			return true;
	}
	return false;
}

/*
 * take one free static call slot of this hook: the kernel fills the array from the
 * first free slot the same way when it registers an lsm, see lsm_static_call_init.
 * the call is written first and the branch is turned on after it, so no walker can
 * reach a slot that is not ready yet
 */
int hk_lsm_insert(struct hk_lsm_hook *hook)
{
	struct security_hook_list *entry = NULL;
	size_t scall_key_off = 0;
	size_t scall_tramp_off = 0;
	size_t scall_hl_off = 0;
	size_t scall_active_off = 0;
	size_t entry_size = 0;
	size_t scall_size = 0;
	size_t head_off = 0;
	size_t hook_off;
	void *scalls;
	void *scall = NULL;
	void *array;
	size_t i;
	int ret;

	if (!hook || !hook->replacement || !hook->head_name)
		return -EINVAL;

	mutex_lock(&hk_lsm_lock);
	if (hook->entry) {
		ret = -EALREADY;
		goto out_unlock;
	}

	hook_off = hook->hook_offset;
	hk_lsm_off("security_hook_list", "hook", &hook_off);
	if (!hook_off ||
	    hk_lsm_size("security_hook_list", &entry_size) || !entry_size ||
	    hk_lsm_size("lsm_static_call", &scall_size) || !scall_size ||
	    hk_lsm_off("lsm_static_call", "key", &scall_key_off) ||
	    hk_lsm_off("lsm_static_call", "trampoline", &scall_tramp_off) ||
	    hk_lsm_off("lsm_static_call", "hl", &scall_hl_off) ||
	    hk_lsm_off("lsm_static_call", "active", &scall_active_off) ||
	    hk_lsm_off("lsm_static_calls_table", hook->head_name, &head_off)) {
		ret = -EOPNOTSUPP;
		goto out_unlock;
	}

	/* the list route has no slot to take, so an insert is not offered there */
	scalls = (void *)hk_lsm_resolve("static_calls_table");
	if (!scalls) {
		ret = -EOPNOTSUPP;
		goto out_unlock;
	}

	array = (char *)scalls + head_off;
	for (i = 0; i < HK_LSM_SCAN_SLOTS; i++) {
		void *candidate = (char *)array + i * scall_size;

		/* hl is written by the kernel only while it initializes, so it stays null */
		if (READ_ONCE(*(void **)((char *)candidate + scall_hl_off)))
			continue;
		if (hk_lsm_scall_taken(candidate))
			continue;
		scall = candidate;
		break;
	}
	if (!scall) {
		ret = -ENOSPC;
		goto out_unlock;
	}

	entry = kzalloc(entry_size, GFP_KERNEL);
	if (!entry) {
		ret = -ENOMEM;
		goto out_unlock;
	}
	*(void **)((char *)entry + hook_off) = hook->replacement;

	ret = hk_lsm_track(hook);
	if (ret) {
		kfree(entry);
		goto out_unlock;
	}

	if (hk_lsm_update_scall(scall, scall_key_off, scall_tramp_off,
				hook->replacement)) {
		ret = -EFAULT;
		goto out_untrack;
	}
	static_branch_enable(*(struct static_key_false **)
			     ((char *)scall + scall_active_off));

	hook->entry = entry;
	hook->scall = scall;
	hook->inserted = true;
	pr_info("[lkmhook] lsm inserted %s\n", hook->head_name);
	goto out_unlock;

out_untrack:
	hk_lsm_untrack(hook);
	kfree(entry);
out_unlock:
	mutex_unlock(&hk_lsm_lock);
	return ret;
}

/* the caller holds the lock */
static void hk_lsm_remove_inserted(struct hk_lsm_hook *hook)
{
	size_t scall_key_off = 0;
	size_t scall_tramp_off = 0;
	size_t scall_active_off = 0;
	struct security_hook_list *entry = hook->entry;
	struct static_key_false *active;

	hk_lsm_off("lsm_static_call", "key", &scall_key_off);
	hk_lsm_off("lsm_static_call", "trampoline", &scall_tramp_off);
	hk_lsm_off("lsm_static_call", "active", &scall_active_off);

	active = *(struct static_key_false **)((char *)hook->scall +
					       scall_active_off);
	static_branch_disable(active);
	/* the branch is off, what is left is a call that is already inside the hook */
	hk_wait_tasks_rcu();
	hk_lsm_update_scall(hook->scall, scall_key_off, scall_tramp_off, NULL);
	kfree(entry);
	hk_lsm_untrack(hook);
	hook->entry = NULL;
	hook->scall = NULL;
	hook->inserted = false;
	pr_info("[lkmhook] lsm inserted removed %s\n",
		hook->head_name ?: "unknown");
}

/* the caller holds the lock */
static void hk_lsm_unhook_locked(struct hk_lsm_hook *hook)
{
	size_t hook_off;
	void **slot;
	size_t scall_key_off = 0;
	size_t scall_tramp_off = 0;

	if (hook->inserted) {
		hk_lsm_remove_inserted(hook);
		return;
	}

	hook_off = hook->hook_offset;
	hk_lsm_off("security_hook_list", "hook", &hook_off);

	hk_lsm_off("lsm_static_call", "key", &scall_key_off);
	hk_lsm_off("lsm_static_call", "trampoline", &scall_tramp_off);

	if (!hook->entry) {
		return;
	}
	/* a hook that went in through a static call says so with this field */
	if (hook->scall)
		slot = (void **)((char *)hook->entry + hook_off);
	else if (hook->entry == (struct security_hook_list *)&hook->ours)
		slot = (void **)&hook->ours.node.pprev;
	else
		slot = (void **)((char *)hook->entry + hook_off);

	if (hk_lsm_patch_slot(slot, hook->original)) {
		pr_warn("[lkmhook] lsm restore failed %s\n",
			hook->head_name ?: "unknown");
		return;
	}

	if (hook->scall &&
	    hk_lsm_update_scall(hook->scall, scall_key_off, scall_tramp_off,
				hook->original)) {
		hk_lsm_patch_slot(slot, hook->replacement);
		return;
	}

	synchronize_rcu();
	hk_lsm_untrack(hook);
	hook->entry = NULL;
	hook->scall = NULL;
	pr_info("[lkmhook] lsm restored %s\n", hook->head_name ?: "unknown");
}


void hk_lsm_unhook(struct hk_lsm_hook *hook)
{
	mutex_lock(&hk_lsm_lock);
	hk_lsm_unhook_locked(hook);
	mutex_unlock(&hk_lsm_lock);
}

int hk_lsm_register(struct hk_lsm_hook *hook)
{
	return hk_lsm_hook(hook);
}

void hk_lsm_unregister(struct hk_lsm_hook *hook)
{
	hk_lsm_unhook(hook);
}

int hk_lsm_init(const struct hk_lsm_layout *layout)
{
	/*
	 * the layout is copied, a caller that names one on its stack keeps the
	 * installs working after the frame is gone
	 */
	if (layout) {
		g_layout_store = *layout;
		g_layout = &g_layout_store;
	} else {
		g_layout = NULL;
	}
	pr_info("[lkmhook] lsm init tracked=%d\n", READ_ONCE(hk_lsm_count));
	return 0;
}

void hk_lsm_exit(void)
{
	struct hk_lsm_track *track;
	struct hk_lsm_track *tmp;

	mutex_lock(&hk_lsm_lock);
	list_for_each_entry_safe(track, tmp, &hk_lsm_tracked, list)
		hk_lsm_unhook_locked(track->hook);
	mutex_unlock(&hk_lsm_lock);
	g_layout = NULL;
}
