// SPDX-License-Identifier: GPL-2.0
/*
 * sus_map.c - hide mmapped real files from /proc/<pid>/maps (SUSFS SUS_MAP).
 *
 * Upstream sets AS_FLAGS_SUS_MAP on the inode's address_space and makes
 * show_map_vma()/show_smap() skip the line, the smaps_rollup() loop skip the vma it
 * accumulates and pagemap_read() skip the chunk covering such a vma.  An LKM cannot add a
 * flag bit, so: a listing layer (kprobes on show_map_vma/show_smap, vma = regs->regs[1],
 * skip the line with regs->pc = x30) plus a page-walk layer for the two listings it cannot
 * reach (smaps_rollup, pagemap).  smaps_rollup is deliberately NOT probed directly (see
 * "the sentinel") and the skip is gated like upstream's, apps only (see "the read gate").
 */
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/fs.h>
#include <linux/mm.h>		/* struct mm_struct; sus_map_find_vma() walks its vmas */
#include <linux/mm_types.h>
#include <linux/version.h>	/* LINUX_VERSION_CODE: mm->mmap vs mm->mm_mt (6.1) */
#include <linux/namei.h>
#include <linux/dcache.h>
#include <linux/uaccess.h>
#include <linux/err.h>		/* IS_ERR/ERR_PTR for the getlink hook */
#include <linux/cred.h>		/* current_uid(), for the read gate */
#include <linux/spinlock.h>	/* serialises rule publication */
#include <linux/slab.h>		/* kmalloc(GFP_ATOMIC), for the deferred path release */
#include <linux/task_work.h>	/* the map_files reference is released from task_work */
#include <linux/mutex.h>	/* map_ctl_lock: serialises probe arming */
#include <linux/proc_fs.h>	/* proc_create() for /proc/susfs_map */
#include <linux/seq_file.h>
#include "susfs_abi.h"
#include "susfs_log.h"
#include "susfs.h"	/* susfs_abi_path_ok */
#include "symbol_resolver.h"	/* find_kernel_symbol_exact, for the walk ops */
#include "ksu_umount_gate.h"	/* susfs_is_current_proc_umounted_app (issue #34) */
#include "ksu_allow_gate.h"	/* susfs_control_visible(): the node gate is su's own */
#include "susfs_kprobe.h"

#define SUS_MAP_MAX 64

struct sus_map_entry {
    unsigned long target_ino;
    dev_t target_dev;
};

static struct sus_map_entry map_entries[SUS_MAP_MAX];
static int nmap;

/* Serialises rule PUBLICATION only; the reader stays lock-free: a slot is published by
 * writing target_dev first and target_ino last with release semantics (0 = free), and nmap
 * only advances under the lock with the appended slot.  Deletion clears target_ino back to 0
 * (never target_dev), so a reader that saw the old ino also saw its own dev instead of a
 * cleared wildcard.  The lock itself is for concurrent writers, which would otherwise
 * fill one slot and silently drop a rule while both report success. */
static DEFINE_SPINLOCK(map_table_lock);

/* temporary interface: insmod susfs_guard_lkm.ko map_ino=<n> hides that inode */
static unsigned long param_map_ino;
module_param_named(map_ino, param_map_ino, ulong, 0644);

/* dev==0 means "any filesystem": only the map_ino parameter can pick that. */
static int sus_map_add_full(unsigned long ino, dev_t dev, bool *added)
{
    unsigned long flags;
    int i, rc = 0;

    *added = false;
    if (!ino)
        return -EINVAL;

    spin_lock_irqsave(&map_table_lock, flags);
    for (i = 0; i < nmap; i++) {
        if (map_entries[i].target_ino == ino && map_entries[i].target_dev == dev) {
            spin_unlock_irqrestore(&map_table_lock, flags);
            return 0;
        }
        if (map_entries[i].target_ino)
            continue;
        WRITE_ONCE(map_entries[i].target_dev, dev);
        smp_store_release(&map_entries[i].target_ino, ino);
        *added = true;
        spin_unlock_irqrestore(&map_table_lock, flags);
        return 0;
    }
    if (nmap < SUS_MAP_MAX) {
        WRITE_ONCE(map_entries[nmap].target_dev, dev);
        smp_store_release(&map_entries[nmap].target_ino, ino);
        smp_store_release(&nmap, nmap + 1);
        *added = true;
    } else {
        rc = -ENOSPC;
    }
    spin_unlock_irqrestore(&map_table_lock, flags);
    return rc;
}

/* A deleted rule leaves a zero inode behind; add_full() reuses that slot, so a script
 * that adds and removes paths does not exhaust the 64 entries. */
static int sus_map_del_full(unsigned long ino, dev_t dev)
{
    unsigned long flags;
    int i, rc = -ENOENT;

    if (!ino)
        return -EINVAL;

    spin_lock_irqsave(&map_table_lock, flags);
    for (i = 0; i < nmap; i++) {
        if (map_entries[i].target_ino != ino)
            continue;
        if (map_entries[i].target_dev && map_entries[i].target_dev != dev)
            continue;
        /* target_ino is the publish word; leave target_dev alone so a reader that
         * saw the old ino never sees it paired with the dev==0 wildcard. */
        smp_store_release(&map_entries[i].target_ino, 0);
        rc = 0;
        break;
    }
    spin_unlock_irqrestore(&map_table_lock, flags);
    return rc;
}

static int sus_map_del_exact(unsigned long ino, dev_t dev)
{
    unsigned long flags;
    int i, rc = -ENOENT;

    if (!ino)
        return -EINVAL;

    spin_lock_irqsave(&map_table_lock, flags);
    for (i = 0; i < nmap; i++) {
        if (map_entries[i].target_ino != ino || map_entries[i].target_dev != dev)
            continue;
        smp_store_release(&map_entries[i].target_ino, 0);
        rc = 0;
        break;
    }
    spin_unlock_irqrestore(&map_table_lock, flags);
    return rc;
}

static int sus_map_clear(void)
{
    unsigned long flags;
    int i;

    spin_lock_irqsave(&map_table_lock, flags);
    for (i = 0; i < nmap; i++)
        smp_store_release(&map_entries[i].target_ino, 0);
    smp_store_release(&nmap, 0);
    spin_unlock_irqrestore(&map_table_lock, flags);
    return 0;
}

