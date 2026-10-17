// SPDX-License-Identifier: GPL-2.0
/*
 * susfs_kstat.c - spoof kstat fields (SUSFS SUS_KSTAT feature), LKM port.
 *
 * Interface mirrors the original SUSFS userspace commands, one command per write to
 * /proc/susfs_kstat:
 *   add_sus_kstat <path> - store the CURRENT stat of <path> as the spoof target
 *       (ino/dev/times/blocks/blksize), flags = KSTAT_AUTO_SPOOF; use it BEFORE the path is
 *       bind-mounted/overlayed, then call update_sus_kstat.
 *   add_sus_kstat_statically <path> <ino> <dev> <nlink> <size> <atime> <atime_nsec> <mtime>
 *       <mtime_nsec> <ctime> <ctime_nsec> <blocks> <blksize> - set each field explicitly;
 *       "default" keeps the current value and does NOT spoof that field, and only non-default
 *       fields get their KSTAT_SPOOF_* flag set.
 *   update_sus_kstat <path> / update_sus_kstat_full_clone <path> - re-resolve <path> (after it was
 *       bind-mounted/overlayed) and update target_ino/target_dev only, spoofed values staying as
 *       previously added; the _full_clone form also raises KSTAT_SPOOF_NLINK|KSTAT_SPOOF_SIZE.
 *   del <path> - remove one rule by its target pathname;   clear - remove all rules.
 *
 * Hook strategy (LTO on this GKI inlines the stat chain vfs_fstatat -> vfs_statx -> vfs_getattr ->
 * cp_new_stat, so VFS-layer kprobes miss): the reliable hook is the RETURN of the stat syscall,
 * where the user statbuf is fully written - so this layer replaces the sys_call_table entries of
 * the numbers below and rewrites the requested fields of the user buffer in the wrapper's tail,
 * once the original has returned (see kstat_hooks[] and susfs_kstat_post()).  Native:
 * newfstatat(79), fstat(80), statx(291).
 * AArch32: fstatat64(327), stat64(195), lstat64(196), fstat64(197) -> struct stat64; stat(106),
 * lstat(107), fstat(108) -> struct compat_stat; statx(397) -> the SAME struct statx as native
 * (unistd32.h maps 397 to the native statx body, and struct statx is a fixed-width uapi struct, so
 * only the pointers are 32-bit).  Field offsets are arm64 asm-generic struct stat / struct statx
 * (offsetof + static_assert), the ARM EABI struct stat64 (hand-derived, see the note above
 * STAT64_ST_DEV_OFF) and struct compat_stat (offsetof + static_assert).  The
 * vfs_getattr kretprobe that used to stand behind this as a lower-layer fallback is GONE: its
 * removal and what replaced it are explained in the note above struct kstat_call_counters.
 */

#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/stat.h>
#include <linux/compat.h>
#include <linux/tracepoint.h>
#include <trace/events/syscalls.h>
#include <asm/syscall.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kernel.h>
#include <linux/namei.h>
#include <linux/dcache.h>
#include <linux/kdev_t.h>
#include <linux/string.h>
#include <linux/cred.h>
#include <linux/version.h>	/* LINUX_VERSION_CODE: the inode ctime accessor */
#include "susfs_abi.h"
#include "susfs_log.h"
#include "susfs.h"	/* susfs_expose_proc, sus_path_dirent_filter(), SUS_NR_GETDENTS*_COMPAT */
#include "susfs_fp_hook.h"	/* struct susfs_fp_hook + install/remove/drain: the table entries */
#include "ksu_umount_gate.h"	/* susfs_is_current_proc_umounted_app (issue #34) */
#include "susfs_kprobe.h"

/* KSTAT_SPOOF_* bits live in susfs_abi.h (upstream declares them in susfs.h next to struct
 * st_susfs_sus_kstat).  KSTAT_AUTO_SPOOF* below are /proc-interface masks, not supercall ABI. */
#define KSTAT_AUTO_SPOOF (KSTAT_SPOOF_INO | KSTAT_SPOOF_DEV | \
	KSTAT_SPOOF_ATIME_TV_SEC | KSTAT_SPOOF_ATIME_TV_NSEC | \
	KSTAT_SPOOF_MTIME_TV_SEC | KSTAT_SPOOF_MTIME_TV_NSEC | \
	KSTAT_SPOOF_CTIME_TV_SEC | KSTAT_SPOOF_CTIME_TV_NSEC | \
	KSTAT_SPOOF_BLKSIZE | KSTAT_SPOOF_BLOCKS)
#define KSTAT_AUTO_SPOOF_FULL_CLONE (KSTAT_AUTO_SPOOF | \
	KSTAT_SPOOF_NLINK | KSTAT_SPOOF_SIZE)

#define SUS_KSTAT_MAX 32
/* Upstream's target_pathname is char[256]: a shorter buffer truncates a legal long path into a wrong rule. */
#define KSTAT_PATH_MAX 256

struct sus_kstat_entry {
	char target_pathname[KSTAT_PATH_MAX];
	unsigned long target_ino;
	/* dev stored ENCODED (new_encode_dev) so it matches the user statbuf st_dev field 1:1 on the tracepoint hot path. */
	dev_t target_dev;
	unsigned long spoofed_ino;
	unsigned long spoofed_dev;
	unsigned int spoofed_nlink;
	long long spoofed_size;
	long spoofed_atime_tv_sec;
	unsigned long spoofed_atime_tv_nsec;
	long spoofed_mtime_tv_sec;
	unsigned long spoofed_mtime_tv_nsec;
	long spoofed_ctime_tv_sec;
	unsigned long spoofed_ctime_tv_nsec;
	long long spoofed_blocks;
	long spoofed_blksize;
	unsigned int flags;
};

static struct sus_kstat_entry kstat_entries[SUS_KSTAT_MAX];
static int nkstat;
static DEFINE_MUTEX(kstat_lock);

/* Table locking, in two tiers.  kstat_lock (mutex) serialises WRITERS and the /proc read, because
 * writers resolve paths and that sleeps.  kstat_table_lock (spinlock) guards the table for the
 * READERS - the stat layer's entry tail and the show_map_vma kretprobe, hot paths where kstat_lock
 * cannot be taken; a reader holds it only long enough to copy the matching entry out and must never
 * copy_to_user under it.  Rule: resolve first (sleeping, outside), then swap (non-sleeping, inside) -
 * publishing a slot before it is filled is a bug (an empty slot used to be visible as soon as nkstat
 * was bumped, before kern_path() had even run). */
static DEFINE_SPINLOCK(kstat_table_lock);

/* The part of an entry a reader needs, copied out under kstat_table_lock. */
struct sus_kstat_snapshot {
	unsigned long spoofed_ino;
	unsigned long spoofed_dev;
	unsigned int spoofed_nlink;
	long long spoofed_size;
	long spoofed_atime_tv_sec;
	unsigned long spoofed_atime_tv_nsec;
	long spoofed_mtime_tv_sec;
	unsigned long spoofed_mtime_tv_nsec;
	long spoofed_ctime_tv_sec;
	unsigned long spoofed_ctime_tv_nsec;
	long long spoofed_blocks;
	long spoofed_blksize;
	unsigned int flags;
};

static void kstat_snapshot(const struct sus_kstat_entry *e,
			   struct sus_kstat_snapshot *s)
{
	s->spoofed_ino = e->spoofed_ino;
	s->spoofed_dev = e->spoofed_dev;
	s->spoofed_nlink = e->spoofed_nlink;
	s->spoofed_size = e->spoofed_size;
	s->spoofed_atime_tv_sec = e->spoofed_atime_tv_sec;
	s->spoofed_atime_tv_nsec = e->spoofed_atime_tv_nsec;
	s->spoofed_mtime_tv_sec = e->spoofed_mtime_tv_sec;
	s->spoofed_mtime_tv_nsec = e->spoofed_mtime_tv_nsec;
	s->spoofed_ctime_tv_sec = e->spoofed_ctime_tv_sec;
	s->spoofed_ctime_tv_nsec = e->spoofed_ctime_tv_nsec;
	s->spoofed_blocks = e->spoofed_blocks;
	s->spoofed_blksize = e->spoofed_blksize;
	s->flags = e->flags;
}

/* struct stat offsets (native 64-bit).  arm64 uses the asm-generic layout,
 * x86_64 its own: the two agree on every member this layer touches except
 * st_nlink, which asm-generic packs as u32 after st_mode (20) and x86_64
 * widens to unsigned long before it (16).  The width goes with it, so the
 * nlink write below copies ST_NLINK_WIDTH bytes, not sizeof(v32). */
#define ST_DEV_OFF          0
#define ST_INO_OFF          8
#ifdef __x86_64__
#define ST_NLINK_OFF        16
#define ST_NLINK_WIDTH      8
#else
#define ST_NLINK_OFF        20
#define ST_NLINK_WIDTH      4
#endif
#define ST_SIZE_OFF         48
#define ST_BLKSIZE_OFF      56
#define ST_BLOCKS_OFF       64
#define ST_ATIME_OFF        72
#define ST_ATIME_NSEC_OFF   80
#define ST_MTIME_OFF        88
#define ST_MTIME_NSEC_OFF   96
#define ST_CTIME_OFF        104
#define ST_CTIME_NSEC_OFF   112

/* The numbers above are a uapi contract, checked at build time instead of trusted: arm64 uses the
 * generic layout (__ARCH_WANT_NEW_STAT), and a DDK header change that moved a member would
 * otherwise corrupt the caller's stat buffer instead of failing this build. */
static_assert(offsetof(struct stat, st_dev) == ST_DEV_OFF, "stat.st_dev");
static_assert(offsetof(struct stat, st_ino) == ST_INO_OFF, "stat.st_ino");
static_assert(offsetof(struct stat, st_nlink) == ST_NLINK_OFF, "stat.st_nlink");
static_assert(sizeof(((struct stat *)0)->st_nlink) == ST_NLINK_WIDTH, "stat.st_nlink width");
static_assert(offsetof(struct stat, st_size) == ST_SIZE_OFF, "stat.st_size");
static_assert(offsetof(struct stat, st_blksize) == ST_BLKSIZE_OFF, "stat.st_blksize");
static_assert(offsetof(struct stat, st_blocks) == ST_BLOCKS_OFF, "stat.st_blocks");
static_assert(offsetof(struct stat, st_atime) == ST_ATIME_OFF, "stat.st_atime");
static_assert(offsetof(struct stat, st_mtime) == ST_MTIME_OFF, "stat.st_mtime");
static_assert(offsetof(struct stat, st_ctime) == ST_CTIME_OFF, "stat.st_ctime");

/* ---- the read gate ----
 *
 * Upstream gates every sus_kstat read on susfs_is_current_proc_umounted_app(), exactly
 * (TIF_PROC_UMOUNTED && current_uid().val >= 10000).  KernelSU's setuid_hook sets that flag only
 * with the SUSFS integration compiled into the kernel, which this kernel has none of, so the flag
 * is asked of KernelSU itself instead: susfs_is_current_proc_umounted_app() answers it with
 * ksu_uid_should_umount() (ksu_umount_gate.h, issue #34).  The old `uid >= 10000` proxy spoofed for
 * every app uid, the manager and su-granted apps included.
 * Without any gate the spoofing would be visible to root too, wider than upstream.  Writers
 * (supercall, /proc) are configuration and stay ungated. */
static bool susfs_kstat_gate_ok(void)
{
	return susfs_is_current_proc_umounted_app();
}

/* ---- table access ----
 * The *_table_* helpers are the ONLY places that modify kstat_entries or nkstat, each under
 * kstat_table_lock; their callers hold kstat_lock, making the index they pass stable. */

/* Cheapest possible gate for the READ paths: with nothing registered no lookup can match, which on
 * the stat layer's entry tail saves the two copy_from_user() reads the spoofers do before matching, and
 * on the show_map_vma kprobe (still armed after a `clear`) saves an uncontended spinlock per VMA
 * (the vfs_getattr kretprobe this sentence used to name is gone - see the note above struct
 * kstat_call_counters).
 * READ_ONCE is enough: nkstat is published only AFTER
 * the entry it counts has been written, under kstat_table_lock (kstat_table_append stores the entry,
 * then bumps the count), so a stale non-zero value only means work we used to do and a stale zero can
 * cost at most the single read racing the very first add; the authoritative test is the locked lookup.
 * sus_path uses the same idiom (READ_ONCE(sus_path_count)). */
static bool susfs_kstat_table_empty(void)
{
	return READ_ONCE(nkstat) == 0;
}

/* Hot-path lookup: match (ino, dev) and copy the entry out atomically w.r.t. the writers. */
static bool susfs_kstat_lookup(unsigned long ino, dev_t dev,
			       struct sus_kstat_snapshot *out)
{
	unsigned long flags;
	bool found = false;
	int i;

	spin_lock_irqsave(&kstat_table_lock, flags);
	for (i = 0; i < nkstat; i++) {
		if (kstat_entries[i].target_ino == ino &&
		    kstat_entries[i].target_dev == dev) {
			kstat_snapshot(&kstat_entries[i], out);
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&kstat_table_lock, flags);
	return found;
}

/* Writer-side lookup by path: safe without the spinlock, since every caller holds kstat_lock. */
static struct sus_kstat_entry *susfs_kstat_find_by_path(const char *path)
{
	int i;

