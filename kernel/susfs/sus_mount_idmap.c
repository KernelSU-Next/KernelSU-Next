/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sus_mount_idmap.c - sus_mount id -> shown-id records
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/string.h>

#include "sus_mount_idmap.h"

#define SUS_MOUNT_IDMAP_MAX 1024

struct sus_mount_idmap_entry {
	int sus_id;
	int shown_id;
	dev_t s_dev;
};

static struct sus_mount_idmap_entry mount_idmap[SUS_MOUNT_IDMAP_MAX];
static int n_idmap;
static DEFINE_SPINLOCK(idmap_lock);
static atomic_t n_idmap_recycled = ATOMIC_INIT(0);
static atomic_t n_idmap_dropped_dev = ATOMIC_INIT(0);
static atomic_t n_idmap_full = ATOMIC_INIT(0);

void sus_mount_idmap_add(int sus_id, int shown_id, dev_t s_dev)
{
	unsigned long flags;
	int lo, hi, pos;

	if (sus_id <= 0 || shown_id <= 0)
		return;
	spin_lock_irqsave(&idmap_lock, flags);
	lo = 0; hi = n_idmap;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if (mount_idmap[mid].sus_id < sus_id)
			lo = mid + 1;
		else
			hi = mid;
	}
	pos = lo;
	if (pos < n_idmap && mount_idmap[pos].sus_id == sus_id) {
		mount_idmap[pos].shown_id = shown_id;
		mount_idmap[pos].s_dev = s_dev;
		goto out;
	}
	if (n_idmap >= SUS_MOUNT_IDMAP_MAX) {
		if (atomic_inc_return(&n_idmap_full) == 1)
			pr_warn("sus_mount: id map full at %d entries - further marked mounts keep their KSU-range id in statx(STATX_MNT_ID) and fdinfo; raise SUS_MOUNT_IDMAP_MAX\n",
				SUS_MOUNT_IDMAP_MAX);
		goto out;
	}
	memmove(&mount_idmap[pos + 1], &mount_idmap[pos],
		(n_idmap - pos) * sizeof(mount_idmap[0]));
	mount_idmap[pos].sus_id = sus_id;
	mount_idmap[pos].shown_id = shown_id;
	mount_idmap[pos].s_dev = s_dev;
	n_idmap++;
out:
	spin_unlock_irqrestore(&idmap_lock, flags);
}

void sus_mount_idmap_drop_dev(dev_t s_dev)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&idmap_lock, flags);
	for (i = 0; i < n_idmap; ) {
		if (mount_idmap[i].s_dev == s_dev) {
			memmove(&mount_idmap[i], &mount_idmap[i + 1],
				(n_idmap - i - 1) * sizeof(mount_idmap[0]));
			n_idmap--;
			atomic_inc(&n_idmap_dropped_dev);
		} else {
			i++;
		}
	}
	spin_unlock_irqrestore(&idmap_lock, flags);
}

void sus_mount_idmap_drop(int sus_id)
{
	unsigned long flags;
	int lo, hi, pos;

	if (sus_id <= 0)
		return;
	spin_lock_irqsave(&idmap_lock, flags);
	lo = 0; hi = n_idmap;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if (mount_idmap[mid].sus_id < sus_id)
			lo = mid + 1;
		else
			hi = mid;
	}
	pos = lo;
	if (pos < n_idmap && mount_idmap[pos].sus_id == sus_id) {
		memmove(&mount_idmap[pos], &mount_idmap[pos + 1],
			(n_idmap - pos - 1) * sizeof(mount_idmap[0]));
		n_idmap--;
		atomic_inc(&n_idmap_recycled);
	}
	spin_unlock_irqrestore(&idmap_lock, flags);
}

int sus_mount_idmap_shown_for(int sus_id)
{
	unsigned long flags;
	int lo, hi, shown = 0;

	if (sus_id <= 0)
		return 0;
	spin_lock_irqsave(&idmap_lock, flags);
	lo = 0; hi = n_idmap;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if (mount_idmap[mid].sus_id < sus_id)
			lo = mid + 1;
		else
			hi = mid;
	}
	if (lo < n_idmap && mount_idmap[lo].sus_id == sus_id)
		shown = mount_idmap[lo].shown_id;
	spin_unlock_irqrestore(&idmap_lock, flags);
	return shown;
}

void sus_mount_idmap_reset(void)
{
	unsigned long flags;

	spin_lock_irqsave(&idmap_lock, flags);
	memset(mount_idmap, 0, sizeof(mount_idmap));
	n_idmap = 0;
	spin_unlock_irqrestore(&idmap_lock, flags);
}

int sus_mount_idmap_live(void)
{
	unsigned long flags;
	int i, live = 0;

	spin_lock_irqsave(&idmap_lock, flags);
	for (i = 0; i < n_idmap; i++)
		if (mount_idmap[i].sus_id)
			live++;
	spin_unlock_irqrestore(&idmap_lock, flags);
	return live;
}

int sus_mount_idmap_used(void)
{
	return READ_ONCE(n_idmap);
}

int sus_mount_idmap_recycled(void)
{
	return atomic_read(&n_idmap_recycled);
}

int sus_mount_idmap_dropped_dev(void)
{
	return atomic_read(&n_idmap_dropped_dev);
}

int sus_mount_idmap_full(void)
{
	return atomic_read(&n_idmap_full);
}