static int sus_map_active(void)
{
    int i, n = smp_load_acquire(&nmap), active = 0;

    for (i = 0; i < n; i++)
        if (smp_load_acquire(&map_entries[i].target_ino))
            active++;
    return active;
}

static void sus_map_add(unsigned long ino)
{
    bool added;

    sus_map_add_full(ino, 0, &added);
}

static bool sus_map_lookup(unsigned long ino, dev_t dev)
{
    int n = smp_load_acquire(&nmap);
    int i;

    if (!ino)
        return false;
    for (i = 0; i < n; i++) {
        unsigned long e_ino = smp_load_acquire(&map_entries[i].target_ino);
        dev_t e_dev;

        if (!e_ino || e_ino != ino)
            continue;
        e_dev = READ_ONCE(map_entries[i].target_dev);
        if (e_dev && e_dev != dev)
            continue;
        return true;
    }
    return false;
}

/* ---- the read gate ----
 *
 * Upstream hides behind SUSFS_IS_INODE_SUS_MAP() -> susfs_is_current_proc_umounted_app()
 * = (test_thread_flag(TIF_PROC_UMOUNTED) && current_uid().val >= 10000): apps only - and only the
 * ones KernelSU umounts modules for - so root/init and a su-granted app see the real mapping.
 * TIF_PROC_UMOUNTED is set by the SUSFS-patched KernelSU, which this LKM is not, so the same
 * question is put to KernelSU directly (ksu_umount_gate.h, issue #34); the plain `uid >= 10000`
 * proxy this replaces hid from the manager and from su-granted apps too.
 * Configuration stays ungated: the supercall and map_ino are rule management. */
static bool sus_map_gate_ok(void)
{
    return susfs_is_current_proc_umounted_app();
}

/* Lines dropped by the show_map / show_map_vma / show_smap probes: this is the decision.
 * Kept apart from hide_modules and from n_walk_skip, which is the walk layer that only feeds
 * it - a vendor kernel can have every walk counter moving and this one at zero, which is
 * exactly the fingerprint of an inlined show_map_vma (susfs4ksu-lkm#102).  Declared here
 * because sus_map_skip_vma_pre() below is its first user. */
static atomic_t n_show_hides = ATOMIC_INIT(0);

/* One handler for show_map_vma (maps) and show_smap (smaps) - both are seq_operations
 * .show callbacks that emit only what they are given, so returning 0 means "handled,
 * nothing printed" (seq_read ignores the value), which is upstream's SUS_MAP effect.
 *
 * ---- the sentinel ----
 *
 * smaps_rollup must NOT be pointed at this handler: show_smaps_rollup() is reached
 * through single_open(), whose single_start() hands .show the iterator sentinel (void *)1
 * instead of a vma, and the function ignores its v argument.  Measured with the probe
 * registered: `cat /proc/<pid>/smaps_rollup` as an app took vma->vm_file at 0xa0 -> ldr
 * from 0xa1 -> "Unable to handle kernel NULL pointer dereference at virtual address
 * 00000000000000a1", pc sus_map_skip_vma_pre+0x3c, then a panic (uptime 41346.347).
 * Unreachable from a kprobe too: upstream skips the vma *inside* the rollup loop, and
 * smap_gather_stats() is inlined by LTO here (absent from /proc/kallsyms), so only
 * walk_page_range() could be intercepted. */
static int sus_map_skip_vma_pre(struct kprobe *kp, struct pt_regs *regs)
{
    struct vm_area_struct *vma;
    struct inode *inode;

    if (!sus_map_gate_ok())
        return 0;

    vma = (struct vm_area_struct *)regs->regs[1];
    /* Defence in depth against the sentinel above: a vma is always a slab object in
     * the linear map, so anything below one page is not one - this turns a future
     * mis-registration into a lost filter, not a panic. */
    if ((unsigned long)vma < PAGE_SIZE)
        return 0;
    if (!vma->vm_file)
        return 0;
    inode = file_inode(vma->vm_file);
    if (!inode)
        return 0;
    if (sus_map_lookup(inode->i_ino, inode->i_sb->s_dev)) {
        atomic_inc(&n_show_hides);
        /* ratelimited like sus_path's; the rule stores no path, so ino/dev name it */
        pr_info_ratelimited("sus_map: hid %s line (ino=%lu dev=%lu uid=%u)\n",
                            kp->symbol_name, inode->i_ino,
                            (unsigned long)inode->i_sb->s_dev,
                            current_uid().val);
        regs_set_return_value(regs, 0);
        regs->pc = regs->regs[30];
        return 1;
    }
    return 0;
}

static struct kprobe kp_map = {
    .symbol_name = "show_map_vma",
    .pre_handler = sus_map_skip_vma_pre,
};

static struct kprobe kp_map_smap = {
    .symbol_name = "show_smap",
    .pre_handler = sus_map_skip_vma_pre,
};

/* The outer half of the maps listing.  show_map() is what proc_pid_maps_op points at
 * (a proc_ops .proc_show field, i.e. a function pointer in data, which a build cannot
 * inline away), and it calls show_map_vma() for every line.  A vendor kernel is free to
 * inline that call - measured on a vivo 6.6.89: `show_map_vma` exists in /proc/kallsyms
 * and the probe arms, but it never fires, so map_stat shows hides=0 while the listing
 * stays visible (susfs4ksu-lkm#102).  Probing show_map() instead catches the same vma at
 * the outer layer; and because a hit there returns straight to the caller, the inner
 * probe (where it is out of line) never sees the same vma twice.  Both take
 * (struct seq_file *, void *v) with the vma as the second argument, so one handler serves
 * all of them. */
static struct kprobe kp_map_outer = {
    .symbol_name = "show_map",
    .pre_handler = sus_map_skip_vma_pre,
};

