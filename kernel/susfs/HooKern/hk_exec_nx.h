/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HK_EXEC_NX_H
#define HK_EXEC_NX_H

/* ARM64 descriptor policy, shared by the kernel adapter and host tests.
 * The caller pins the mapping and excludes page-table writers throughout.
 * Only PXN is relaxed; UXN, GP, AP, addresses and descriptor types survive.
 */
#ifdef __KERNEL__
#include <linux/errno.h>
#else
#include <errno.h>
#endif

#define HK_NX_LEAF (1ULL << 53)
#define HK_NX_TABLE (1ULL << 59)
#define HK_NX_LEVELS 5
#define HK_NX_SCAN_MAX 65536U

typedef unsigned long long hk_nx_desc;

struct hk_nx_ops {
	hk_nx_desc (*read)(void *entry);
	void *(*child)(hk_nx_desc desc);
	void *(*entry)(void *table, unsigned int index);
	int (*write)(void *entry, hk_nx_desc desc);
	void (*flush)(void);
};

struct hk_nx_step {
	void *entry;
	hk_nx_desc old;
	hk_nx_desc mask;
	unsigned int shift;
};

struct hk_nx_request {
	const struct hk_nx_ops *ops;
	void *root;
	hk_nx_desc addr;
	unsigned int root_shift;
	unsigned int page_shift;
	unsigned int cleared;
	unsigned int tables_cleared;
	int rollback_error;
	/* Why the last run ended in -EIO: 1 = the read-back right after one
	 * store disagreed (the store did not land), 2 = the read-back of the
	 * whole path disagreed (a store landed and was restored).  Both are
	 * -EIO; this field is the only difference visible in the log. */
	int eio_stage;
};

/* Ignore the parent's PXN: every other currently valid mapping must still
 * be non-executable without it. A child's table PXN protects its subtree.
 * Never weaken neighbours or recursively rewrite their permissions.
 */
static inline int hk_nx_neighbours(const struct hk_nx_ops *ops, void *table,
		unsigned int shift, unsigned int page_shift, void *target,
		unsigned int *budget)
{
	unsigned int i, entries = 1U << (page_shift - 3);

	if (!table)
		return -EFAULT;
	for (i = 0; i < entries; i++) {
		void *entry = ops->entry(table, i);
		hk_nx_desc desc;
		int ret;

		if (!*budget)
			return -E2BIG;
		--*budget;
		desc = ops->read(entry);
		if (!(desc & 1) || entry == target)
			continue;
		if (shift == page_shift || (desc & 3) == 1) {
			if (!(desc & HK_NX_LEAF))
				return -EPERM;
			continue;
		}
		if (desc & HK_NX_TABLE)
			continue;
		ret = hk_nx_neighbours(ops, ops->child(desc),
				shift - (page_shift - 3), page_shift,
				target, budget);
		if (ret)
			return ret;
	}
	return 0;
}

/* One fresh, unpublished allocation page. Folded levels are absent from the
 * hardware walk: root_shift/page_shift describe the actual build geometry.
 * Successful table relaxations are not later restored by vfree: other live
 * allocations may then use them. Their leaf permissions remain authoritative.
 */
static inline int hk_nx_clear_page(struct hk_nx_request *req)
{
	struct hk_nx_step path[HK_NX_LEVELS];
	const struct hk_nx_ops *ops = req->ops;
	unsigned int depth = 0, shift = req->root_shift, i, budget;
	unsigned int stride, entries, changed = 0;
	void *entry = req->root;
	int ret = 0;

	req->cleared = 0;
	req->tables_cleared = 0;
	req->rollback_error = 0;
	if (req->page_shift < 12 || req->page_shift > 16 ||
	    shift < req->page_shift || shift >= 64)
		return -EINVAL;
	stride = req->page_shift - 3;
	if ((shift - req->page_shift) % stride ||
	    (req->addr & ((1ULL << req->page_shift) - 1)))
		return -EINVAL;
	entries = 1U << stride;
	for (;;) {
		hk_nx_desc desc;
		void *table;

		if (!entry || depth == HK_NX_LEVELS)
			return -EFAULT;
		desc = ops->read(entry);
		/* No absent entries or block mappings on the trampoline path. */
		if ((desc & 3) != 3)
			return -EFAULT;
		path[depth].entry = entry;
		path[depth].old = desc;
		path[depth].mask = shift == req->page_shift ?
			HK_NX_LEAF : HK_NX_TABLE;
		path[depth++].shift = shift;
		if (shift == req->page_shift)
			break;
		table = ops->child(desc);
		if (!table)
			return -EFAULT;
		shift -= stride;
		entry = ops->entry(table, (req->addr >> shift) & (entries - 1));
	}

	budget = HK_NX_SCAN_MAX;
	for (i = 0; i + 1 < depth; i++) {
		if (!(path[i].old & HK_NX_TABLE))
			continue;
		ret = hk_nx_neighbours(ops, ops->child(path[i].old),
			path[i].shift - stride, req->page_shift, entry, &budget);
		if (ret)
			return ret;
	}

	/* Prepare everything before the first store. Unmask from leaf to root. */
	for (i = depth; i-- > 0;) {
		hk_nx_desc want = path[i].old & ~path[i].mask;

		if (want == path[i].old)
			continue;
		/* Include the failing write in rollback: nofault may partially store. */
		changed |= 1U << i;
		ret = ops->write(path[i].entry, want);
		if (!ret && ops->read(path[i].entry) != want)
			ret = -EIO;
		if (ret) {
			req->eio_stage = 1;
			goto rollback;
		}
		req->cleared++;
		if (i + 1 < depth)
			req->tables_cleared++;
	}
	if (changed)
		ops->flush();
	for (i = 0; i < depth; i++) {
		if (ops->read(path[i].entry) != (path[i].old & ~path[i].mask)) {
			req->eio_stage = 2;
			ret = -EIO;
			goto rollback;
		}
	}
	return 0;

rollback:
	/* Restore parent restrictions first. The trampoline is still unpublished. */
	for (i = 0; i < depth; i++) {
		int err;

		if (!(changed & (1U << i)))
			continue;
		err = ops->write(path[i].entry, path[i].old);
		if (!err && ops->read(path[i].entry) != path[i].old)
			err = -EIO;
		if (err && !req->rollback_error)
			req->rollback_error = err;
	}
	if (changed)
		ops->flush();
	return ret;
}

#endif
