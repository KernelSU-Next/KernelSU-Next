/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sus_mount_ns_iter.c - P-owned namespace mount iterator
 *
 * The iterator takes the same lock pair the kernel's own mount-table readers
 * take: rcu_read_lock() plus ns_lock below 6.12, or namespace_sem for read
 * from 6.12 on.  It filters cursor/stale mounts and stays inside @ns.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/rcupdate.h>
#include <linux/rwsem.h>
#include <linux/version.h>

#include "mount.h"
#include "sus_mount_ns_iter.h"

struct sus_mount_ns_iter {
	struct mnt_namespace *ns;
	void *pos;
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
static struct rw_semaphore *sus_mount_ns_sem;

void sus_mount_ns_iter_set_sem(void *sem)
{
	sus_mount_ns_sem = sem;
}
#else
void sus_mount_ns_iter_set_sem(void *sem)
{
	(void)sem;
}
#endif

struct sus_mount_ns_iter *sus_mount_ns_iter_begin(struct mnt_namespace *ns)
{
	struct sus_mount_ns_iter *iter;

	if (!ns)
		return NULL;
	iter = kzalloc(sizeof(*iter), GFP_ATOMIC);
	if (!iter)
		return NULL;
	iter->ns = ns;
	rcu_read_lock();
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	if (!sus_mount_ns_sem) {
		rcu_read_unlock();
		kfree(iter);
		return NULL;
	}
	down_read(sus_mount_ns_sem);
	iter->pos = rb_first(&ns->mounts);
#else
	spin_lock(&ns->ns_lock);
	iter->pos = ns->list.next;
#endif
	return iter;
}

static bool sus_mount_ns_iter_usable(struct sus_mount_ns_iter *iter,
				     struct mount *r)
{
	if (r->mnt_ns != iter->ns)
		return false;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
	return mnt_ns_attached(r);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	return !RB_EMPTY_NODE(&r->mnt_node);
#else
	return !(r->mnt.mnt_flags & MNT_CURSOR);
#endif
}

struct mount *sus_mount_ns_iter_next(struct sus_mount_ns_iter *iter)
{
	if (!iter || !iter->pos)
		return NULL;
	for (;;) {
		struct mount *r;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
		struct rb_node *node = iter->pos;

		iter->pos = rb_next(node);
		r = rb_entry(node, struct mount, mnt_node);
#else
		struct list_head *pos = iter->pos;

		if (pos == &iter->ns->list) {
			iter->pos = NULL;
			return NULL;
		}
		iter->pos = pos->next;
		r = list_entry(pos, struct mount, mnt_list);
#endif
		if (sus_mount_ns_iter_usable(iter, r))
			return r;
		if (!iter->pos)
			return NULL;
	}
}

void sus_mount_ns_iter_end(struct sus_mount_ns_iter *iter)
{
	if (!iter)
		return;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	up_read(sus_mount_ns_sem);
#else
	spin_unlock(&iter->ns->ns_lock);
#endif
	rcu_read_unlock();
	kfree(iter);
}