/* ---- the page-walk layer: smaps_rollup and pagemap ----
 *
 * Both listings test a vma upstream holds as a local, so neither is reachable from a
 * kprobe: no local at a function boundary, smap_gather_stats() inlined by LTO, and
 * pagemap_read() only has its vma after taking mmap_lock inside.
 *   show_smaps_rollup() -> for (vma = priv->mm->mmap; vma;) { ... skip ... }
 *   pagemap_read()      -> vma = vma_lookup(mm, start_vaddr); ... skip chunk
 * Both share one exported primitive, called with a caller-unique mm_walk_ops:
 *   smap_gather_stats(): walk_page_range(vma->vm_mm, ..., smaps_walk_ops|smaps_shmem_walk_ops, mss)
 *   pagemap_read():      walk_page_range(mm, start, end, &pagemap_ops, &pm)
 * Those three ops have no other user and every caller holds mmap_lock for read (the walk
 * asserts it), so skipping the call leaves upstream's result: the rollup vma adds nothing
 * to mss, the pagemap chunk stays unfilled and `ret = walk_page_range(...)` sees 0, as
 * upstream leaves it.  The ops are data symbols, so they come from kallsyms by name
 * (sus_mount's mnt_id_ida mechanism); if none resolves, the probe is not registered and both
 * listings stay unfiltered (logged).  The probe sits on a kernel-wide primitive, so it bails
 * out in one load and three compares - ops test first.
 *
 * Which of walk_page_range()/walk_page_vma() is the live call site cannot be assumed: with
 * CONFIG_LTO_CLANG_FULL either may be inlined into its caller (invisible to any probe - the
 * wall smap_gather_stats() hit), so both symbols are probed and walk_dbg=1 records every
 * distinct ops pointer with its call count (walk_ops), naming the real caller. */
static const void *sus_map_ops_smaps;
static const void *sus_map_ops_smaps_shmem;
static const void *sus_map_ops_pagemap;
static bool sus_map_walk_ops_done;
static atomic_t n_walk_skip = ATOMIC_INIT(0);
static atomic_t n_walk_seen = ATOMIC_INIT(0);		/* walk_page_range calls */
static atomic_t n_walk_seen_vma = ATOMIC_INIT(0);	/* walk_page_vma calls  */
/* One counter per resolution step: "the walk was not skipped" has four different causes. */
static atomic_t n_walk_ops_hit = ATOMIC_INIT(0);	/* ops was ours */
static atomic_t n_walk_scan_fail = ATOMIC_INIT(0);	/* no vma for (mm,start) */
static atomic_t n_walk_nofile = ATOMIC_INIT(0);		/* vma has no vm_file */
static atomic_t n_walk_nomatch = ATOMIC_INIT(0);	/* inode is not a rule */
static atomic_t n_walk_dbg_left = ATOMIC_INIT(4);
/* map_files symlink resolutions that were turned into ENOENT.  n_getlink_calls vs
 * n_getlink_skip keeps "the probe ran" apart from "the probe matched". */
static atomic_t n_map_files_hides = ATOMIC_INIT(0);
/* The reference map_files_get_link() took for its caller could not be handed to task_work
 * (GFP_ATOMIC or task_work_add): the call is then left as the kernel answered it - visible, but
 * not leaked - and this is the only thing that says so. */
static atomic_t n_map_files_putfail = ATOMIC_INIT(0);
static atomic_t n_getlink_calls = ATOMIC_INIT(0);
static atomic_t n_getlink_skip = ATOMIC_INIT(0);
static atomic_t n_getlink_nomatch = ATOMIC_INIT(0);

/* walk_dbg: name every ops pointer reaching the two primitives, once per value (off by default - a linear scan per call). */
static int walk_dbg;
module_param_named(walk_dbg, walk_dbg, int, 0644);

#define WALK_OPS_MAX 8
static const void *walk_seen_ops[WALK_OPS_MAX];
static atomic_t walk_seen_cnt[WALK_OPS_MAX];
static int walk_seen_n;

static void sus_map_note_ops(const void *ops)
{
    int i, n = READ_ONCE(walk_seen_n);

    for (i = 0; i < n; i++) {
        if (READ_ONCE(walk_seen_ops[i]) == ops) {
            atomic_inc(&walk_seen_cnt[i]);
            return;
        }
    }
    if (n >= WALK_OPS_MAX)
        return;
    WRITE_ONCE(walk_seen_ops[n], ops);
    atomic_inc(&walk_seen_cnt[n]);
    smp_store_release(&walk_seen_n, n + 1);
    SUSFS_LOGI("sus_map: walk ops[%d] = %pS\n", n, ops);
}

static int sus_map_walk_ops_show(char *buf, const struct kernel_param *kp)
{
    int i, n = 0, cnt = READ_ONCE(walk_seen_n);

    for (i = 0; i < cnt; i++)
        n += scnprintf(buf + n, PAGE_SIZE - n, "%2d %pS\n", i, walk_seen_ops[i]);
    n += scnprintf(buf + n, PAGE_SIZE - n,
                   "counts: page_range=%d page_vma=%d skipped=%d\n",
                   atomic_read(&n_walk_seen), atomic_read(&n_walk_seen_vma),
                   atomic_read(&n_walk_skip));
    for (i = 0; i < cnt; i++)
        n += scnprintf(buf + n, PAGE_SIZE - n, "  [%d] n=%d\n", i,
                       atomic_read(&walk_seen_cnt[i]));
    return n;
}
static const struct kernel_param_ops sus_map_walk_ops_ops = {
    .get = sus_map_walk_ops_show,
};
module_param_cb(walk_ops, &sus_map_walk_ops_ops, NULL, 0400);

static void sus_map_resolve_walk_ops(void)
{
    if (sus_map_walk_ops_done)
        return;
    sus_map_walk_ops_done = true;

    sus_map_ops_smaps = (const void *)find_kernel_symbol_exact("smaps_walk_ops");
    sus_map_ops_smaps_shmem = (const void *)find_kernel_symbol_exact("smaps_shmem_walk_ops");
    sus_map_ops_pagemap = (const void *)find_kernel_symbol_exact("pagemap_ops");

    if (!sus_map_ops_smaps && !sus_map_ops_smaps_shmem && !sus_map_ops_pagemap) {
        pr_warn("sus_map: smaps/pagemap walk ops not found in kallsyms - "
                "smaps_rollup and pagemap stay unfiltered\n");
        return;
    }
    SUSFS_LOGI("sus_map: walk ops smaps=%px smaps_shmem=%px pagemap=%px\n",
            sus_map_ops_smaps, sus_map_ops_smaps_shmem, sus_map_ops_pagemap);
}