	for (i = 0; i < nkstat; i++)
		if (!strcmp(kstat_entries[i].target_pathname, path))
			return &kstat_entries[i];
	return NULL;
}

/* Append a fully-prepared entry; -ENOSPC when the table is full. */
static int kstat_table_append(const struct sus_kstat_entry *src)
{
	unsigned long flags;
	int idx = -1;

	spin_lock_irqsave(&kstat_table_lock, flags);
	if (nkstat < SUS_KSTAT_MAX) {
		idx = nkstat;
		kstat_entries[idx] = *src;
		nkstat = idx + 1;
	}
	spin_unlock_irqrestore(&kstat_table_lock, flags);
	return idx;
}

/* Replace a live entry wholesale: the reader sees old or new, never a mix of the two. */
static void kstat_table_put(int idx, const struct sus_kstat_entry *src)
{
	unsigned long flags;

	spin_lock_irqsave(&kstat_table_lock, flags);
	if (idx >= 0 && idx < nkstat)
		kstat_entries[idx] = *src;
	spin_unlock_irqrestore(&kstat_table_lock, flags);
}

/* Re-target an entry, and optionally raise its flags. */
static void kstat_table_retarget(int idx, unsigned long ino, dev_t dev,
				 unsigned int add_flags)
{
	unsigned long flags;

	spin_lock_irqsave(&kstat_table_lock, flags);
	if (idx >= 0 && idx < nkstat) {
		kstat_entries[idx].target_ino = ino;
		kstat_entries[idx].target_dev = dev;
		kstat_entries[idx].flags |= add_flags;
	}
	spin_unlock_irqrestore(&kstat_table_lock, flags);
}

/* Remove by index: move the last entry into the hole, under the lock so a reader never sees that move half-done. */
static void kstat_table_del(int idx)
{
	unsigned long flags;

	spin_lock_irqsave(&kstat_table_lock, flags);
	if (idx >= 0 && idx < nkstat) {
		nkstat--;
		if (idx != nkstat)
			kstat_entries[idx] = kstat_entries[nkstat];
	}
	spin_unlock_irqrestore(&kstat_table_lock, flags);
}

static void kstat_table_clear(void)
{
	unsigned long flags;

	spin_lock_irqsave(&kstat_table_lock, flags);
	nkstat = 0;
	spin_unlock_irqrestore(&kstat_table_lock, flags);
}

/* ---- /proc/<pid>/maps coverage ----
 *
 * Upstream's susfs_sus_kstat_spoof_show_map_vma() rewrites dev/ino inside show_map_vma(), where
 * both are still locals, i.e. unreachable from an LKM - and re-printing the line would mean
 * reproducing the kernel's column padding (seq_setwidth()/seq_pad() are not exported, m->pad_until
 * is private state), while a line padded differently from its neighbours is itself a tell.  So the
 * already-formatted line is edited at the return of the function that printed it: the "maj:min ino"
 * run is located by rendering the REAL values the way fs/proc/task_mmu.c does (seq_put_hex_ll for
 * major/minor - lowercase, minimum width 2 - and seq_put_decimal_ull for the ino) and replaced by
 * the spoofed ones.  An earlier version DROPPED the whole line: that closed the stat-vs-maps
 * contradiction but changed the line count (a mapping listed for every process except this one is
 * its own signal) and left smaps unfiltered anyway, its header coming from another call site.
 *
 * The spoofed dev is stored as userspace sees st_dev (new_encode_dev), so it is decoded with
 * new_decode_dev(), the kernel's own inverse, before being printed in the maj:min column; upstream
 * substitutes target_dev into the RAW dev local instead, printing "0:fe4b" for a file whose stat()
 * says 254:75.  Armed on the first rule that spoofs ino or dev. */

/* Same buffer edit as the maps name/numbers rewrite in susfs_open_redirect.c: replace the first
 * occurrence of old[] with new[], growing only when the buffer has room. */
static bool kstat_buf_replace(struct seq_file *m, const char *old, size_t old_len,
			      const char *new, size_t new_len)
{
	char *buf = m->buf;
	size_t count = m->count, i, pos = 0;

	if (!old_len || !new_len || old_len > count)
		return false;
	for (i = 0; i + old_len <= count; i++) {
		if (!memcmp(buf + i, old, old_len)) {
			pos = i;
			break;
		}
	}
	if (i + old_len > count)
		return false;
	if (new_len > old_len && count + (new_len - old_len) >= m->size)
		return false;
	if (new_len != old_len)
		memmove(buf + pos + new_len, buf + pos + old_len, count - (pos + old_len));
	memcpy(buf + pos, new, new_len);
	m->count = count - old_len + new_len;
	return true;
}

struct kstat_map_args {
	struct seq_file *m;
	struct vm_area_struct *vma;
};

static atomic_t n_kstat_map_hits = ATOMIC_INIT(0);
static atomic_t n_kstat_map_rewrites = ATOMIC_INIT(0);

static int kstat_map_vma_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct kstat_map_args *a = (struct kstat_map_args *)ri->data;

	a->m = (struct seq_file *)susfs_sc_arg(regs, 0);
	a->vma = (struct vm_area_struct *)susfs_sc_arg(regs, 1);
	return 0;
}

static int kstat_map_vma_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	const struct kstat_map_args *a = (const struct kstat_map_args *)ri->data;
	struct seq_file *m = a->m;
	struct vm_area_struct *vma = a->vma;
	struct sus_kstat_snapshot snap;
	struct inode *inode;
	char old[48], new[48];
	unsigned int major, minor;
	int old_len, new_len;

	if (susfs_kstat_table_empty())
		return 0;
	if (!susfs_ptr_plausible(m) || !susfs_ptr_plausible(vma) ||
	    !m->buf || !m->count || !vma->vm_file)
		return 0;
	inode = file_inode(vma->vm_file);
	if (!inode)
		return 0;
	if (!susfs_kstat_gate_ok())
		return 0;
	if (!susfs_kstat_lookup(inode->i_ino,
				new_encode_dev(inode->i_sb->s_dev), &snap))
		return 0;
	if (!(snap.flags & (KSTAT_SPOOF_INO | KSTAT_SPOOF_DEV)))
		return 0;

	atomic_inc(&n_kstat_map_hits);

	/* What the kernel just printed is MAJOR()/MINOR() of the RAW s_dev. */
	old_len = scnprintf(old, sizeof(old), "%02x:%02x %lu",
			    (unsigned int)MAJOR(inode->i_sb->s_dev),
			    (unsigned int)MINOR(inode->i_sb->s_dev),
			    (unsigned long)inode->i_ino);

	if (snap.flags & KSTAT_SPOOF_DEV) {
		unsigned int enc = (unsigned int)snap.spoofed_dev;

		/* new_decode_dev(): the inverse of what cp_new_stat() encoded, so a file whose stat() says 254:75 prints "fe:4b" here too. */
		major = (enc & 0xfff00u) >> 8;
		minor = (enc & 0xffu) | ((enc >> 12) & 0xfff00u);
	} else {
		major = (unsigned int)MAJOR(inode->i_sb->s_dev);
		minor = (unsigned int)MINOR(inode->i_sb->s_dev);
	}
	new_len = scnprintf(new, sizeof(new), "%02x:%02x %lu", major, minor,
			    (snap.flags & KSTAT_SPOOF_INO)
				    ? snap.spoofed_ino
				    : (unsigned long)inode->i_ino);

	/* Keep the column width, space-padding the run: a SHORTER run would pull the name left by the
	 * difference and a line whose name does not line up with its neighbours is visible on its own.
	 * A LONGER run does shift it - the rare case (a spoofed ino with more digits than the real one). */
	while (new_len < old_len && new_len < (int)sizeof(new) - 1)
		new[new_len++] = ' ';

	if (old_len > 0 && new_len > 0 &&
	    kstat_buf_replace(m, old, (size_t)old_len, new, (size_t)new_len))
		atomic_inc(&n_kstat_map_rewrites);
	return 0;
}

static struct kretprobe krp_kstat_map_vma = {
	.kp.symbol_name = "show_map_vma",
	.entry_handler = kstat_map_vma_entry,
	.handler = kstat_map_vma_ret,
	.data_size = sizeof(struct kstat_map_args),
	.maxactive = 16,
};

static bool kstat_maps_registered;

/* Registering sleeps, so this runs from the rule-management paths (process context).  It is NOT
 * called under kstat_lock - both call sites arm after unlocking it, deliberately - so the
 * check-then-act below needs a lock of its own: register_kretprobe() answers -EINVAL once another
 * task has resolved .addr, and the failure branch clears .addr, which without this lock disarms a
 * probe that is LIVE (its breakpoint stays installed, and unregister_kretprobe() can no longer
 * take it out).  sus_mount_ctl_lock and or_lock exist for the same reason. */
static DEFINE_MUTEX(kstat_maps_ctl_lock);

/* Set by kstat_maps_disarm() while it holds the lock.  kstat_maps_arm() is reachable from the
 * /proc node (kstat_proc_write) as well as the supercall, and the node holds no module reference,
 * so without this a write that is already inside the handler could arm the kretprobe again after
 * the module started going away - its handler would then outlive the module. */
static bool kstat_maps_exiting;

static void kstat_maps_arm(void)
{
	int rc;

	mutex_lock(&kstat_maps_ctl_lock);
	if (kstat_maps_exiting) {
		mutex_unlock(&kstat_maps_ctl_lock);
		return;
	}
	if (kstat_maps_registered) {
		mutex_unlock(&kstat_maps_ctl_lock);
		return;
	}
	rc = register_kretprobe(&krp_kstat_map_vma);
	if (rc) {
		/* Every later rule operation retries this arming (kstat_maps_arm() is called from
		 * the add/update/clear paths and from the supercall), so the struct has to come back
		 * clear: a failure that happened after the symbol was resolved leaves .addr set, and
		 * the retry would be refused with -EINVAL instead of trying again - see
		 * susfs_kprobe.h.  Safe under the lock. */
		susfs_krp_forget_addr(&krp_kstat_map_vma);
		mutex_unlock(&kstat_maps_ctl_lock);
		pr_warn("susfs_kstat: register_kretprobe(show_map_vma) failed %d - maps keeps printing the real dev:ino\n",
			rc);
	} else {
		kstat_maps_registered = true;
		mutex_unlock(&kstat_maps_ctl_lock);
		SUSFS_LOGI("susfs_kstat: maps hook armed (kretprobe show_map_vma)\n");
	}
}

static void kstat_maps_disarm(void)
{
	mutex_lock(&kstat_maps_ctl_lock);
	/* The only caller is susfs_kstat_exit(), so "stop accepting arms" and "drop the probe" can
	 * be one critical section - exactly what the /proc-reachable arm path needs.  If a runtime
	 * disarm is ever wanted (say for `clear`), it must NOT come through here: the flag is
	 * permanent, so disarming at runtime would make kstat_maps_arm() refuse for the rest of the
	 * module's life.  Such a caller needs its own _locked() variant. */
	kstat_maps_exiting = true;
	if (!kstat_maps_registered) {
		mutex_unlock(&kstat_maps_ctl_lock);
		return;
	}
	unregister_kretprobe(&krp_kstat_map_vma);
	susfs_krp_forget_addr(&krp_kstat_map_vma);
	kstat_maps_registered = false;
	mutex_unlock(&kstat_maps_ctl_lock);
}

/* Why the vfs_getattr fallback is not here any more (it stood exactly where these counters now
 * live): it was a second hook, one layer lower - it rewrote struct kstat BEFORE cp_new_stat()/
 * cp_compat_stat()/cp_statx() copied it out - so it was the one place that also covered what the
 * newfstatat wrapper never saw: fstat(2), statx(2), a 32-bit caller's stat()/lstat()/fstat(), and
 * any kernel-internal caller of the exported vfs_getattr.  LTO inlines that whole chain, and on
 * device it was hit by 15 of 120 000 app-uid newfstatat calls while spoofing nothing, so it was
 * deleted rather than kept as coverage that does not fire - the same test the maps hook, __d_path
 * and pin_user_pages_remote failed.  What it covered is whitelisted here by number instead:
 * fstat(80) and statx(291) ride this same tracepoint, which is a superset of the stat entry
 * points this module serves.  The per-number counters below answer the question its two counters
 * existed for, since "registered" and "reached" are different claims everywhere in this module.
 *
 * Coverage today (this note used to list a gap; it is closed): the whitelist above covers every
 * statbuf entry point a 32-bit or 64-bit caller has on arm64 - the three native numbers, the four
 * struct stat64 numbers, the three struct compat_stat numbers (106/107/108) and AArch32's statx
 * (397).  Numbers in the stat range that are still NOT here (the compat numbers of other ABIs, and
 * anything a future kernel adds) are counted by kstat_note_unlisted() below.
 *
 * What this hook structurally CANNOT cover: anything that fills a statbuf without going through a
 * user syscall exit.  io_uring's IORING_OP_STATX / IORING_OP_FSTAT call vfs_statx() from a worker
 * or from io_uring_enter, so the LSM layer (sus_path's inode_getattr slot) still answers ENOENT for
 * a hidden file, but there is no wrapper tail to rewrite it and the caller sees the REAL numbers - see
 * COVERAGE.md's io_uring row.  The same is true of an in-kernel caller of vfs_getattr. */
struct kstat_call_counters {
	atomic_t calls;		/* this number returned 0: it reached the statbuf code */
	atomic_t ret_err;	/* this number returned -errno */
	atomic_t no_buf;	/* statbuf argument was NULL */
	atomic_t rewrite;	/* the buffer really changed */
	atomic_t miss_empty;	/* no rule registered at all */
	atomic_t miss_gate;	/* gate: not an app process KernelSU would umount modules for */
	atomic_t miss_lookup;	/* the (ino,dev) key matched no rule */
	atomic_t match_noflag;	/* matched, but this buffer has no field we impersonate */
	atomic_t uaccess;	/* copy_from_user/copy_to_user failed */
	atomic_t narrow;	/* matched, but a field was left ALONE: this buffer cannot represent
				 * the value (32-bit ino/nlink/size in struct compat_stat - the
				 * kernel answers those with -EOVERFLOW rather than truncate) */
};

#define KSTAT_COUNTERS(nm)						\
	static struct kstat_call_counters cnt_##nm = {			\
		.calls = ATOMIC_INIT(0), .ret_err = ATOMIC_INIT(0),	\
		.no_buf = ATOMIC_INIT(0), .rewrite = ATOMIC_INIT(0),	\
		.miss_empty = ATOMIC_INIT(0), .miss_gate = ATOMIC_INIT(0),\
		.miss_lookup = ATOMIC_INIT(0), .match_noflag = ATOMIC_INIT(0),\
		.uaccess = ATOMIC_INIT(0), .narrow = ATOMIC_INIT(0),	\
	}