static bool sus_map_walk_ops_any(void)
{
    return sus_map_ops_smaps || sus_map_ops_smaps_shmem || sus_map_ops_pagemap;
}

static bool sus_map_walk_ops_ours(const void *ops)
{
    return ops && (ops == sus_map_ops_smaps || ops == sus_map_ops_smaps_shmem ||
                   ops == sus_map_ops_pagemap);
}

static bool sus_map_walk_answer(struct kprobe *kp, struct pt_regs *regs,
                                struct vm_area_struct *vma)
{
    struct inode *inode;

    /* Defence in depth, like the vma handler: a real vma is never below a page. */
    if ((unsigned long)vma < PAGE_SIZE)
        return false;
    if (!vma->vm_file) {
        atomic_inc(&n_walk_nofile);
        return false;
    }
    inode = file_inode(vma->vm_file);
    if (!inode) {
        atomic_inc(&n_walk_nofile);
        return false;
    }
    if (!sus_map_lookup(inode->i_ino, inode->i_sb->s_dev)) {
        atomic_inc(&n_walk_nomatch);
        return false;
    }

    atomic_inc(&n_walk_skip);
    pr_info_ratelimited("sus_map: skipped %s walk for ino=%lu dev=%lu uid=%u\n",
                        kp->symbol_name, inode->i_ino,
                        (unsigned long)inode->i_sb->s_dev, current_uid().val);
    /* Both primitives return int 0 for "walked, nothing wrong" and both callers expect
     * that from a skipped walk (upstream's own skip leaves the same value).  Leaving
     * x0 = mm would turn pagemap_read()'s `ret` into a kernel pointer handed to read(2). */
    regs_set_return_value(regs, 0);
    regs->pc = regs->regs[30];
    return true;
}

/* The VMA container changed in 6.1; both spellings answer the same question - "the first
 * vma whose end is past @start", i.e. the one containing @start or the next after a gap -
 * and the caller still compares the result against vm_start.
 *   <= 6.0  mm_struct.mmap heads a doubly linked list, walked with vma->vm_next
 *   >= 6.1  that list became the mm_struct.mm_mt maple tree, walked with the vma iterator
 *           helpers VMA_ITERATOR() / for_each_vma() (mm_types.h, mm.h) - which is why
 *           "no member named 'mmap' in 'struct mm_struct'" and "'vm_next' in
 *           'struct vm_area_struct'" appeared on android14-6.1 and android15-6.6.
 * Both forms are header-only (macros and static inlines), so neither needs a kernel symbol;
 * mmap_lock is held for read by every caller, as the walk itself requires. */
static struct vm_area_struct *sus_map_find_vma(struct mm_struct *mm, unsigned long start)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
	struct vm_area_struct *vma;
	VMA_ITERATOR(vmi, mm, 0);

	for_each_vma(vmi, vma) {
		if (start < vma->vm_end)
			return vma;
	}
	return NULL;
#else
	struct vm_area_struct *vma;

	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		if (start < vma->vm_end)
			break;
	}
	return vma;
#endif
}

/* walk_dbg >= 2: dump the first few resolutions past the ops test, to tell "no vma for
 * (mm, start)" from "the vma is not the expected one". */
static void sus_map_walk_dbg_log(const char *what, struct mm_struct *mm,
                                 unsigned long start, struct vm_area_struct *vma)
{
    if (walk_dbg < 2)
        return;
    if (atomic_dec_if_positive(&n_walk_dbg_left) < 0)
        return;
    SUSFS_LOGI("sus_map: %s mm=%px start=%lx mmap=%px vma=%px %lx-%lx file=%px\n",
            what, mm, start, mm ? sus_map_find_vma(mm, 0) : NULL, vma,
            vma ? vma->vm_start : 0UL, vma ? vma->vm_end : 0UL,
            (vma && vma->vm_file) ? vma->vm_file : NULL);
}

/* walk_page_range(mm, start, end, ops, private): the vma is resolved from (mm, start) as
 * the walk itself would have done, under the mmap_lock every caller of these three ops
 * holds for read - so sus_map_find_vma()'s container cannot change underneath it. */
static int sus_map_skip_walk_pre(struct kprobe *kp, struct pt_regs *regs)
{
    const void *ops = (const void *)regs->regs[3];
    struct mm_struct *mm;
    struct vm_area_struct *vma;
    unsigned long start;

    atomic_inc(&n_walk_seen);
    if (walk_dbg)
        sus_map_note_ops(ops);
    if (!sus_map_walk_ops_ours(ops))
        return 0;

    atomic_inc(&n_walk_ops_hit);
    if (!sus_map_gate_ok())
        return 0;

    mm = (struct mm_struct *)regs->regs[0];
    start = (unsigned long)regs->regs[1];
    if (!mm)
        return 0;

    vma = sus_map_find_vma(mm, start);
    if (!vma || start < vma->vm_start) {
        atomic_inc(&n_walk_scan_fail);
        sus_map_walk_dbg_log("walk_page_range: no vma", mm, start, NULL);
        return 0;
    }
    sus_map_walk_dbg_log("walk_page_range: vma", mm, start, vma);

    return sus_map_walk_answer(kp, regs, vma) ? 1 : 0;
}

/* walk_page_vma(vma, ops, private): same walk, vma handed over directly. */
static int sus_map_skip_walk_vma_pre(struct kprobe *kp, struct pt_regs *regs)
{
    const void *ops = (const void *)regs->regs[1];

    atomic_inc(&n_walk_seen_vma);
    if (walk_dbg)
        sus_map_note_ops(ops);
    if (!sus_map_walk_ops_ours(ops))
        return 0;

    atomic_inc(&n_walk_ops_hit);
    if (!sus_map_gate_ok())
        return 0;

    return sus_map_walk_answer(kp, regs,
                               (struct vm_area_struct *)regs->regs[0]) ? 1 : 0;
}

static struct kprobe kp_map_walk = {
    .symbol_name = "walk_page_range",
    .pre_handler = sus_map_skip_walk_pre,
};

static struct kprobe kp_map_walk_vma = {
    .symbol_name = "walk_page_vma",
    .pre_handler = sus_map_skip_walk_vma_pre,
};

/* ---- /proc/<pid>/mem (and ptrace): the page-fetch primitive ----
 *
 * Upstream's fifth SUS_MAP site is __access_remote_vm() (mm/memory.c, patch:5667-5687):
 * after mmap_read_lock it resolves vma = vma_lookup(mm, addr) once and, inside the transfer
 * loop, breaks out when that vma's file is registered - so a read (or write) starting
 * inside a hidden mapping transfers nothing at all.  Its entry cannot be hooked usefully:
 * the lock is taken INSIDE it, and resolving a vma without that lock is a use-after-free
 * waiting for the target process to munmap.  What can be hooked is the primitive the loop
 * calls with the lock held:
 *   __access_remote_vm()       -> get_user_pages_remote(mm, addr, 1, ...)
 *   process_vm_rw_single_vec() -> pin_user_pages_remote(mm, pa, ...)
 *   (both *_remote variants require mmap_read_lock)
 * Short-circuiting that call (x0 = 0, pc = lr) makes the caller see "no page transferred":
 * __access_remote_vm returns the bytes it had already moved - 0 when the read starts inside
 * the mapping, upstream's result - and process_vm_readv takes its `pinned_pages <= 0` error
 * path.  Nothing is pinned, because the call never runs.
 *
 * The second primitive is not needed for upstream parity (upstream patches
 * __access_remote_vm only, and process_vm_readv does not go through it - measured from this
 * kernel's mm/process_vm_access.c: process_vm_rw_single_vec calls pin_user_pages_remote
 * directly); it is hooked because the same short circuit closes one more probe.
 *
 * Cost control: the handler runs on every *_remote page fetch while a rule exists, so the
 * cheapest tests come first (no rules -> one load; uid < 10000 -> the gate) and find_vma()
 * only runs for callers the gate lets through. */
static atomic_t n_gup_calls = ATOMIC_INIT(0);
static atomic_t n_gup_inner_calls = ATOMIC_INIT(0);
static atomic_t n_vm_hides = ATOMIC_INIT(0);

static int sus_map_vm_access_pre(struct kprobe *kp, struct pt_regs *regs);

/* Both are thin layers of the same code and which one a call site reaches is decided by
 * LTO - measured: the __access_remote_vm path reaches get_user_pages_remote out of line (its
 * probe fired), while process_vm_readv's pin_user_pages_remote was inlined away, so that
 * wrapper's probe was removed after zero hits in every run.  __get_user_pages_remote, what
 * both wrappers end in, takes the same (mm, start) as its first two arguments and is armed
 * too; a short circuit at the outer layer stops the inner one from being reached. */
static struct kprobe kp_gup_remote = {
    .symbol_name = "get_user_pages_remote",
    .pre_handler = sus_map_vm_access_pre,
};

static struct kprobe kp_gup_remote_inner = {
    .symbol_name = "__get_user_pages_remote",
    .pre_handler = sus_map_vm_access_pre,
};

static int sus_map_vm_access_pre(struct kprobe *kp, struct pt_regs *regs)
{
    struct mm_struct *mm = (struct mm_struct *)regs->regs[0];
    unsigned long addr = regs->regs[1];
    struct vm_area_struct *vma;
    struct inode *inode;

    if (!smp_load_acquire(&nmap))
        return 0;
    if (!sus_map_gate_ok())
        return 0;
    if (!mm || !addr)
        return 0;

    atomic_inc(kp == &kp_gup_remote_inner ? &n_gup_inner_calls : &n_gup_calls);

    /* both callers hold mmap_read_lock: the *_remote contract of this primitive */
    vma = find_vma(mm, addr);
    if (!vma || !vma->vm_file)
        return 0;
    inode = file_inode(vma->vm_file);
    if (!inode)
        return 0;
    if (!sus_map_lookup(inode->i_ino, inode->i_sb->s_dev))
        return 0;

    atomic_inc(&n_vm_hides);
    regs_set_return_value(regs, 0);     /* "nothing transferred" */
    regs->pc = regs->regs[30];          /* skip the call: nothing is pinned */
    return 1;
}

/* ---- /proc/<pid>/map_files/<start>-<end> ----
 *
 * Each entry is a symlink to the file mapped at that range, so `ls -l` and readlink name a
 * sus_map-registered file outright - measured: this is how the address of a mapping the maps
 * listing had already dropped was still reachable, i.e. this listing undid the hiding.
 * Upstream skips the entry in proc_map_files_readdir() (patch:1088-1095); a kprobe cannot
 * skip one entry of a readdir (the decision is a local in the middle of that function), so
 * the symlink still exists but resolving it answers ENOENT - what a checker gets for a file
 * that is not there.  The residual difference is that the range itself is still listed.
 *
 * The first attempt hooked `proc_map_files_get_link` (the i_op): it registered fine and was
 * never called.  From this kernel's fs/proc/base.c - do_readlinkat() calls i_op->readlink
 * FIRST and only falls back to vfs_readlink() (-> get_link) when it is NULL, and
 * proc_map_files uses `.readlink = proc_pid_readlink`, which ends in the per-inode callback
 * `ei->op.proc_get_link` = map_files_get_link(), the one place that hands over the MAPPED
 * file's path.  So the hook judges by inode (path->dentry), which is exact and only ever
 * fails a call that would have succeeded: the caller treats a negative return as "no link". */
/* Releasing the reference map_files_get_link() took, from task_work rather than from the probe.
 *
 * The probe's return handler runs in the exception context that fired it (arm64 raises BRK for a
 * kprobe), where sleeping is not allowed - and path_put() can sleep, because dropping the last
 * reference reaches ->d_iput().  GFP_ATOMIC and task_work_add() are both safe there; what is not
 * safe is guessing, so a failure to queue is reported and the reference is left where it was. */
struct sus_map_put_work {
	struct callback_head cb;
	struct path path;
};

static void sus_map_put_work_fn(struct callback_head *cb)
{
	struct sus_map_put_work *w = container_of(cb, struct sus_map_put_work, cb);

	path_put(&w->path);
	kfree(w);
}