KSTAT_COUNTERS(nfstatat);
KSTAT_COUNTERS(nfstat);
KSTAT_COUNTERS(statx);
KSTAT_COUNTERS(fstatat64);
KSTAT_COUNTERS(stat64);
KSTAT_COUNTERS(lstat64);
KSTAT_COUNTERS(fstat64);
/* struct compat_stat (AArch32 106/107/108) and AArch32 statx (397) get counters of their own:
 * "the 64-bit path works" says nothing about the 32-bit one, and mixing the two would hide exactly
 * that. */
KSTAT_COUNTERS(compat_stat);
KSTAT_COUNTERS(compat_statx);

/* "Stat-range numbers the dispatcher did not know": the whitelist above covers every statbuf entry
 * point arm64 gives either ABI (native 79/80/291, AArch32 106/107/108, 195/196/197, 327, 397), so a
 * hit here means the kernel grew a new one, a number belongs to another ABI's table, or a tracer
 * rewrote the syscall number.  The window is where stat numbers live (fs/stat.c's callers), not a
 * claim about any particular number. */
#define KSTAT_NR_SCAN_LO	106
#define KSTAT_NR_SCAN_HI	600
#define KSTAT_UNLISTED_SEEN	8

static atomic_t n_kstat_unlisted = ATOMIC_INIT(0);
static atomic_t kstat_unlisted_next = ATOMIC_INIT(0);
static long kstat_unlisted_seen[KSTAT_UNLISTED_SEEN];

static void kstat_note_unlisted(long nr)
{
	int slot;

	atomic_inc(&n_kstat_unlisted);
	slot = atomic_inc_return(&kstat_unlisted_next) - 1;
	kstat_unlisted_seen[slot % KSTAT_UNLISTED_SEEN] = nr;
}

struct kstat_call_state {
	struct kstat_call_counters *cnt;
	bool compat;
};