/* 0 when the release has been queued, -errno when it has not. */
static int sus_map_path_put_soon(const struct path *p)
{
	struct sus_map_put_work *w;

	w = kmalloc(sizeof(*w), GFP_ATOMIC);
	if (!w)
		return -ENOMEM;
	w->path = *p;
	init_task_work(&w->cb, sus_map_put_work_fn);
	if (task_work_add(current, &w->cb, TWA_RESUME)) {
		kfree(w);
		return -EIO;
	}
	return 0;
}

static int sus_map_maplink_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    /* struct path *path is an output argument, filled only at return: save it here. */
    *(unsigned long *)ri->data = regs->regs[1];
    return 0;
}

static int sus_map_maplink_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    unsigned long outp = *(unsigned long *)ri->data;
    const struct path *p;
    struct inode *inode;

    atomic_inc(&n_getlink_calls);
    if ((long)regs_return_value(regs) != 0 || !outp) {
        atomic_inc(&n_getlink_skip);
        return 0;
    }
    if (!sus_map_gate_ok()) {
        atomic_inc(&n_getlink_skip);
        return 0;
    }
    p = (const struct path *)outp;
    if (!p->dentry) {
        atomic_inc(&n_getlink_skip);
        return 0;
    }
    inode = d_inode(p->dentry);
    if (!inode) {
        atomic_inc(&n_getlink_skip);
        return 0;
    }
    if (!sus_map_lookup(inode->i_ino, inode->i_sb->s_dev)) {
        atomic_inc(&n_getlink_nomatch);
        return 0;
    }

    atomic_inc(&n_map_files_hides);
    /* The reference has to change hands with the answer.  map_files_get_link() runs
     * `*path = vma->vm_file->f_path; path_get(path);` before it returns 0, and its only caller
     * releases that reference on the SUCCESS path alone (proc_pid_readlink():
     * `if (error) goto out; ... path_put(&path);`).  Turning the return into -ENOENT without
     * taking the reference over leaked a dentry and a mount reference on every resolution of a
     * hidden mapping's symlink - the mapped file keeps its own reference, so the dentry is never
     * reclaimed and the mount can never be unmounted, and an app uid can do it in a loop.
     *
     * It cannot be released here (see sus_map_path_put_soon()), so it goes to task_work.  If even
     * that fails the call keeps the kernel's own answer: a symlink that resolves is a tell, a
     * silent leak is not. */
    if (sus_map_path_put_soon(p)) {
        atomic_inc(&n_map_files_putfail);
        return 0;
    }
    pr_info_ratelimited("sus_map: hid map_files symlink to ino=%lu (uid=%u)\n",
                        inode->i_ino, current_uid().val);
    regs_set_return_value(regs, (unsigned long)(long)-ENOENT);
    return 0;
}

static struct kretprobe kr_map_files = {
    .kp.symbol_name = "map_files_get_link",
    .entry_handler = sus_map_maplink_entry,
    .handler = sus_map_maplink_ret,
    .data_size = sizeof(unsigned long),	/* the output struct path * */
    .maxactive = 16,
};
static bool kr_map_files_ok;

/* Kept in one table so init and exit cannot drift apart. */
static struct kprobe *const map_probes[] = {
    &kp_map, &kp_map_smap, &kp_map_outer, &kp_map_walk, &kp_map_walk_vma,
    &kp_gup_remote, &kp_gup_remote_inner,
};
#define N_MAP_PROBES ARRAY_SIZE(map_probes)
static bool map_registered;
static bool map_probe_armed[N_MAP_PROBES];

/* maps is covered by show_map_vma (0) or show_map (2); show_smap (1) is the smaps half and
 * cannot substitute for either.  Requiring index 0 alone made a successful show_map arm look
 * like a total failure. */
static bool sus_map_maps_armed(void)
{
    return READ_ONCE(map_probe_armed[0]) || READ_ONCE(map_probe_armed[2]);
}

static bool sus_map_smaps_armed(void)
{
    return READ_ONCE(map_probe_armed[1]);
}

/* Reachability, not configuration: "the probe is registered" says nothing on a kernel built
 * with CONFIG_LTO_CLANG_FULL, where the call site it should catch may have been inlined away
 * (walk_page_range() is EXPORT_SYMBOL_GPL and LTO may still inline the calls inside
 * pagemap_read()/smap_gather_stats()).  walk_seen == 0 while a rule matches fingerprints such
 * an inlined call site, not a rule that failed to match. */
static int sus_map_stat_show(char *buf, const struct kernel_param *kp)
{
    int i, armed = 0;

    for (i = 0; i < (int)N_MAP_PROBES; i++)
        armed += map_probe_armed[i] ? 1 : 0;

    return scnprintf(buf, PAGE_SIZE,
                     "rules=%d armed=%d/%d walk_seen=%d walk_vma=%d walk_skipped=%d "
                     "ops_hit=%d scan_fail=%d nofile=%d nomatch=%d getlink: calls=%d skip=%d nomatch=%d hides=%d putfail=%d line_hides=%d\n"
                     "vm: gup_calls=%d inner_calls=%d hides=%d\n"
                     "ops: smaps=%px smaps_shmem=%px pagemap=%px\n",
                     sus_map_active(), armed, (int)N_MAP_PROBES,
                     atomic_read(&n_walk_seen), atomic_read(&n_walk_seen_vma),
                     atomic_read(&n_walk_skip),
                     atomic_read(&n_walk_ops_hit), atomic_read(&n_walk_scan_fail),
                     atomic_read(&n_walk_nofile), atomic_read(&n_walk_nomatch),
                     atomic_read(&n_getlink_calls), atomic_read(&n_getlink_skip),
                     atomic_read(&n_getlink_nomatch), atomic_read(&n_map_files_hides),
                     atomic_read(&n_map_files_putfail), atomic_read(&n_show_hides),
                     atomic_read(&n_gup_calls), atomic_read(&n_gup_inner_calls),
                     atomic_read(&n_vm_hides),
                     sus_map_ops_smaps, sus_map_ops_smaps_shmem,
                     sus_map_ops_pagemap);
}
static const struct kernel_param_ops sus_map_stat_ops = {
    .get = sus_map_stat_show,
};
module_param_cb(map_stat, &sus_map_stat_ops, NULL, 0400);

/* Serialises probe arming.  register_kprobe() answers -EINVAL once another task has resolved
 * .addr, and the failure branch below clears .addr - without this lock that disarms a probe that
 * is LIVE: its breakpoint stays installed and unregister_kprobe() can no longer take it out, so
 * it would survive the unload with a handler pointing into freed module text.  Two tasks can add
 * a rule at the same time (every supercall runs in its own task_work), and susfs_sus_map_init()
 * reaches this too. */
static DEFINE_MUTEX(map_ctl_lock);

/* Registers whichever of the probes are not up yet.  Called from init (when a rule
 * already exists) and from the supercall that adds the first rule, so both paths arm
 * exactly the same set. */
static int sus_map_register_probes(void)
{
    int i, n = 0, first_err = 0;
    int ret;

    mutex_lock(&map_ctl_lock);

    sus_map_resolve_walk_ops();

    for (i = 0; i < (int)N_MAP_PROBES; i++) {
        int rc;

        if (map_probe_armed[i])
            continue;
        if ((map_probes[i] == &kp_map_walk || map_probes[i] == &kp_map_walk_vma) &&
            !sus_map_walk_ops_any())
            continue;
        rc = register_kprobe(map_probes[i]);
        if (rc) {
            pr_warn("sus_map: register_kprobe(%s) failed %d - that listing is not filtered\n",
                    map_probes[i]->symbol_name, rc);
            /* This function is retried from the supercall that adds a rule (and it skips the
             * probes that did arm), so the struct has to be clear for the retry to reach the
             * symbol again: a failure after address resolution leaves .addr set - see
             * susfs_kprobe.h. */
            susfs_kp_forget_addr(map_probes[i]);
            if (!first_err)
                first_err = rc;
            continue;
        }
        map_probe_armed[i] = true;
        n++;
    }

    if (n)
        SUSFS_LOGI("sus_map: %d/%d probes armed (%d rules)\n",
                n, (int)N_MAP_PROBES, nmap);

    /* The map_files hook is a kretprobe, so it lives outside map_probes[] (which
     * holds struct kprobe *) and is tracked on its own; optional like the rest. */
    if (!kr_map_files_ok) {
        int rc = register_kretprobe(&kr_map_files);

        if (rc) {
            susfs_krp_forget_addr(&kr_map_files);
            pr_warn("sus_map: register_kretprobe(proc_map_files_get_link) failed %d - readlink on a hidden mapping still names the file\n",
                    rc);
        } else {
            kr_map_files_ok = true;
        }
    }

    map_registered = sus_map_maps_armed();
    if (map_registered && !sus_map_smaps_armed())
        pr_warn("sus_map: smaps listing is not filtered (show_smap probe unavailable)\n");
    ret = map_registered ? 0 : first_err;

    mutex_unlock(&map_ctl_lock);
    return ret;
}


static int sus_map_ensure_probes(unsigned long ino, dev_t dev, bool added)
{
    int rc;

    if (READ_ONCE(map_registered))
        return 0;
    rc = sus_map_register_probes();
    if (sus_map_maps_armed()) {
        if (!sus_map_smaps_armed())
            pr_warn("sus_map: smaps listing is not filtered (show_smap probe unavailable)\n");
        return 0;
    }
    /* No maps listing hook: a rule that cannot hide anything must not stay.  Delete only
     * the rule this call inserted; an existing rule must survive a failed re-add. */
    if (added)
        sus_map_del_exact(ino, dev);
    return rc;
}

static int sus_map_add_path(const char *path)
{
    struct path p;
    struct inode *inode;
    int rc;

    if (!path || !*path)
        return -EINVAL;
    if (strlen(path) >= SUSFS_MAX_LEN_PATHNAME)
        return -ENAMETOOLONG;

    rc = kern_path(path, LOOKUP_FOLLOW, &p);
    if (rc)
        return rc;
    inode = d_backing_inode(p.dentry);
    if (!inode) {
        path_put(&p);
        return -ENOENT;
    }
    {
        unsigned long ino = inode->i_ino;
        dev_t dev = inode->i_sb->s_dev;

        bool added;

        rc = sus_map_add_full(ino, dev, &added);
        path_put(&p);
        if (rc)
            return rc;
        return sus_map_ensure_probes(ino, dev, added);
    }
}

static int sus_map_del_path(const char *path)
{
    struct path p;
    struct inode *inode;
    int rc;

    if (!path || !*path)
        return -EINVAL;
    if (strlen(path) >= SUSFS_MAX_LEN_PATHNAME)
        return -ENAMETOOLONG;

    rc = kern_path(path, LOOKUP_FOLLOW, &p);
    if (rc)
        return rc;
    inode = d_backing_inode(p.dentry);
    if (!inode) {
        path_put(&p);
        return -ENOENT;
    }
    rc = sus_map_del_full(inode->i_ino, inode->i_sb->s_dev);
    path_put(&p);
    return rc;
}

#define SUS_MAP_CMD_MAX 512

static int sus_map_proc_show(struct seq_file *m, void *v)
{
    int i, n = smp_load_acquire(&nmap), active = 0;

    for (i = 0; i < n; i++)
        if (smp_load_acquire(&map_entries[i].target_ino))
            active++;

    seq_printf(m, "rules: %d/%d\n", active, SUS_MAP_MAX);
    for (i = 0; i < n; i++) {
        unsigned long ino = smp_load_acquire(&map_entries[i].target_ino);

        if (!ino)
            continue;
        seq_printf(m, "ino=%lu dev=%lu\n", ino,
                   (unsigned long)READ_ONCE(map_entries[i].target_dev));
    }
    return 0;
}

static int sus_map_proc_open(struct inode *inode, struct file *file)
{
    if (!susfs_control_visible())
        return -ENOENT;
    return single_open(file, sus_map_proc_show, NULL);
}