/* rewrite the requested fields of the native user statbuf */
static bool susfs_kstat_spoof_statbuf(struct kstat_call_state *st, unsigned long statbuf)
{
	/* Snapshot, not a pointer into the table: a concurrent writer must not be able to retarget the
	 * entry between the match and the copy_to_user below, which must not run under a spinlock. */
	struct sus_kstat_snapshot snap;
	const struct sus_kstat_snapshot *e = &snap;
	unsigned long ino = 0, dev = 0;
	unsigned long v;
	unsigned int v32;
	long long v64;
	long sl;
	bool rewrote = false;

	if (susfs_kstat_table_empty()) {
		atomic_inc(&st->cnt->miss_empty);
		return false;
	}
	if (!susfs_kstat_gate_ok()) {
		atomic_inc(&st->cnt->miss_gate);
		return false;
	}

	if (copy_from_user(&ino, (void __user *)(statbuf + ST_INO_OFF), sizeof(ino))) {
		atomic_inc(&st->cnt->uaccess);
		return false;
	}
	if (copy_from_user(&dev, (void __user *)(statbuf + ST_DEV_OFF), sizeof(dev))) {
		atomic_inc(&st->cnt->uaccess);
		return false;
	}

	if (!susfs_kstat_lookup(ino, dev, &snap)) {
		atomic_inc(&st->cnt->miss_lookup);
		return false;
	}

	if (e->flags & KSTAT_SPOOF_INO) {
		v = e->spoofed_ino;
		if (copy_to_user((void __user *)(statbuf + ST_INO_OFF), &v, sizeof(v))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_DEV) {
		v = e->spoofed_dev;
		if (copy_to_user((void __user *)(statbuf + ST_DEV_OFF), &v, sizeof(v))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_NLINK) {
		v64 = e->spoofed_nlink;
		if (copy_to_user((void __user *)(statbuf + ST_NLINK_OFF), &v64, ST_NLINK_WIDTH)) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_SIZE) {
		v64 = e->spoofed_size;
		if (copy_to_user((void __user *)(statbuf + ST_SIZE_OFF), &v64, sizeof(v64))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_BLKSIZE) {
		v32 = (unsigned int)e->spoofed_blksize;
		if (copy_to_user((void __user *)(statbuf + ST_BLKSIZE_OFF), &v32, sizeof(v32))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_BLOCKS) {
		v64 = e->spoofed_blocks;
		if (copy_to_user((void __user *)(statbuf + ST_BLOCKS_OFF), &v64, sizeof(v64))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_ATIME_TV_SEC) {
		sl = e->spoofed_atime_tv_sec;
		if (copy_to_user((void __user *)(statbuf + ST_ATIME_OFF), &sl, sizeof(sl))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_ATIME_TV_NSEC) {
		v = e->spoofed_atime_tv_nsec;
		if (copy_to_user((void __user *)(statbuf + ST_ATIME_NSEC_OFF), &v, sizeof(v))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_MTIME_TV_SEC) {
		sl = e->spoofed_mtime_tv_sec;
		if (copy_to_user((void __user *)(statbuf + ST_MTIME_OFF), &sl, sizeof(sl))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_MTIME_TV_NSEC) {
		v = e->spoofed_mtime_tv_nsec;
		if (copy_to_user((void __user *)(statbuf + ST_MTIME_NSEC_OFF), &v, sizeof(v))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_CTIME_TV_SEC) {
		sl = e->spoofed_ctime_tv_sec;
		if (copy_to_user((void __user *)(statbuf + ST_CTIME_OFF), &sl, sizeof(sl))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_CTIME_TV_NSEC) {
		v = e->spoofed_ctime_tv_nsec;
		if (copy_to_user((void __user *)(statbuf + ST_CTIME_NSEC_OFF), &v, sizeof(v))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}

	if (!rewrote)
		atomic_inc(&st->cnt->match_noflag);
	else
		atomic_inc(&st->cnt->rewrite);
	return rewrote;
}

/* AArch32 (compat) statbuf layouts.  There are TWO, and which one a syscall fills is decided by
 * its NUMBER, not by the caller being 32-bit:
 *   __NR_stat64 195, __NR_lstat64 196, __NR_fstat64 197, __NR_fstatat64 327 -> struct stat64
 *       (arch/arm64/include/asm/stat.h), filled by cp_new_stat64() via SYSCALL_DEFINE2(stat64)/
 *       (lstat64)/(fstat64) and SYSCALL_DEFINE4(fstatat64) (fs/stat.c, __ARCH_WANT_COMPAT_STAT64);
 *       this struct is NOT struct compat_stat.  All four are handled below (the first three keep
 *       statbuf in args[1], fstatat64 in args[2]).
 *   __NR_stat 106, __NR_lstat 107, __NR_fstat 108 -> struct compat_stat
 *       (arch/arm64/include/asm/compat.h) via cp_compat_stat().  They ARE mapped in this tree's
 *       arch/arm64/include/asm/unistd32.h (106 -> compat_sys_newstat, 107 -> newlstat, 108 ->
 *       newfstat) and they ARE handled now, by susfs_kstat_spoof_compat_stat() below - the third
 *       layout, whose offsets are pinned with offsetof()/static_assert() right after this comment.
 *       Their key is the same (ino, new_encode_dev(dev)) the other families carry, so the rule table
 *       and susfs_kstat_lookup() are shared; what differs is the FIELD WIDTHS: st_ino is 32-bit,
 *       st_nlink is 16-bit and st_size/st_blksize/st_blocks are 32-bit compat_off_t, so the write
 *       side refuses a value this struct cannot represent exactly where the kernel refuses it too
 *       (cp_compat_stat() answers -EOVERFLOW for an ino/nlink that does not fit and for a size above
 *       MAX_NON_LFS) - that is what the per-number `narrow` counter counts.
 *       AArch32 statx 397 is NOT a layout of its own: unistd32.h maps it to the native statx body,
 *       and struct statx is a fixed-width uapi struct, so the STATX offsets below serve it unchanged
 *       (only the buffer pointer is 32-bit and goes through compat_ptr()).
 *       Struct stat64 and struct compat_stat must never be confused: this is what an earlier version
 *       of this file did - it read the lookup key from struct stat64's +4 (the HIGH half of its
 *       64-bit st_dev) for the 106/107/108 buffers, so the key could never match, and a match would
 *       have written into st_dev/st_rdev.
 *
 * ALIGNMENT (these numbers were established twice): compat_u64/compat_s64 are
 * __attribute__((aligned(4))) only when CONFIG_COMPAT_FOR_U64_ALIGNMENT is set
 * (include/asm-generic/compat.h), and only the 32-bit arm architecture selects it - so on this
 * arm64 kernel the plain `typedef s64 compat_s64;` applies, u64 members keep natural 8-byte
 * alignment, and st_size is at +48 (not +44), st_ino at +96 (not +88).  A 4-byte-aligned read is
 * not a silent near-miss - measured on device with tools/susfs_compat_stat, a 6-byte file reported
 * size=25769803776 = 6 << 32 (low half from padding, high half from the value).  The offsets below
 * are what that client and the kernel agree on. */
#define STAT64_ST_DEV_OFF       0	/* compat_u64 */
#define STAT64_ST_BROKEN_INO_OFF 12	/* compat_ulong_t __st_ino (one of the two ino fields) */
#define STAT64_ST_NLINK_OFF     20	/* compat_uint_t */
#define STAT64_ST_SIZE_OFF      48	/* compat_s64 */
#define STAT64_ST_BLKSIZE_OFF   56	/* compat_ulong_t */
#define STAT64_ST_BLOCKS_OFF    64	/* compat_u64 */
#define STAT64_ST_ATIME_OFF     72
#define STAT64_ST_ATIME_NSEC_OFF 76
#define STAT64_ST_MTIME_OFF     80
#define STAT64_ST_MTIME_NSEC_OFF 84
#define STAT64_ST_CTIME_OFF     88
#define STAT64_ST_CTIME_NSEC_OFF 92
#define STAT64_ST_INO_OFF       96	/* compat_u64 st_ino (also written; the KEY is read from +12) */
#define STAT64_ST_SIZE          104

/* struct compat_stat (the AArch32 stat()/lstat()/fstat() layout, 106/107/108).  Unlike the struct
 * stat64 block above these are taken from the kernel's OWN type (linux/compat.h -> asm/compat.h),
 * so a layout drift in any of the eight build variants fails the build instead of mis-writing a
 * user buffer.  The numbers are identical on 5.15 and 6.6 (checked field by field); the asserts
 * below are what keeps that true for 6.12/6.18 too.
 *
 * Field widths are the reason this needs its own write path and not a re-read of the stat64 one:
 *   st_dev u32 (new_encode_dev, so the same key as every other family), st_ino u32, st_mode u16,
 *   st_nlink u16, st_uid/st_gid u16, st_rdev u32, st_size/st_blksize/st_blocks s32 (compat_off_t),
 *   st_atime/st_mtime/st_ctime s32 (old_time32_t) and their _nsec u32 (compat_ulong_t).
 * cp_compat_stat() (fs/stat.c, identical in 5.15 and 6.6) writes exactly those, and it *refuses*
 * three of them instead of truncating: an ino or nlink that does not fit its 32/16 bits, and a size
 * above MAX_NON_LFS, all answer -EOVERFLOW.  The write-back below follows the same rule (skip the
 * field, count it in `narrow`) rather than writing a value the kernel itself would never have put
 * there.  st_blksize/st_blocks/the timestamps are plain assignments in the kernel, so those are
 * written at their own width like it does. */
#define COMPAT_STAT_ST_DEV_OFF		((int)offsetof(struct compat_stat, st_dev))
#define COMPAT_STAT_ST_INO_OFF		((int)offsetof(struct compat_stat, st_ino))
#define COMPAT_STAT_ST_NLINK_OFF	((int)offsetof(struct compat_stat, st_nlink))
#define COMPAT_STAT_ST_SIZE_OFF		((int)offsetof(struct compat_stat, st_size))
#define COMPAT_STAT_ST_BLKSIZE_OFF	((int)offsetof(struct compat_stat, st_blksize))
#define COMPAT_STAT_ST_BLOCKS_OFF	((int)offsetof(struct compat_stat, st_blocks))
#define COMPAT_STAT_ST_ATIME_OFF	((int)offsetof(struct compat_stat, st_atime))
#define COMPAT_STAT_ST_ATIME_NSEC_OFF	((int)offsetof(struct compat_stat, st_atime_nsec))
#define COMPAT_STAT_ST_MTIME_OFF	((int)offsetof(struct compat_stat, st_mtime))
#define COMPAT_STAT_ST_MTIME_NSEC_OFF	((int)offsetof(struct compat_stat, st_mtime_nsec))
#define COMPAT_STAT_ST_CTIME_OFF	((int)offsetof(struct compat_stat, st_ctime))
#define COMPAT_STAT_ST_CTIME_NSEC_OFF	((int)offsetof(struct compat_stat, st_ctime_nsec))

static_assert(offsetof(struct compat_stat, st_dev) == 0, "compat_stat.st_dev");
static_assert(offsetof(struct compat_stat, st_ino) == 4, "compat_stat.st_ino");
static_assert(offsetof(struct compat_stat, st_mode) == 8, "compat_stat.st_mode");
static_assert(offsetof(struct compat_stat, st_nlink) == 10, "compat_stat.st_nlink");
static_assert(offsetof(struct compat_stat, st_rdev) == 16, "compat_stat.st_rdev");
static_assert(offsetof(struct compat_stat, st_size) == 20, "compat_stat.st_size");
static_assert(offsetof(struct compat_stat, st_blksize) == 24, "compat_stat.st_blksize");
static_assert(offsetof(struct compat_stat, st_blocks) == 28, "compat_stat.st_blocks");
static_assert(offsetof(struct compat_stat, st_atime) == 32, "compat_stat.st_atime");
static_assert(offsetof(struct compat_stat, st_atime_nsec) == 36, "compat_stat.st_atime_nsec");
static_assert(offsetof(struct compat_stat, st_mtime) == 40, "compat_stat.st_mtime");
static_assert(offsetof(struct compat_stat, st_mtime_nsec) == 44, "compat_stat.st_mtime_nsec");
static_assert(offsetof(struct compat_stat, st_ctime) == 48, "compat_stat.st_ctime");
static_assert(offsetof(struct compat_stat, st_ctime_nsec) == 52, "compat_stat.st_ctime_nsec");
static_assert(sizeof(struct compat_stat) == 64, "compat_stat size");
/* The widths the write-back has to respect (see the note above): if any of these ever changed, the
 * `narrow` decisions below would silently become wrong. */
static_assert(sizeof(((struct compat_stat *)0)->st_dev) == 4, "compat_stat.st_dev width");
static_assert(sizeof(((struct compat_stat *)0)->st_ino) == 4, "compat_stat.st_ino width");
static_assert(sizeof(((struct compat_stat *)0)->st_nlink) == 2, "compat_stat.st_nlink width");
static_assert(sizeof(((struct compat_stat *)0)->st_size) == 4, "compat_stat.st_size width");
static_assert(sizeof(((struct compat_stat *)0)->st_blksize) == 4, "compat_stat.st_blksize width");
static_assert(sizeof(((struct compat_stat *)0)->st_blocks) == 4, "compat_stat.st_blocks width");
static_assert(sizeof(((struct compat_stat *)0)->st_atime) == 4, "compat_stat.st_atime width");
static_assert(sizeof(((struct compat_stat *)0)->st_atime_nsec) == 4, "compat_stat.st_atime_nsec width");

/* ARM EABI syscall numbers that fill struct stat64 (arch/arm64/include/asm/unistd32.h; the
 * statbuf argument position is from fs/stat.c's COMPAT_SYSCALL_DEFINE2/4). */
/* AArch32 stat numbers, from the 32-bit ARM table (arch/arm/tools/syscall.tbl) - an arm64 tree
 * has no macro for them.  They are only reached for a task running that ABI: arm64's native
 * 195/196/197 are shmctl/shmat/shmdt (whose second argument is an address or a small integer,
 * not a statbuf) and its native 79/80 are newfstatat/fstat, which AArch32 numbers 79/80 give to
 * settimeofday/getgroups. */
#define COMPAT_FSTATAT64_NR	327	/* fstatat64(dfd, path, statbuf, flag) */
#define COMPAT_STAT64_NR	195	/* stat64(path, statbuf) */
#define COMPAT_LSTAT64_NR	196	/* lstat64(path, statbuf) */
#define COMPAT_FSTAT64_NR	197	/* fstat64(fd, statbuf) */
/* struct compat_stat trio: statbuf is args[1] in all three (COMPAT_SYSCALL_DEFINE2(newstat,
 * filename, statbuf) and DEFINE2(newfstat, fd, statbuf) in fs/stat.c), and all three end in
 * cp_compat_stat(). */
#define COMPAT_STAT_NR		106	/* stat(path, struct compat_stat *) */
#define COMPAT_LSTAT_NR		107	/* lstat(path, struct compat_stat *) */
#define COMPAT_FSTAT_NR		108	/* fstat(fd, struct compat_stat *) */
/* AArch32 statx: yes, 32-bit callers have it (unistd32.h:397), and it is the NATIVE statx body -
 * fs/stat.c has no COMPAT_SYSCALL_DEFINE for statx on arm64 - filling the same 256-byte
 * struct statx at a zero-extended 32-bit pointer.  statbuf is args[4] and the requested mask
 * args[3], exactly as in the native case; only the counter differs (see KSTAT_COUNTERS above). */
#define COMPAT_STATX_NR		397	/* statx(dfd, path, flags, mask, struct statx *) */

struct kstat_nr_entry {
	long nr;
	const char *name;
	struct kstat_call_counters *cnt;
};

static struct kstat_nr_entry kstat_nr_table[] = {
	{ __NR_newfstatat, "newfstatat", &cnt_nfstatat },
	{ __NR_fstat,      "fstat",      &cnt_nfstat },
	{ __NR_statx,      "statx",      &cnt_statx },
	{ COMPAT_FSTATAT64_NR, "fstatat64/compat", &cnt_fstatat64 },
	{ COMPAT_STAT64_NR,    "stat64/compat",    &cnt_stat64 },
	{ COMPAT_LSTAT64_NR,   "lstat64/compat",   &cnt_lstat64 },
	{ COMPAT_FSTAT64_NR,   "fstat64/compat",   &cnt_fstat64 },
	/* The struct compat_stat trio and AArch32 statx: separate rows on purpose - a 32-bit caller
	 * that only ever uses stat()/lstat()/fstat() (no stat64) would otherwise be invisible in this
	 * table, and that is precisely the case the offsets above exist for. */
	{ COMPAT_STAT_NR,      "stat/compat",      &cnt_compat_stat },
	{ COMPAT_LSTAT_NR,     "lstat/compat",     &cnt_compat_stat },
	{ COMPAT_FSTAT_NR,     "fstat/compat",     &cnt_compat_stat },
	{ COMPAT_STATX_NR,     "statx/compat",     &cnt_compat_statx },
};

/* compat (32-bit) statbuf: struct stat64 (see above) - the layout all four handled syscalls use. */
static bool susfs_kstat_spoof_compat_statbuf(struct kstat_call_state *st, unsigned long statbuf)
{
	struct sus_kstat_snapshot snap;
	const struct sus_kstat_snapshot *e = &snap;
	unsigned long long v64;
	unsigned int ino = 0, dev = 0;
	unsigned int v32;
	int v;
	bool rewrote = false;

	if (susfs_kstat_table_empty()) {
		atomic_inc(&st->cnt->miss_empty);
		return false;
	}
	if (!susfs_kstat_gate_ok()) {
		atomic_inc(&st->cnt->miss_gate);
		return false;
	}

	if (copy_from_user(&ino, (void __user *)(statbuf + STAT64_ST_BROKEN_INO_OFF), sizeof(ino))) {
		atomic_inc(&st->cnt->uaccess);
		return false;
	}
	if (copy_from_user(&dev, (void __user *)(statbuf + STAT64_ST_DEV_OFF), sizeof(dev))) {
		atomic_inc(&st->cnt->uaccess);
		return false;
	}
	if (!susfs_kstat_lookup(ino, dev, &snap)) {
		atomic_inc(&st->cnt->miss_lookup);
		return false;
	}

	if (e->flags & KSTAT_SPOOF_INO) {
		v32 = (unsigned int)e->spoofed_ino;
		if (copy_to_user((void __user *)(statbuf + STAT64_ST_BROKEN_INO_OFF), &v32, sizeof(v32))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		v64 = (unsigned long long)e->spoofed_ino;
		if (copy_to_user((void __user *)(statbuf + STAT64_ST_INO_OFF), &v64, sizeof(v64))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_DEV) {
		v64 = (unsigned long long)e->spoofed_dev;
		if (copy_to_user((void __user *)(statbuf + STAT64_ST_DEV_OFF), &v64, sizeof(v64))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_NLINK) {
		v32 = (unsigned int)e->spoofed_nlink;
		if (copy_to_user((void __user *)(statbuf + STAT64_ST_NLINK_OFF), &v32, sizeof(v32))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_SIZE) {
		v64 = (unsigned long long)e->spoofed_size;
		if (copy_to_user((void __user *)(statbuf + STAT64_ST_SIZE_OFF), &v64, sizeof(v64))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_BLKSIZE) {
		v32 = (unsigned int)e->spoofed_blksize;
		if (copy_to_user((void __user *)(statbuf + STAT64_ST_BLKSIZE_OFF), &v32, sizeof(v32))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_BLOCKS) {
		v64 = (unsigned long long)e->spoofed_blocks;
		if (copy_to_user((void __user *)(statbuf + STAT64_ST_BLOCKS_OFF), &v64, sizeof(v64))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_ATIME_TV_SEC) {
		v = (int)e->spoofed_atime_tv_sec;
		if (copy_to_user((void __user *)(statbuf + STAT64_ST_ATIME_OFF), &v, sizeof(v))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_ATIME_TV_NSEC) {
		v32 = (unsigned int)e->spoofed_atime_tv_nsec;
		if (copy_to_user((void __user *)(statbuf + STAT64_ST_ATIME_NSEC_OFF), &v32, sizeof(v32))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_MTIME_TV_SEC) {
		v = (int)e->spoofed_mtime_tv_sec;
		if (copy_to_user((void __user *)(statbuf + STAT64_ST_MTIME_OFF), &v, sizeof(v))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_MTIME_TV_NSEC) {
		v32 = (unsigned int)e->spoofed_mtime_tv_nsec;
		if (copy_to_user((void __user *)(statbuf + STAT64_ST_MTIME_NSEC_OFF), &v32, sizeof(v32))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_CTIME_TV_SEC) {
		v = (int)e->spoofed_ctime_tv_sec;
		if (copy_to_user((void __user *)(statbuf + STAT64_ST_CTIME_OFF), &v, sizeof(v))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_CTIME_TV_NSEC) {
		v32 = (unsigned int)e->spoofed_ctime_tv_nsec;
		if (copy_to_user((void __user *)(statbuf + STAT64_ST_CTIME_NSEC_OFF), &v32, sizeof(v32))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}

	if (!rewrote)
		atomic_inc(&st->cnt->match_noflag);
	else
		atomic_inc(&st->cnt->rewrite);
	return rewrote;
}

/* struct compat_stat (AArch32 stat/lstat/fstat, 106/107/108).  Same rule table and same lookup key
 * as everything else; what this function exists for is the different field widths - see the note
 * above COMPAT_STAT_ST_DEV_OFF.  The three refusals below mirror cp_compat_stat()'s -EOVERFLOW
 * guards (ino, nlink, size): the kernel never puts a value in those fields that does not fit, so
 * neither does this - the field keeps what the kernel wrote and the `narrow` counter is bumped,
 * rather than handing the caller a truncated (and, for size, negative) number. */
static bool susfs_kstat_spoof_compat_stat(struct kstat_call_state *st, unsigned long statbuf)
{
	struct sus_kstat_snapshot snap;
	const struct sus_kstat_snapshot *e = &snap;
	unsigned int ino = 0, dev = 0, v32;
	unsigned short v16;
	int v;
	bool rewrote = false;

	if (susfs_kstat_table_empty()) {
		atomic_inc(&st->cnt->miss_empty);
		return false;
	}
	if (!susfs_kstat_gate_ok()) {
		atomic_inc(&st->cnt->miss_gate);
		return false;
	}

	if (copy_from_user(&ino, (void __user *)(statbuf + COMPAT_STAT_ST_INO_OFF), sizeof(ino))) {
		atomic_inc(&st->cnt->uaccess);
		return false;
	}
	if (copy_from_user(&dev, (void __user *)(statbuf + COMPAT_STAT_ST_DEV_OFF), sizeof(dev))) {
		atomic_inc(&st->cnt->uaccess);
		return false;
	}
	if (!susfs_kstat_lookup(ino, dev, &snap)) {
		atomic_inc(&st->cnt->miss_lookup);
		return false;
	}

	if (e->flags & KSTAT_SPOOF_INO) {
		if (e->spoofed_ino > 0xffffffffUL) {
			atomic_inc(&st->cnt->narrow);
		} else {
			v32 = (unsigned int)e->spoofed_ino;
			if (copy_to_user((void __user *)(statbuf + COMPAT_STAT_ST_INO_OFF), &v32, sizeof(v32))) {
				atomic_inc(&st->cnt->uaccess);
				return false;
			}
			rewrote = true;
		}
	}
	if (e->flags & KSTAT_SPOOF_DEV) {
		/* new_encode_dev() of a dev_t is a u32 by construction: always representable. */
		v32 = (unsigned int)e->spoofed_dev;
		if (copy_to_user((void __user *)(statbuf + COMPAT_STAT_ST_DEV_OFF), &v32, sizeof(v32))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_NLINK) {
		if (e->spoofed_nlink > 0xffffU) {
			atomic_inc(&st->cnt->narrow);
		} else {
			v16 = (unsigned short)e->spoofed_nlink;
			if (copy_to_user((void __user *)(statbuf + COMPAT_STAT_ST_NLINK_OFF), &v16, sizeof(v16))) {
				atomic_inc(&st->cnt->uaccess);
				return false;
			}
			rewrote = true;
		}
	}
	if (e->flags & KSTAT_SPOOF_SIZE) {
		if (e->spoofed_size < 0 || e->spoofed_size > 0x7fffffffLL) {
			atomic_inc(&st->cnt->narrow);
		} else {
			v = (int)e->spoofed_size;
			if (copy_to_user((void __user *)(statbuf + COMPAT_STAT_ST_SIZE_OFF), &v, sizeof(v))) {
				atomic_inc(&st->cnt->uaccess);
				return false;
			}
			rewrote = true;
		}
	}
	if (e->flags & KSTAT_SPOOF_BLKSIZE) {
		v = (int)e->spoofed_blksize;
		if (copy_to_user((void __user *)(statbuf + COMPAT_STAT_ST_BLKSIZE_OFF), &v, sizeof(v))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_BLOCKS) {
		v = (int)e->spoofed_blocks;
		if (copy_to_user((void __user *)(statbuf + COMPAT_STAT_ST_BLOCKS_OFF), &v, sizeof(v))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_ATIME_TV_SEC) {
		v = (int)e->spoofed_atime_tv_sec;
		if (copy_to_user((void __user *)(statbuf + COMPAT_STAT_ST_ATIME_OFF), &v, sizeof(v))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_ATIME_TV_NSEC) {
		v32 = (unsigned int)e->spoofed_atime_tv_nsec;
		if (copy_to_user((void __user *)(statbuf + COMPAT_STAT_ST_ATIME_NSEC_OFF), &v32, sizeof(v32))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_MTIME_TV_SEC) {
		v = (int)e->spoofed_mtime_tv_sec;
		if (copy_to_user((void __user *)(statbuf + COMPAT_STAT_ST_MTIME_OFF), &v, sizeof(v))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_MTIME_TV_NSEC) {
		v32 = (unsigned int)e->spoofed_mtime_tv_nsec;
		if (copy_to_user((void __user *)(statbuf + COMPAT_STAT_ST_MTIME_NSEC_OFF), &v32, sizeof(v32))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_CTIME_TV_SEC) {
		v = (int)e->spoofed_ctime_tv_sec;
		if (copy_to_user((void __user *)(statbuf + COMPAT_STAT_ST_CTIME_OFF), &v, sizeof(v))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_CTIME_TV_NSEC) {
		v32 = (unsigned int)e->spoofed_ctime_tv_nsec;
		if (copy_to_user((void __user *)(statbuf + COMPAT_STAT_ST_CTIME_NSEC_OFF), &v32, sizeof(v32))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}

	if (!rewrote)
		atomic_inc(&st->cnt->match_noflag);
	else
		atomic_inc(&st->cnt->rewrite);
	return rewrote;
}

#define STATX_F_INO		STATX_INO
#define STATX_F_NLINK		STATX_NLINK
#define STATX_F_SIZE		STATX_SIZE
#define STATX_F_BLOCKS		STATX_BLOCKS

struct susfs_statx_offs {
	unsigned short dev, dev_minor, ino, nlink, size, blksize, blocks;
	unsigned short atime_sec, atime_nsec, mtime_sec, mtime_nsec,
		       ctime_sec, ctime_nsec, mask;
};

static const struct susfs_statx_offs kstat_statx_offs = {
	.dev = offsetof(struct statx, stx_dev_major),
	/* statx is the one family that hands out the two device halves separately; the table
	 * stores the encoded value st_dev carries, so the lookup key has to be rebuilt from
	 * both halves before the lookup below. */
	.dev_minor = offsetof(struct statx, stx_dev_minor),
	.ino = offsetof(struct statx, stx_ino),
	.nlink = offsetof(struct statx, stx_nlink),
	.size = offsetof(struct statx, stx_size),
	.blksize = offsetof(struct statx, stx_blksize),
	.blocks = offsetof(struct statx, stx_blocks),
	.atime_sec = offsetof(struct statx, stx_atime),
	.atime_nsec = offsetof(struct statx, stx_atime) + offsetof(struct timespec64, tv_nsec),
	.mtime_sec = offsetof(struct statx, stx_mtime),
	.mtime_nsec = offsetof(struct statx, stx_mtime) + offsetof(struct timespec64, tv_nsec),
	.ctime_sec = offsetof(struct statx, stx_ctime),
	.ctime_nsec = offsetof(struct statx, stx_ctime) + offsetof(struct timespec64, tv_nsec),
	.mask = offsetof(struct statx, stx_mask),
};

static_assert(offsetof(struct statx, stx_mask) == 0x00, "statx.stx_mask");
static_assert(offsetof(struct statx, stx_blksize) == 0x04, "statx.stx_blksize");
static_assert(offsetof(struct statx, stx_attributes) == 0x08, "statx.stx_attributes");
static_assert(offsetof(struct statx, stx_nlink) == 0x10, "statx.stx_nlink");
static_assert(offsetof(struct statx, stx_ino) == 0x20, "statx.stx_ino");
static_assert(offsetof(struct statx, stx_size) == 0x28, "statx.stx_size");
static_assert(offsetof(struct statx, stx_blocks) == 0x30, "statx.stx_blocks");
static_assert(offsetof(struct statx, stx_atime) == 0x40, "statx.stx_atime");
static_assert(offsetof(struct statx, stx_btime) == 0x50, "statx.stx_btime");
static_assert(offsetof(struct statx, stx_ctime) == 0x60, "statx.stx_ctime");
static_assert(offsetof(struct statx, stx_mtime) == 0x70, "statx.stx_mtime");

static_assert(offsetof(struct statx, stx_dev_major) == 0x88, "statx.stx_dev_major");
static_assert(offsetof(struct statx, stx_dev_minor) == 0x8c, "statx.stx_dev_minor");
static_assert(offsetof(struct statx, stx_mnt_id) == 0x90, "statx.stx_mnt_id");
static_assert(sizeof(struct statx) == 256, "statx size");
static_assert(sizeof(((struct statx *)0)->stx_ino) == 8, "statx stx_ino is u64");
static_assert(sizeof(((struct statx *)0)->stx_size) == 8, "statx stx_size is u64");

/* Read a field of the buffer, write the spoofed value back only when the mask carries the field. */
#define STATX_FIELD_U64(off, val, mbit)						\
	do {									\
		if (eff & (mbit)) {						\
			u64 __v = (u64)(val);					\
										\
			if (copy_to_user((void __user *)(sbuf + (off)), &__v,	\
					 sizeof(__v))) {			\
				atomic_inc(&st->cnt->uaccess);			\
				return false;					\
			}							\
			rewrote = true;						\
		}								\
	} while (0)

#define STATX_FIELD_S32(off, val, mbit)						\
	do {									\
		if (eff & (mbit)) {						\
			s32 __v = (s32)(val);					\
			u32 __u = (u32)__v;					\
										\
			if (copy_to_user((void __user *)(sbuf + (off)), &__u,	\
					 sizeof(__u))) {			\
				atomic_inc(&st->cnt->uaccess);			\
				return false;					\
			}							\
			rewrote = true;						\
		}								\
	} while (0)

#define STATX_FIELD_U32(off, val, mbit)						\
	do {									\
		if (eff & (mbit)) {						\
			u32 __v = (u32)(val);					\
										\
			if (copy_to_user((void __user *)(sbuf + (off)), &__v,	\
					 sizeof(__v))) {			\
				atomic_inc(&st->cnt->uaccess);			\
				return false;					\
			}							\
			rewrote = true;						\
		}								\
	} while (0)

/* returns true when the buffer was really changed */
static bool susfs_kstat_spoof_statx(struct kstat_call_state *st, unsigned long sbuf,
				    u32 req_mask)
{
	const struct susfs_statx_offs *o = &kstat_statx_offs;
	struct sus_kstat_snapshot snap;
	const struct sus_kstat_snapshot *e = &snap;
	unsigned long ino = 0;
	unsigned int dev = 0;
	u32 dev_major = 0, dev_minor = 0;
	u32 buf_mask = 0, eff;
	bool rewrote = false;

	if (susfs_kstat_table_empty()) {
		atomic_inc(&st->cnt->miss_empty);
		return false;
	}
	if (!susfs_kstat_gate_ok()) {
		atomic_inc(&st->cnt->miss_gate);
		return false;
	}

	if (copy_from_user(&ino, (void __user *)(sbuf + o->ino), sizeof(ino))) {
		atomic_inc(&st->cnt->uaccess);
		return false;
	}
	/* statx reports the device as two halves, while the table stores the ENCODED value the
	 * other stat families put in st_dev (sus_kstat_entry.dev): read both halves and encode
	 * them the same way, or the lookup key can never match and the whole statx spoof is dead.
	 * (Reading only stx_dev_major here is exactly that bug.) */
	if (copy_from_user(&dev_major, (void __user *)(sbuf + o->dev), sizeof(dev_major))) {
		atomic_inc(&st->cnt->uaccess);
		return false;
	}
	if (copy_from_user(&dev_minor, (void __user *)(sbuf + o->dev_minor), sizeof(dev_minor))) {
		atomic_inc(&st->cnt->uaccess);
		return false;
	}
	dev = new_encode_dev(MKDEV(dev_major, dev_minor));
	if (copy_from_user(&buf_mask, (void __user *)(sbuf + o->mask), sizeof(buf_mask))) {
		atomic_inc(&st->cnt->uaccess);
		return false;
	}

	if (!susfs_kstat_lookup(ino, dev, &snap)) {
		atomic_inc(&st->cnt->miss_lookup);
		return false;
	}

	eff = req_mask | buf_mask;

	if (e->flags & KSTAT_SPOOF_INO)
		STATX_FIELD_U64(o->ino, e->spoofed_ino, STATX_F_INO);
	if (e->flags & KSTAT_SPOOF_NLINK)
		STATX_FIELD_U32(o->nlink, e->spoofed_nlink, STATX_F_NLINK);
	if (e->flags & KSTAT_SPOOF_SIZE)
		STATX_FIELD_U64(o->size, e->spoofed_size, STATX_F_SIZE);
	if (e->flags & KSTAT_SPOOF_BLOCKS)
		STATX_FIELD_U64(o->blocks, e->spoofed_blocks, STATX_F_BLOCKS);

	if (e->flags & KSTAT_SPOOF_BLKSIZE) {
		s32 v = (s32)e->spoofed_blksize;

		if (copy_to_user((void __user *)(sbuf + o->blksize), &v, sizeof(v))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}
	if (e->flags & KSTAT_SPOOF_ATIME_TV_SEC)
		STATX_FIELD_S32(o->atime_sec, e->spoofed_atime_tv_sec, STATX_ATIME);
	if (e->flags & KSTAT_SPOOF_ATIME_TV_NSEC)
		STATX_FIELD_U32(o->atime_nsec, e->spoofed_atime_tv_nsec, STATX_ATIME);
	if (e->flags & KSTAT_SPOOF_MTIME_TV_SEC)
		STATX_FIELD_S32(o->mtime_sec, e->spoofed_mtime_tv_sec, STATX_MTIME);
	if (e->flags & KSTAT_SPOOF_MTIME_TV_NSEC)
		STATX_FIELD_U32(o->mtime_nsec, e->spoofed_mtime_tv_nsec, STATX_MTIME);
	if (e->flags & KSTAT_SPOOF_CTIME_TV_SEC)
		STATX_FIELD_S32(o->ctime_sec, e->spoofed_ctime_tv_sec, STATX_CTIME);
	if (e->flags & KSTAT_SPOOF_CTIME_TV_NSEC)
		STATX_FIELD_U32(o->ctime_nsec, e->spoofed_ctime_tv_nsec, STATX_CTIME);

	if ((e->flags & KSTAT_SPOOF_DEV) && (eff & STATX_BASIC_STATS)) {
		unsigned int enc = (unsigned int)e->spoofed_dev;
		/* stx_dev_major/minor are u32 each: write all four bytes of each half.  Writing a u16
		 * per half would leave the upper bytes of the real device number behind for any minor
		 * that does not fit 16 bits. */
		unsigned int maj = (enc & 0xfff00u) >> 8;
		unsigned int min = MINOR(new_decode_dev(enc));

		if (copy_to_user((void __user *)(sbuf + o->dev), &maj, sizeof(maj))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		if (copy_to_user((void __user *)(sbuf + o->dev_minor), &min, sizeof(min))) {
			atomic_inc(&st->cnt->uaccess);
			return false;
		}
		rewrote = true;
	}

	if (!rewrote) {
		atomic_inc(&st->cnt->match_noflag);
		return false;
	}

	atomic_inc(&st->cnt->rewrite);
	return true;
}

/* The post-check, called at the TAIL of the table-entry wrapper (see kstat_hooks[] below) instead
 * of from a entry tail.  Two features ride it, exactly as before: the stat family below,
 * and sus_path's directory-listing rewrite.  The listing side still needs its own gate - see the
 * note above sus_path_dirent_layout_id(): a syscall number alone does not identify a call (arm64's
 * native 141 is getpriority where AArch32 numbers getdents 141), so the number is only ever read
 * together with the caller's ABI - and here the ABI is not a guess, it is which table the wrapper
 * was installed in.
 *
 * WHAT CHANGED, AND WHY.  As a tracepoint this ran for EVERY syscall exit, so is_compat_task(),
 * syscall_get_nr() and the two classifiers below were paid on all of them - measured at ~28 ns per
 * syscall when the stat family moved onto it (getpid 113 -> 141 ns), and re-measured at the time of
 * this change as 126 ns without the tracepoint vs 151 ns with it (three runs each, +25..31 ns), on
 * the same device the module ships to.  As a wrapper tail it runs only for the thirteen numbers in
 * kstat_hooks[], and both the "which ABI" and the "which syscall" questions are answered by which
 * table entry was called rather than by a test that every syscall has to pay for.  Nothing is lost:
 * the arguments arrive from the wrapper, which copies them out of regs BEFORE calling the original
 * (a syscall's return value lands in susfs_sc_arg(regs, 0), so reading them afterwards would hand the
 * spoofers the return value in place of the first argument).
 *
 * The old KNOWN BOUNDARY (ptrace) went away with the tracepoint: arch/arm64/kernel/syscall.c
 * writes regs->syscallno = scno before syscall_trace_enter(), so a tracer that rewrote a getdents64
 * into another syscall was still classified as getdents64 here.  A tracer cannot rewrite which
 * table entry the kernel calls, so this dispatch is on what was really called.
 *
 * `out` is the wrapper's answer and starts at the original's return value: the stat family rewrites
 * the caller's buffer and leaves the return value alone, while the listing filter shortens the count
 * and must hand that back - the tracepoint did it with syscall_set_return_value(), which a wrapper
 * has no regs to do it with. */
static __nocfi void susfs_kstat_post(long nr, bool compat, const unsigned long *args,
				     long ret, long *out)
{
	struct kstat_call_counters *c;
	struct kstat_call_state st;
	int lay = sus_path_dirent_layout_id(nr, compat);

	if (lay >= 0) {
		long rc;

		/* Nothing to rewrite when the syscall produced no bytes (an error, or the end of
		 * the directory), and the arguments must not be read before the gate said this
		 * really is a listing call: the filter is told "no buffer, nothing produced" and
		 * returns without touching the caller's memory.
		 * The number alone does not identify the call (native 141 is getpriority, not
		 * the AArch32 getdents): sus_path_dirent_layout_id() gates it on the ABI, so
		 * anything that is not really a listing leaves here before the arguments are
		 * read.
		 * The armed flag is not tested here any more: sus_path_dirent_filter() checks
		 * it itself, together with ret <= 0 and a NULL buffer, so a second gate at this
		 * call site only duplicated it while looking like coverage. */
		if (ret <= 0) {
			sus_path_dirent_filter(lay, 0, ret);
			return;
		}
		/* The arguments come from the wrapper now - see the note above. */
		rc = sus_path_dirent_filter(lay,
					    compat ? (unsigned long)compat_ptr((u32)args[1])
						   : args[1],
					    ret);
		if (rc != ret)
			*out = rc;
		return;
	}

	/* ---- stat family: number -> counters -> argument positions ----
	 * Gated on the ABI for the same reason as the listing numbers above: on arm64 the native
	 * table's 195/196/197 are shmctl/shmat/shmdt, and shmat's second argument is a mapped
	 * address - read as a compat struct stat64 buffer it would hand back whatever the caller
	 * happens to have there, and on a rule hit rewrite it. */
	if (compat) {
		switch (nr) {
		case COMPAT_FSTATAT64_NR:
			c = &cnt_fstatat64;
			break;
		case COMPAT_STAT64_NR:
			c = &cnt_stat64;
			break;
		case COMPAT_LSTAT64_NR:
			c = &cnt_lstat64;
			break;
		case COMPAT_FSTAT64_NR:
			c = &cnt_fstat64;
			break;
		case COMPAT_STAT_NR:
		case COMPAT_LSTAT_NR:
		case COMPAT_FSTAT_NR:
			/* struct compat_stat, NOT struct stat64 - see the offsets and the widths
			 * above; the two differ in every field after st_ino. */
			c = &cnt_compat_stat;
			break;
		case COMPAT_STATX_NR:
			/* A counter of its own: "64-bit statx is spoofed" says nothing about the
			 * 32-bit one, and mixing them is how a 32-bit-only gap stayed invisible. */
			c = &cnt_compat_statx;
			break;
		default:
			c = NULL;
			break;
		}
	} else {
		switch (nr) {
		case __NR_newfstatat:
			c = &cnt_nfstatat;
			break;
		case __NR_fstat:
			/* fstat(fd, statbuf): the buffer is args[1], not args[2]. */
			c = &cnt_nfstat;
			break;
		case __NR_statx:
			c = &cnt_statx;
			break;
		default:
			c = NULL;
			break;
		}
	}

	if (!c) {
		/* A listing number cannot reach this point any more: the gate at the top of this
		 * function answers both getdents ABIs before the stat switch, so only the stat
		 * range is worth noting here. */
		if (nr >= KSTAT_NR_SCAN_LO && nr <= KSTAT_NR_SCAN_HI)
			kstat_note_unlisted(nr);
		return;
	}

	/* Whitelisted: from here on only this number's counters move. */
	if (ret != 0) {
		atomic_inc(&c->ret_err);
		return;
	}
	atomic_inc(&c->calls);

	st.cnt = c;
	st.compat = compat;	/* the ABI that selected the number above */

	/* The statbuf argument is NOT the same one for every syscall, so the NULL check has to live
	 * inside each branch: fstat64(fd, statbuf) keeps it in args[1], and checking args[2] first
	 * made every 32-bit fstat64() return early - the rule applied to fstatat64, not to fstat64. */
	/* Argument positions differ per number, so this stays a number switch - but only numbers
	 * the gate above selected for THIS ABI can reach it, and the two ABIs' number sets
	 * (79/80/291 native; 106/107/108, 195/196/197, 327, 397 compat) do not overlap, so it needs
	 * no gate of its own. */
	switch (nr) {
	case __NR_newfstatat:
		/* The arguments come from the wrapper now - see the note above. */
		if (!args[2]) {
			atomic_inc(&c->no_buf);
			return;
		}
		susfs_kstat_spoof_statbuf(&st, args[2]);
		return;
	case __NR_fstat:
		/* The arguments come from the wrapper now - see the note above. */
		if (!args[1]) {
			atomic_inc(&c->no_buf);
			return;
		}
		susfs_kstat_spoof_statbuf(&st, args[1]);
		return;
	case __NR_statx:
	case COMPAT_STATX_NR:	/* 397 IS the native statx body: same buffer, same mask, 32-bit ptr */

		/* The arguments come from the wrapper now - see the note above. */
		if (!args[4]) {
			atomic_inc(&c->no_buf);
			return;
		}
		susfs_kstat_spoof_statx(&st,
			st.compat ? (unsigned long)compat_ptr((u32)args[4]) : args[4],
			(u32)args[3]);
		return;
	case COMPAT_FSTATAT64_NR:

		/* The arguments come from the wrapper now - see the note above. */
		if (!args[2]) {
			atomic_inc(&c->no_buf);
			return;
		}
		susfs_kstat_spoof_compat_statbuf(&st, (unsigned long)compat_ptr((u32)args[2]));
		return;
	case COMPAT_STAT64_NR:
	case COMPAT_LSTAT64_NR:
	case COMPAT_FSTAT64_NR:

		/* The arguments come from the wrapper now - see the note above. */
		if (!args[1]) {
			atomic_inc(&c->no_buf);
			return;
		}
		susfs_kstat_spoof_compat_statbuf(&st, (unsigned long)compat_ptr((u32)args[1]));
		return;
	case COMPAT_STAT_NR:
	case COMPAT_LSTAT_NR:
	case COMPAT_FSTAT_NR:

		/* stat(path, statbuf), lstat(path, statbuf), fstat(fd, statbuf): the buffer is
		 * args[1] in all three (fs/stat.c: COMPAT_SYSCALL_DEFINE2(newstat, filename,
		 * statbuf) and DEFINE2(newfstat, fd, statbuf)).  struct compat_stat, so the
		 * write-back is susfs_kstat_spoof_compat_stat(), not the one above. */
		/* The arguments come from the wrapper now - see the note above. */
		if (!args[1]) {
			atomic_inc(&c->no_buf);
			return;
		}
		susfs_kstat_spoof_compat_stat(&st, (unsigned long)compat_ptr((u32)args[1]));
		return;
	}
}

/* ==== the sys_call_table entries that drive the post-check above ==== */
/*
 * This is what replaced the stat layer's table entries.  Thirteen entries, one per (number, ABI)
 * pair, because the two questions that tracepoint had to answer on EVERY syscall - "which ABI is
 * the caller" and "which number is this" - are both answered here by which entry was called: a
 * 32-bit task never reaches sys_call_table, and the compat table does not share its numbering with
 * the native one (AArch32 141 is getdents where arm64's 141 is getpriority; its 195/196/197 are
 * stat64/lstat64/fstat64 where the native ones are shmctl/shmat/shmdt).
 *
 * WHY IT MOVED.  As a tracepoint the callback ran on every syscall exit and paid
 * is_compat_task(), syscall_get_nr() and two classifiers for all of them.  Measured on the device
 * before this change, on the getpid line of susfs_bench (no entry of ours is involved there, so it
 * is the cost of whatever fires on every syscall):
 *
 *     kstat layer forced off (fail_layer=6)   126 / 126 / 126 ns   <- no tracepoint
 *     everything armed                        151 / 150 / 151 ns   <- tracepoint registered
 *
 * i.e. +25..31 ns per syscall, on every syscall, whether or not it is one this module cares about.
 * Now only these thirteen numbers pay anything, and both classifiers went away with the tracepoint.
 *
 * Arguments are copied out of regs BEFORE the original runs: a syscall's return value is written
 * into susfs_sc_arg(regs, 0) (arch/arm64/kernel/syscall.c, and the compat entry point does the same), so
 * reading them afterwards would hand the spoofers the return value in place of the first argument.
 *
 * __nocfi is mandatory on the wrapper, for the reason susfs_opath.c states at length: it is reached
 * by an indirect call out of the kernel's table, and it calls the original the same way.  The NULL
 * test on the slot is not about publish order - susfs_fp_install() publishes the original before it
 * writes the table - it is what turns a future mistake there into one failed syscall instead of a
 * jump to address 0.
 *
 * nr < 0 means "this number exists in the compat table only" (struct susfs_fp_hook spells that
 * out); every AArch32 entry below is one of those, because its native namesake is a different
 * syscall.  The .sym fields are for the diagnostic dump only - susfs_fp_dump_entry() resolves them
 * by name - so one that does not match what this kernel calls the entry shows up as "no symbol"
 * there and changes nothing about the hook itself.
 */
static susfs_syscall_fn_t kstat_orig_nfstatat;
static susfs_syscall_fn_t kstat_orig_fstat;
static susfs_syscall_fn_t kstat_orig_statx;
static susfs_syscall_fn_t kstat_orig_getdents64;
#ifdef __aarch64__
static susfs_syscall_fn_t kstat_orig_compat_stat64;
static susfs_syscall_fn_t kstat_orig_compat_lstat64;
static susfs_syscall_fn_t kstat_orig_compat_fstat64;
static susfs_syscall_fn_t kstat_orig_compat_fstatat64;
static susfs_syscall_fn_t kstat_orig_compat_stat;
static susfs_syscall_fn_t kstat_orig_compat_lstat;
static susfs_syscall_fn_t kstat_orig_compat_fstat;
static susfs_syscall_fn_t kstat_orig_compat_statx;
static susfs_syscall_fn_t kstat_orig_compat_getdents64;
static susfs_syscall_fn_t kstat_orig_compat_getdents;
#endif

#define SUSFS_KSTAT_WRAPPER(fn, slot, nrval, combat)			\
	static __nocfi long fn(const struct pt_regs *regs)		\
	{								\
		susfs_syscall_fn_t orig = READ_ONCE(slot);		\
		unsigned long a[6];					\
		long ret, out;						\
									\
		if (!orig)						\
			return -ENOSYS;					\
		/* susfs_syscall_fn_t takes a const regs (the table's own type) while	\
		 * syscall_get_arguments() asks for a non-const one it only reads - the	\
		 * cast is what that difference costs, and nothing here writes regs. */	\
		syscall_get_arguments(current, (struct pt_regs *)regs, a);	\
		ret = orig(regs);					\
		out = ret;						\
		susfs_kstat_post(nrval, combat, a, ret, &out);		\
		return out;						\
	}

SUSFS_KSTAT_WRAPPER(kstat_wrap_nfstatat, kstat_orig_nfstatat, __NR_newfstatat, false)
SUSFS_KSTAT_WRAPPER(kstat_wrap_fstat, kstat_orig_fstat, __NR_fstat, false)
SUSFS_KSTAT_WRAPPER(kstat_wrap_statx, kstat_orig_statx, __NR_statx, false)
/* getdents64 does NOT use the shared macro: sus_dirent.c can answer it without the original
 * ever writing to the caller's buffer, which is what removes the two tells the post-hoc
 * filter leaves (the d_ino = 0 placeholder, and the scrub that can be faulted).  When that
 * layer declines - unresolved symbol, prefilter=0 - this falls back to exactly what the
 * macro does, so hiding continues either way. */
static __nocfi long kstat_wrap_getdents64(const struct pt_regs *regs)
{
	susfs_syscall_fn_t orig = READ_ONCE(kstat_orig_getdents64);
	unsigned long a[6];
	long ret, out;

	if (!orig)
		return -ENOSYS;
	if (susfs_dirent_prefilter(regs, &out, false))
		return out;
	syscall_get_arguments(current, (struct pt_regs *)regs, a);
	ret = orig(regs);
	out = ret;
	susfs_kstat_post(__NR_getdents64, false, a, ret, &out);
	return out;
}

#ifdef __aarch64__
/* The compat half is arm64 only for now: the x86_64 port drops the 32-bit
 * ABI for v1, AArch32 numbers and layouts do not describe ia32. */
SUSFS_KSTAT_WRAPPER(kstat_wrap_compat_stat64, kstat_orig_compat_stat64, COMPAT_STAT64_NR, true)
SUSFS_KSTAT_WRAPPER(kstat_wrap_compat_lstat64, kstat_orig_compat_lstat64, COMPAT_LSTAT64_NR, true)
SUSFS_KSTAT_WRAPPER(kstat_wrap_compat_fstat64, kstat_orig_compat_fstat64, COMPAT_FSTAT64_NR, true)
SUSFS_KSTAT_WRAPPER(kstat_wrap_compat_fstatat64, kstat_orig_compat_fstatat64, COMPAT_FSTATAT64_NR, true)
SUSFS_KSTAT_WRAPPER(kstat_wrap_compat_stat, kstat_orig_compat_stat, COMPAT_STAT_NR, true)
SUSFS_KSTAT_WRAPPER(kstat_wrap_compat_lstat, kstat_orig_compat_lstat, COMPAT_LSTAT_NR, true)
SUSFS_KSTAT_WRAPPER(kstat_wrap_compat_fstat, kstat_orig_compat_fstat, COMPAT_FSTAT_NR, true)
SUSFS_KSTAT_WRAPPER(kstat_wrap_compat_statx, kstat_orig_compat_statx, COMPAT_STATX_NR, true)
static __nocfi long kstat_wrap_compat_getdents64(const struct pt_regs *regs)
{
	susfs_syscall_fn_t orig = READ_ONCE(kstat_orig_compat_getdents64);
	unsigned long a[6];
	long ret, out;

	if (!orig)
		return -ENOSYS;
	if (susfs_dirent_prefilter(regs, &out, false))
		return out;
	syscall_get_arguments(current, (struct pt_regs *)regs, a);
	ret = orig(regs);
	out = ret;
	susfs_kstat_post(SUS_NR_GETDENTS64_COMPAT, true, a, ret, &out);
	return out;
}

static __nocfi long kstat_wrap_compat_getdents(const struct pt_regs *regs)
{
	susfs_syscall_fn_t orig = READ_ONCE(kstat_orig_compat_getdents);
	unsigned long a[6];
	long ret, out;

	if (!orig)
		return -ENOSYS;
	if (susfs_dirent_prefilter(regs, &out, true))
		return out;
	syscall_get_arguments(current, (struct pt_regs *)regs, a);
	ret = orig(regs);
	out = ret;
	susfs_kstat_post(SUS_NR_GETDENTS_COMPAT, true, a, ret, &out);
	return out;
}
#endif

static struct susfs_fp_hook kstat_hooks[] = {
	{ .nr = __NR_newfstatat, .nr_compat = 0, .name = "kstat/newfstatat",
	  .sym = SUSFS_SC_WRAPPER_NAME(newfstatat),
	  .wrapper = kstat_wrap_nfstatat, .orig_slot = &kstat_orig_nfstatat },
	{ .nr = __NR_fstat, .nr_compat = 0, .name = "kstat/fstat",
	  .sym = SUSFS_SC_WRAPPER_NAME(fstat),
	  .wrapper = kstat_wrap_fstat, .orig_slot = &kstat_orig_fstat },
	{ .nr = __NR_statx, .nr_compat = 0, .name = "kstat/statx",
	  .sym = SUSFS_SC_WRAPPER_NAME(statx),
	  .wrapper = kstat_wrap_statx, .orig_slot = &kstat_orig_statx },
	{ .nr = __NR_getdents64, .nr_compat = 0, .name = "kstat/getdents64",
	  .sym = SUSFS_SC_WRAPPER_NAME(getdents64),
	  .wrapper = kstat_wrap_getdents64, .orig_slot = &kstat_orig_getdents64 },

#ifdef __aarch64__
	/* The AArch32 half.  nr stays -1 for every one of them. */
	{ .nr = -1, .nr_compat = COMPAT_STAT64_NR, .name = "kstat/compat stat64",
	  .sym = "__arm64_compat_sys_stat64",
	  .wrapper = kstat_wrap_compat_stat64, .orig_slot_compat = &kstat_orig_compat_stat64 },
	{ .nr = -1, .nr_compat = COMPAT_LSTAT64_NR, .name = "kstat/compat lstat64",
	  .sym = "__arm64_compat_sys_lstat64",
	  .wrapper = kstat_wrap_compat_lstat64, .orig_slot_compat = &kstat_orig_compat_lstat64 },
	{ .nr = -1, .nr_compat = COMPAT_FSTAT64_NR, .name = "kstat/compat fstat64",
	  .sym = "__arm64_compat_sys_fstat64",
	  .wrapper = kstat_wrap_compat_fstat64, .orig_slot_compat = &kstat_orig_compat_fstat64 },
	{ .nr = -1, .nr_compat = COMPAT_FSTATAT64_NR, .name = "kstat/compat fstatat64",
	  .sym = "__arm64_compat_sys_fstatat64",
	  .wrapper = kstat_wrap_compat_fstatat64, .orig_slot_compat = &kstat_orig_compat_fstatat64 },
	{ .nr = -1, .nr_compat = COMPAT_STAT_NR, .name = "kstat/compat stat",
	  .sym = "__arm64_compat_sys_newstat",
	  .wrapper = kstat_wrap_compat_stat, .orig_slot_compat = &kstat_orig_compat_stat },
	{ .nr = -1, .nr_compat = COMPAT_LSTAT_NR, .name = "kstat/compat lstat",
	  .sym = "__arm64_compat_sys_newlstat",
	  .wrapper = kstat_wrap_compat_lstat, .orig_slot_compat = &kstat_orig_compat_lstat },
	{ .nr = -1, .nr_compat = COMPAT_FSTAT_NR, .name = "kstat/compat fstat",
	  .sym = "__arm64_compat_sys_newfstat",
	  .wrapper = kstat_wrap_compat_fstat, .orig_slot_compat = &kstat_orig_compat_fstat },
	{ .nr = -1, .nr_compat = COMPAT_STATX_NR, .name = "kstat/compat statx",
	  .sym = "__arm64_compat_sys_statx",
	  .wrapper = kstat_wrap_compat_statx, .orig_slot_compat = &kstat_orig_compat_statx },
	{ .nr = -1, .nr_compat = SUS_NR_GETDENTS64_COMPAT, .name = "kstat/compat getdents64",
	  .sym = "__arm64_compat_sys_getdents64",
	  .wrapper = kstat_wrap_compat_getdents64, .orig_slot_compat = &kstat_orig_compat_getdents64 },
	{ .nr = -1, .nr_compat = SUS_NR_GETDENTS_COMPAT, .name = "kstat/compat getdents",
	  .sym = "__arm64_compat_sys_getdents",
	  .wrapper = kstat_wrap_compat_getdents, .orig_slot_compat = &kstat_orig_compat_getdents },
#endif
};

/* ---- path resolution + rule management (original SUSFS semantics) ---- */

/* resolve <path> to its CURRENT (ino, encoded dev).  Sleeps - never call it with kstat_table_lock held. */
static int susfs_kstat_resolve(const char *path, unsigned long *ino, dev_t *dev)
{
	struct path p;
	struct inode *inode;
	int err;

	err = kern_path(path, 0, &p);
	if (err)
		return err;
	inode = d_backing_inode(p.dentry);
	if (!inode) {
		path_put(&p);
		return -ENOENT;
	}
	*ino = inode->i_ino;
	*dev = new_encode_dev(inode->i_sb->s_dev);
	path_put(&p);
	return 0;
}

/* resolve <path> and fill the spoofed_* fields with its CURRENT stat (the generic_fillattr
 * mapping).  Fills a DETACHED entry only: callers build here, then commit through one of the
 * kstat_table_* helpers, so nothing half-built is ever visible to a reader. */
static int susfs_kstat_fill_from_path(struct sus_kstat_entry *e, const char *path)
{
	struct path p;
	struct inode *inode;
	int err;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	/* 6.6 renamed the inode field to __i_ctime and marked it private ("use inode_*_ctime
	 * accessors!"), so reading it stopped compiling: "no member named 'i_ctime' in 'struct inode'".
	 * inode_get_ctime() returns the very same struct timespec64 by value, and this gate is
	 * release-aligned as well as upstream-aligned: v6.6 added the accessor upstream and is also the
	 * GKI tree boundary.  i_atime/i_mtime went the same way one move later: v6.11 split them into
	 * time64_t i_atime_sec/i_mtime_sec plus u32 i_atime_nsec/i_mtime_nsec, read through
	 * inode_get_atime()/inode_get_mtime() - which reassemble the very same struct timespec64 by
	 * value, and are what the second #if below uses; reading the old fields directly no longer
	 * compiles there.  The gate is on 6.12 rather than on 6.11 because 6.11 is not a GKI kernel:
	 * the header trees this module is built against are 6.6 (old fields) and 6.12/6.18 (accessors). */
	struct timespec64 ctime;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	struct timespec64 atime, mtime;
#endif

	err = kern_path(path, 0, &p);
	if (err)
		return err;
	inode = d_backing_inode(p.dentry);
	if (!inode) {
		path_put(&p);
		return -ENOENT;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	ctime = inode_get_ctime(inode);
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	atime = inode_get_atime(inode);
	mtime = inode_get_mtime(inode);
#endif

	e->target_ino = inode->i_ino;
	e->target_dev = new_encode_dev(inode->i_sb->s_dev);
	e->spoofed_ino = inode->i_ino;
	e->spoofed_dev = new_encode_dev(inode->i_sb->s_dev);
	e->spoofed_nlink = inode->i_nlink;
	e->spoofed_size = inode->i_size;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	e->spoofed_atime_tv_sec = atime.tv_sec;
	e->spoofed_atime_tv_nsec = atime.tv_nsec;
	e->spoofed_mtime_tv_sec = mtime.tv_sec;
	e->spoofed_mtime_tv_nsec = mtime.tv_nsec;
#else
	e->spoofed_atime_tv_sec = inode->i_atime.tv_sec;
	e->spoofed_atime_tv_nsec = inode->i_atime.tv_nsec;
	e->spoofed_mtime_tv_sec = inode->i_mtime.tv_sec;
	e->spoofed_mtime_tv_nsec = inode->i_mtime.tv_nsec;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	e->spoofed_ctime_tv_sec = ctime.tv_sec;
	e->spoofed_ctime_tv_nsec = ctime.tv_nsec;
#else
	e->spoofed_ctime_tv_sec = inode->i_ctime.tv_sec;
	e->spoofed_ctime_tv_nsec = inode->i_ctime.tv_nsec;
#endif
	e->spoofed_blocks = inode->i_blocks;
	e->spoofed_blksize = 1 << inode->i_blkbits;

	path_put(&p);
	return 0;
}

/* re-resolve only target_ino/target_dev; spoofed values stay untouched.  Resolve first (sleeping),
 * then re-target under the lock: a reader sees the old pair or the new pair, never ino-of-B with
 * dev-of-A. */
static int susfs_kstat_update(const char *path, bool full_clone)
{
	struct sus_kstat_entry *e;
	unsigned long ino;
	dev_t dev;
	int err;

	e = susfs_kstat_find_by_path(path);
	if (!e)
		return -ENOENT;
	err = susfs_kstat_resolve(path, &ino, &dev);
	if (err)
		return err;
	kstat_table_retarget((int)(e - kstat_entries), ino, dev,
			     full_clone ? KSTAT_AUTO_SPOOF_FULL_CLONE
					: KSTAT_AUTO_SPOOF);
	return 0;
}

/* add/update a rule from its pathname; kstat_lock held */
static int susfs_kstat_add(const char *path)
{
	struct sus_kstat_entry tmp;
	struct sus_kstat_entry *e;
	int err, idx;

	if (strlen(path) >= KSTAT_PATH_MAX)
		return -ENAMETOOLONG;

	e = susfs_kstat_find_by_path(path);

	memset(&tmp, 0, sizeof(tmp));
	err = susfs_kstat_fill_from_path(&tmp, path);
	if (err)
		return err;
	strscpy(tmp.target_pathname, path, KSTAT_PATH_MAX);
	tmp.flags = KSTAT_AUTO_SPOOF;

	if (e) {
		kstat_table_put((int)(e - kstat_entries), &tmp);
		return 0;
	}
	idx = kstat_table_append(&tmp);
	return idx < 0 ? -ENOSPC : 0;
}

static void susfs_kstat_del(const char *path)
{
	struct sus_kstat_entry *e = susfs_kstat_find_by_path(path);

	if (!e)
		return;
	kstat_table_del((int)(e - kstat_entries));
}

/* parse "default" -> *is_default=true, else parse signed 64-bit int */
static int parse_override(const char *tok, bool *is_default, long long *val)
{
	if (!strcmp(tok, "default")) {
		*is_default = true;
		return 0;
	}
	*is_default = false;
	return kstrtoll(tok, 10, val);
}

/* add_sus_kstat_statically: 12 fields follow the path, each a number or "default". */
static int susfs_kstat_add_statically(char **argv, int argc)
{
	struct sus_kstat_entry tmp;
	struct sus_kstat_entry *e;
	long long val;
	bool dflt;
	int err, i, idx;
	/* field index -> flag and setter, ordered as the CLI:
	 * ino dev nlink size atime atime_nsec mtime mtime_nsec
	 * ctime ctime_nsec blocks blksize */
	static const unsigned int f_flags[12] = {
		KSTAT_SPOOF_INO, KSTAT_SPOOF_DEV, KSTAT_SPOOF_NLINK,
		KSTAT_SPOOF_SIZE, KSTAT_SPOOF_ATIME_TV_SEC,
		KSTAT_SPOOF_ATIME_TV_NSEC, KSTAT_SPOOF_MTIME_TV_SEC,
		KSTAT_SPOOF_MTIME_TV_NSEC, KSTAT_SPOOF_CTIME_TV_SEC,
		KSTAT_SPOOF_CTIME_TV_NSEC, KSTAT_SPOOF_BLOCKS,
		KSTAT_SPOOF_BLKSIZE,
	};
	const char *path = argv[1];

	/* argv[2 + i] below reaches argv[13], so this needs argv[14]: assert it here rather than only
	 * at the call site, which checks `argc == 14` against a 16-slot argv - a future caller that
	 * forgot would read past the array. */
	if (argc < 2 + (int)ARRAY_SIZE(f_flags))
		return -EINVAL;

	if (strlen(path) >= KSTAT_PATH_MAX)
		return -ENAMETOOLONG;

	e = susfs_kstat_find_by_path(path);

	memset(&tmp, 0, sizeof(tmp));
	/* start from the CURRENT stat; non-default fields override it */
	err = susfs_kstat_fill_from_path(&tmp, path);
	if (err)
		return err;
	strscpy(tmp.target_pathname, path, KSTAT_PATH_MAX);
	tmp.flags = 0;

	for (i = 0; i < 12; i++) {
		err = parse_override(argv[2 + i], &dflt, &val);
		if (err)
			return err;
		if (dflt)
			continue;
		tmp.flags |= f_flags[i];
		switch (i) {
		case 0: tmp.spoofed_ino = (unsigned long)val; break;
		case 1: tmp.spoofed_dev = (unsigned long)val; break;
		case 2: tmp.spoofed_nlink = (unsigned int)val; break;
		case 3: tmp.spoofed_size = val; break;
		case 4: tmp.spoofed_atime_tv_sec = (long)val; break;
		case 5: tmp.spoofed_atime_tv_nsec = (unsigned long)val; break;
		case 6: tmp.spoofed_mtime_tv_sec = (long)val; break;
		case 7: tmp.spoofed_mtime_tv_nsec = (unsigned long)val; break;
		case 8: tmp.spoofed_ctime_tv_sec = (long)val; break;
		case 9: tmp.spoofed_ctime_tv_nsec = (unsigned long)val; break;
		case 10: tmp.spoofed_blocks = val; break;
		case 11: tmp.spoofed_blksize = (long)val; break;
		}
	}

	if (e) {
		kstat_table_put((int)(e - kstat_entries), &tmp);
		return 0;
	}
	idx = kstat_table_append(&tmp);
	return idx < 0 ? -ENOSPC : 0;
}

/* statically-add from the supercall ABI struct (is_statically=1): copy the caller's 12 spoofed
 * fields + flags verbatim; resolve target ino/dev here. */
static int susfs_kstat_add_statically_abi(struct st_susfs_sus_kstat *info)
{
	struct sus_kstat_entry tmp;
	struct sus_kstat_entry *e;
	int err, idx;

	e = susfs_kstat_find_by_path(info->target_pathname);

	memset(&tmp, 0, sizeof(tmp));
	err = susfs_kstat_fill_from_path(&tmp, info->target_pathname);
	if (err)
		return err;
	strscpy(tmp.target_pathname, info->target_pathname, KSTAT_PATH_MAX);

	tmp.spoofed_ino = info->spoofed_ino;
	tmp.spoofed_dev = info->spoofed_dev;
	tmp.spoofed_nlink = info->spoofed_nlink;
	tmp.spoofed_size = info->spoofed_size;
	tmp.spoofed_atime_tv_sec = info->spoofed_atime_tv_sec;
	tmp.spoofed_atime_tv_nsec = info->spoofed_atime_tv_nsec;
	tmp.spoofed_mtime_tv_sec = info->spoofed_mtime_tv_sec;
	tmp.spoofed_mtime_tv_nsec = info->spoofed_mtime_tv_nsec;
	tmp.spoofed_ctime_tv_sec = info->spoofed_ctime_tv_sec;
	tmp.spoofed_ctime_tv_nsec = info->spoofed_ctime_tv_nsec;
	tmp.spoofed_blocks = info->spoofed_blocks;
	tmp.spoofed_blksize = info->spoofed_blksize;
	tmp.flags = info->flags;

	if (e) {
		kstat_table_put((int)(e - kstat_entries), &tmp);
		return 0;
	}
	idx = kstat_table_append(&tmp);
	return idx < 0 ? -ENOSPC : 0;
}

/* supercall: CMD_SUSFS_ADD_SUS_KSTAT / UPDATE / STATICALLY */
void susfs_kstat_supercall(unsigned int cmd, void __user **arg)
{
	struct st_susfs_sus_kstat info = {0};
	int err = -EINVAL;

	if (copy_from_user(&info, (void __user *)*arg, sizeof(info))) {
		info.err = -EFAULT;
		goto out;
	}

	/* All three commands key on target_pathname, and a caller may fill all 256 bytes of that field -
	 * reject an unterminated one before any strlen() or kern_path() can walk off our stack copy. */
	if (!susfs_abi_path_ok(info.target_pathname, sizeof(info.target_pathname))) {
		info.err = -ENAMETOOLONG;
		goto out;
	}

	mutex_lock(&kstat_lock);
	switch (cmd) {
	case CMD_SUSFS_ADD_SUS_KSTAT:
		err = susfs_kstat_add(info.target_pathname);
		break;
	case CMD_SUSFS_ADD_SUS_KSTAT_STATICALLY:
		err = susfs_kstat_add_statically_abi(&info);
		break;
	case CMD_SUSFS_UPDATE_SUS_KSTAT:
		err = susfs_kstat_update(info.target_pathname, false);
		break;
	}
	mutex_unlock(&kstat_lock);
	info.err = err;
	/* Armed outside the lock and only after a rule actually landed: a hook that can never fire is worse than no hook. */
	if (!err)
		kstat_maps_arm();
out:
	/* Upstream (fs/susfs.c susfs_add_sus_kstat) writes back ONLY the err field for this
	 * input-type command, never the whole struct: copying the full struct back would overrun a
	 * caller whose own struct is smaller/differently laid out (the prebuilt ksu_susfs tool) and
	 * corrupt its stack - match upstream exactly. */
	if (copy_to_user(&((struct st_susfs_sus_kstat __user *)*arg)->err,
			 &info.err, sizeof(info.err)))
		pr_warn("kstat supercall copy_to_user failed\n");
}

/* ---- /proc/susfs_kstat: runtime rule management ---- */
static int kstat_proc_show(struct seq_file *m, void *v);
static int kstat_proc_open(struct inode *inode, struct file *file);
static ssize_t kstat_proc_write(struct file *file, const char __user *buf,
                                size_t len, loff_t *off);

static const struct proc_ops kstat_proc_ops = {
	.proc_open = kstat_proc_open,
	.proc_read = seq_read,
	.proc_write = kstat_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static struct proc_dir_entry *kstat_proc_entry;

/* How many of kstat_hooks[] are actually in place: this layer's armed state, and what its log line
 * reports.  The entry tail that used to be registered here is gone - the entries' comment
 * above says why and what it cost. */
static int kstat_hooks_armed;

int susfs_kstat_init(void)
{
	int rc;
	int i;

	/* The entries are armed UNCONDITIONALLY, and that is not cosmetic: the supercall interface
	 * (CMD_SUSFS_ADD_SUS_KSTAT) can install rules with no /proc node at all, so skipping
	 * registration when expose_proc=0 silently turned the whole feature into a no-op - rules
	 * accepted, never applied.  expose_proc decides whether the node exists, nothing else.
	 *
	 * One entry at a time, and never fatal for the others - the same shape susfs_opath.c uses: a
	 * number that cannot be installed leaves that one entry answering the kernel's own way while
	 * the rest still work, and the log above names it.  Nothing here is a tracepoint any more, so
	 * the reset rule in kernel/susfs_kprobe.h (about a probe's address field surviving a failed
	 * registration) has no bearing on it. */
	for (i = 0; i < (int)ARRAY_SIZE(kstat_hooks); i++) {
		struct susfs_fp_hook *h = &kstat_hooks[i];

		rc = susfs_fp_install(h);
		if (rc) {
			pr_warn("kstat: %s not installed (%d) - that entry answers the kernel's own way\n",
				h->name, rc);
			continue;
		}
		kstat_hooks_armed++;
	}
	if (!kstat_hooks_armed) {
		pr_warn("kstat: none of the %u entries could be installed - the stat family and the dirent rewrite stay off\n",
			(unsigned int)ARRAY_SIZE(kstat_hooks));
		return -ENOENT;
	}
	if (kstat_hooks_armed < (int)ARRAY_SIZE(kstat_hooks))
		pr_warn("kstat: %d of %u entries installed (the log above names the rest)\n",
			kstat_hooks_armed, (unsigned int)ARRAY_SIZE(kstat_hooks));

	/* 0777 is deliberate, not an oversight.  inode_permission() runs the DAC check BEFORE
	 * security_inode_permission(), so a node the app cannot open hands it EACCES - "this exists,
	 * you may not read it" - instead of the ENOENT sus_path is supposed to produce.  0777 lets DAC
	 * pass and leaves the answer to sus_path's LSM layer, which then becomes the ONLY thing
	 * between an app and a world-writable control node - so without that layer the node is not
	 * created at all, see susfs_control_node_allowed(). */
	if (susfs_control_node_allowed()) {
		kstat_proc_entry = proc_create("susfs_kstat", 0777, NULL,
					       &kstat_proc_ops);
		if (!kstat_proc_entry)
			pr_warn("proc_create(susfs_kstat) failed\n");
	} else {
		SUSFS_LOGI("susfs_kstat: /proc node not created (expose_proc=%d lsm=%d)\n",
			(int)susfs_expose_proc, (int)sus_path_lsm_active());
	}

	SUSFS_LOGI("kstat armed: %d rules (%d/%u table entries installed, proc=%d); stat rewrite + sus_path dirent rewrite ride those entries\n",
		nkstat, kstat_hooks_armed, (unsigned int)ARRAY_SIZE(kstat_hooks),
		kstat_proc_entry != NULL);
	return 0;
}

void susfs_kstat_exit(void)
{
	int i;

	kstat_maps_disarm();

	/* Every entry is restored first and the drain runs after all of them: restoring stops new
	 * callers, and susfs_fp_drain() is what covers a task that already picked one of our wrappers
	 * out of the table - the same order, and the same reason, as susfs_opath_exit().  Idempotent
	 * (guarded on `installed` and on the count), because this runs both for the rollback of a
	 * failed load and for the unload of a loaded module. */
	for (i = 0; i < (int)ARRAY_SIZE(kstat_hooks); i++) {
		struct susfs_fp_hook *h = &kstat_hooks[i];

		if (!h->installed)
			continue;
		susfs_fp_remove(h);
	}
	if (kstat_hooks_armed) {
		susfs_fp_drain();
		kstat_hooks_armed = 0;
	}
	if (kstat_proc_entry) {
		proc_remove(kstat_proc_entry);
		kstat_proc_entry = NULL;
	}
	kstat_table_clear();
}

static int kstat_proc_show(struct seq_file *m, void *v)
{
	int i;

	mutex_lock(&kstat_lock);
	if (nkstat == 0) {
		seq_puts(m, "(empty)\n");
	} else {
		for (i = 0; i < nkstat; i++) {
			struct sus_kstat_entry *e = &kstat_entries[i];

			seq_printf(m,
				"%s ino=%lu dev=%lu flags=0x%x"
				" [ino=%lu dev=%lu nlink=%u size=%lld"
				" atime=%ld.%lu mtime=%ld.%lu ctime=%ld.%lu"
				" blocks=%lld blksize=%ld]\n",
				e->target_pathname, e->target_ino,
				(unsigned long)e->target_dev, e->flags,
				e->spoofed_ino, e->spoofed_dev, e->spoofed_nlink,
				e->spoofed_size,
				e->spoofed_atime_tv_sec, e->spoofed_atime_tv_nsec,
				e->spoofed_mtime_tv_sec, e->spoofed_mtime_tv_nsec,
				e->spoofed_ctime_tv_sec, e->spoofed_ctime_tv_nsec,
				e->spoofed_blocks, e->spoofed_blksize);
		}
	}
	mutex_unlock(&kstat_lock);

	/* "armed" is not "fired": the maps hook has to be readable the same way the other feature
	 * hooks are, or a rewrite that never happens looks identical to one that works. */
	seq_printf(m, "maps: armed=%d hits=%d rewrites=%d\n",
		   kstat_maps_registered, atomic_read(&n_kstat_map_hits),
		   atomic_read(&n_kstat_map_rewrites));

	{
		unsigned int i;

		for (i = 0; i < ARRAY_SIZE(kstat_nr_table); i++) {
			struct kstat_call_counters *c = kstat_nr_table[i].cnt;

			seq_printf(m,
				   "stat nr %-16s (%ld): ok=%d rw=%d | err=%d nobuf=%d empty=%d gate=%d lookup=%d noflag=%d uacc=%d narrow=%d\n",
				   kstat_nr_table[i].name, kstat_nr_table[i].nr,
				   atomic_read(&c->calls), atomic_read(&c->rewrite),
				   atomic_read(&c->ret_err), atomic_read(&c->no_buf),
				   atomic_read(&c->miss_empty), atomic_read(&c->miss_gate),
				   atomic_read(&c->miss_lookup), atomic_read(&c->match_noflag),
				   atomic_read(&c->uaccess), atomic_read(&c->narrow));
		}
		seq_printf(m, "stat unlisted-nr hits=%d (stat-range numbers the dispatcher did not know: last %d seen: %ld %ld %ld %ld %ld %ld %ld %ld)\n",
			   atomic_read(&n_kstat_unlisted), (int)KSTAT_UNLISTED_SEEN,
			   kstat_unlisted_seen[0], kstat_unlisted_seen[1],
			   kstat_unlisted_seen[2], kstat_unlisted_seen[3],
			   kstat_unlisted_seen[4], kstat_unlisted_seen[5],
			   kstat_unlisted_seen[6], kstat_unlisted_seen[7]);
	}

	{
		char line[512];
		int n = sus_path_dirent_stat_line(line, sizeof(line));

		if (n > 0)
			seq_write(m, line, min_t(int, n, (int)sizeof(line) - 1));
	}
	return 0;
}

static int kstat_proc_open(struct inode *inode, struct file *file)
{
	/* root-only, like kstat_proc_open() above - 0777 is deliberate (see susfs_kstat_init()) */
	if (current_uid().val != 0)
		return -ENOENT;
	return single_open(file, kstat_proc_show, NULL);
}

static int split_ws(char *buf, char **argv, int max)
{
	int argc = 0;
	char *p = buf;

	while (argc < max) {
		while (*p == ' ' || *p == '\t' || *p == '\n')
			p++;
		if (*p == '\0')
			break;
		argv[argc++] = p;
		while (*p && *p != ' ' && *p != '\t' && *p != '\n')
			p++;
		if (*p)
			*p++ = '\0';
	}
	return argc;
}

static ssize_t kstat_proc_write(struct file *file, const char __user *buf,
                                size_t len, loff_t *off)
{
	char cmd[768];
	char *argv[16];
	int argc, err;

	/* Same gate as kstat_proc_open(): open() alone is not enough - an fd opened by root and
	 * handed on would keep working, which is why the sibling files check both. */
	if (current_uid().val != 0)
		return -ENOENT;

	if (len >= sizeof(cmd))
		len = sizeof(cmd) - 1;
	if (copy_from_user(cmd, buf, len))
		return -EFAULT;
	cmd[len] = 0;

	argc = split_ws(cmd, argv, 16);
	if (argc == 0)
		return len;

	mutex_lock(&kstat_lock);
	err = -EINVAL;

	if (!strcmp(argv[0], "add_sus_kstat") && argc == 2)
		err = susfs_kstat_add(argv[1]);
	else if (!strcmp(argv[0], "add_sus_kstat_statically") && argc == 14)
		err = susfs_kstat_add_statically(argv, argc);
	else if (!strcmp(argv[0], "update_sus_kstat") && argc == 2)
		err = susfs_kstat_update(argv[1], false);
	else if (!strcmp(argv[0], "update_sus_kstat_full_clone") && argc == 2)
		err = susfs_kstat_update(argv[1], true);
	else if (!strcmp(argv[0], "del") && argc == 2) {
		susfs_kstat_del(argv[1]);
		err = 0;
	} else if (!strcmp(argv[0], "clear")) {
		kstat_table_clear();
		err = 0;
	}

	mutex_unlock(&kstat_lock);

	if (!err)
		kstat_maps_arm();

	if (err) {
		pr_warn("kstat proc write '%s' -> err %d\n", argv[0], err);
		/* Reported to the writer: a command that did not take effect must not look like a
		 * successful full write.  Success still returns len, so callers that expect a complete
		 * write keep working. */
		return err;
	}
	return len;
}