static ssize_t sus_map_proc_write(struct file *file, const char __user *buf,
                                  size_t len, loff_t *off)
{
    char *cmd, *arg;
    size_t i;
    int rc;

    if (!susfs_control_visible())
        return -ENOENT;
    if (len == 0)
        return 0;
    if (len >= SUS_MAP_CMD_MAX)
        return -EINVAL;

    cmd = kvmalloc(len + 1, GFP_KERNEL);
    if (!cmd)
        return -ENOMEM;
    if (copy_from_user(cmd, buf, len)) {
        kvfree(cmd);
        return -EFAULT;
    }
    cmd[len] = 0;

    for (i = strlen(cmd); i > 0; i--) {
        if (cmd[i - 1] != 10 && cmd[i - 1] != 13 && cmd[i - 1] != ' ')
            break;
        cmd[i - 1] = 0;
    }

    if (!strcmp(cmd, "clear")) {
        rc = sus_map_clear();
    } else if (!strncmp(cmd, "add ", 4) || !strncmp(cmd, "del ", 4)) {
        arg = cmd + 4;
        while (*arg == ' ')
            arg++;
        if (!*arg || strlen(arg) >= SUSFS_MAX_LEN_PATHNAME) {
            rc = -EINVAL;
        } else if (cmd[0] == 'a') {
            rc = sus_map_add_path(arg);
        } else {
            rc = sus_map_del_path(arg);
        }
    } else {
        rc = -EINVAL;
    }

    kvfree(cmd);
    return rc ? rc : (ssize_t)len;
}

static const struct proc_ops sus_map_proc_ops = {
    .proc_open = sus_map_proc_open,
    .proc_read = seq_read,
    .proc_write = sus_map_proc_write,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static struct proc_dir_entry *sus_map_entry;

int susfs_sus_map_init(void)
{
    int rc;

    if (susfs_control_node_allowed()) {
        sus_map_entry = proc_create("susfs_map", 0777, NULL, &sus_map_proc_ops);
        if (!sus_map_entry)
            pr_warn("sus_map: proc_create(susfs_map) failed - runtime rule control unavailable, use the map_ino parameter\n");
    } else {
        SUSFS_LOGI("sus_map: /proc/susfs_map not created (expose_proc=%d)\n",
                   (int)susfs_expose_proc);
    }

    sus_map_add(param_map_ino);
    if (nmap == 0) {
        SUSFS_LOGI("sus_map: no rules, hook not installed\n");
        return 0;
    }

    /* Every probe is optional on its own: without show_smap the maps listing is still
     * filtered, so one missing symbol must not take the rest down. */
    rc = sus_map_register_probes();
    if (rc) {
        /* The layer is non-fatal, but a rule that cannot hide anything must not stay:
         * drop it and remove the node so add does not report success later. */
        sus_map_clear();
        if (sus_map_entry) {
            proc_remove(sus_map_entry);
            sus_map_entry = NULL;
        }
        return rc;
    }
    return 0;
}

void susfs_sus_map_exit(void)
{
    int i;

    if (sus_map_entry) {
        proc_remove(sus_map_entry);
        sus_map_entry = NULL;
    }

    /* Same lock as the registration side.  The arm path is check-then-act on map_probe_armed[],
     * so disarming without it would break the invariant the duplicate-registration fix
     * established: one probe's arm and disarm belong under the same lock.  The supercall and
     * /proc/susfs_map both add rules while the module is loaded; the node cannot outlive it. */
    mutex_lock(&map_ctl_lock);
    for (i = 0; i < (int)N_MAP_PROBES; i++) {
        if (!map_probe_armed[i])
            continue;
        unregister_kprobe(map_probes[i]);
        susfs_kp_forget_addr(map_probes[i]);
        map_probe_armed[i] = false;
    }
    if (kr_map_files_ok) {
        unregister_kretprobe(&kr_map_files);
        susfs_krp_forget_addr(&kr_map_files);
        kr_map_files_ok = false;
    }
    map_registered = false;
    mutex_unlock(&map_ctl_lock);

    /* Published the way sus_map_add_full() does it (smp_store_release against the
     * smp_load_acquire readers); a plain store would break that protocol.  No reader can be live
     * by now - every probe is unregistered above - but the write should still have the right
     * shape. */
    smp_store_release(&nmap, 0);
}

/* supercall: CMD_SUSFS_ADD_SUS_MAP (resolve path -> ino/dev, register hook) */
void susfs_sus_map_supercall(void __user **arg)
{
    struct st_susfs_sus_map info = {0};
    struct path p;
    struct inode *inode;
    int rc;

    if (copy_from_user(&info, (void __user *)*arg, sizeof(info))) {
        info.err = -EFAULT;
        goto out;
    }

    /* char[256] field that need not be NUL-terminated: reject it before kern_path() can read off the end of our stack copy. */
    if (!susfs_abi_path_ok(info.target_pathname, sizeof(info.target_pathname))) {
        info.err = -ENAMETOOLONG;
        goto out;
    }

    rc = kern_path(info.target_pathname, LOOKUP_FOLLOW, &p);
    if (rc) {
        info.err = rc;
        goto out;
    }
    inode = d_backing_inode(p.dentry);
    if (!inode) {
        path_put(&p);
        info.err = -ENOENT;
        goto out;
    }

    /* One call fills both fields: the old two-step form let sus_map_add() return early on
     * ino==0 and then wrote map_entries[nmap-1] regardless - indexing -1 at worst,
     * corrupting the previous rule's device at best. */
    {
        unsigned long ino = inode->i_ino;
        dev_t dev = inode->i_sb->s_dev;

        bool added;

        rc = sus_map_add_full(ino, dev, &added);
        if (rc) {
            path_put(&p);
            info.err = rc;
            goto out;
        }
        if (added)
            SUSFS_LOGI("sus_map: added %s (ino=%lu) via supercall\n",
                    info.target_pathname, ino);
        path_put(&p);

        rc = sus_map_ensure_probes(ino, dev, added);
        if (rc) {
            info.err = rc;
            goto out;
        }
    }
    info.err = 0;
out:
    /* upstream writes back only ->err for input-type commands */
    if (copy_to_user(&((struct st_susfs_sus_map __user *)*arg)->err,
                     &info.err, sizeof(info.err)))
        pr_warn("sus_map supercall copy_to_user failed\n");
}
