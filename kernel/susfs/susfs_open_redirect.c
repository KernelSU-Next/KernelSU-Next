// SPDX-License-Identifier: GPL-2.0
/*
 * susfs_open_redirect.c - redirect open of a target path to another path (SUSFS OPEN_REDIRECT feature), LKM port.
 * Forward direction: not in this file.  path_openat / do_sys_openat2 / do_filp_open are LTO-inlined into the syscall
 * entry, so none can be kprobed, and the only out-of-line symbol on the user-open path (vfs_open(file)) sits at the
 * inode layer, where the name is already gone - a swap there has to pin a dentry and a mount per rule, which then
 * blocks umount.  Redirection is done one level up instead, at the syscall entry, by susfs_or_entry.c (wrapper
 * replacement; see that file header).  Nothing here holds a struct path any more: a rule keeps (target_ino, target_dev,
 * target_igen), the spoofed kstatfs snapshot and the two pathnames, and the entry hook resolves the name it was given.
 * Because nothing is pinned, the numbers are all that identify a rule's files: a filesystem may reuse an inode number
 * after a file is deleted, so every match also compares i_generation (the same field NFS export handles use; a
 * filesystem that does not maintain it reports 0, which is no worse than matching the number alone).  The rule table
 * is a fixed array of SUS_OR_MAX slots that ARE reused: deleting or replacing a rule retires its slot, and the slot is
 * cleared and handed out again once a grace period has passed (or_reclaim_slot() - every reader copies what it needs
 * out of an entry under rcu_read_lock, which is what makes that safe).  susfs_open_redirect_rules counts live rules, so
 * the name layer stops paying a lookup once the last rule is gone.
 * uid_scheme: enum UID_SCHEME (susfs_abi.h:67-73), all five values with upstream's predicates (susfs.c:941-964); two of
 * the five need state an LKM cannot read - see or_uid_matches() / or_in_su_domain().
 * Reverse disguise: upstream registers TWO hash entries per rule (susfs.c:844-863), the second with
 * reversed_lookup_only = true and the two pathnames swapped, and every "where did this file come from" reporter answers
 * from it - vfs_readlink() patch:510-548, do_proc_readlink() (/proc/<pid>/fd/N, exe, cwd, root) patch:1062-1083, fdinfo
 * seq_show() (mnt_id + ino) patch:1145-1217, show_map_vma() (maps dev:ino + name) patch:1257-1288, vfs_statfs()
 * (statfs/fstatfs) patch:2038-2056.  Gate: SUSFS_IS_INODE_OPEN_REDIRECT (susfs_def.h:148-151) = inode flag *and*
 * susfs_is_current_proc_umounted_app(), never the rule's uid_scheme - so even a scheme-0 rule is disguised for app
 * processes (or_reverse_visible() keeps that gate through susfs_is_current_proc_umounted_app()).
 * Reachability: none of those five is reachable from an LKM here (the first two and seq_show() are static; the
 * maps/fdinfo numbers are locals a kprobe cannot see).  Covered instead: d_path() and vfs_statfs() are kretprobed and
 * answer from the rule snapshot (both take the redirected file's dentry, so a return probe can replace what they
 * rendered - d_path() keeps its caller-supplied buffer, hence the rewrite rather than an argument swap), and where no
 * shared helper exists the line the function already formatted is rewritten the same way - show_map_vma()'s line
 * carries "maj:min ino" AND the name, both from the redirected file, and the name cannot be reached that way
 * (seq_file_path() -> seq_path() -> __d_path() is LTO-inlined: a probe on it registers and never fires - measured).
 * Never silent: registration outcome, entry-hook state and hit counts are logged and shown by
 * /proc/susfs_open_redirect, because registering only proves the symbol exists - this feature has hit that wall twice
 * (show_vma_header_prefix, then __d_path: both registered, both stayed at 0).
 *
 * /proc/susfs_open_redirect: add_open_redirect <target> <redirected> <uid_scheme> | del <target> | clear
 */
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/fs.h>
#include <linux/cred.h>
#include <linux/namei.h>
#include <linux/dcache.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include "mount.h"		/* fs/mount.h: real_mount() -> mnt_id */
#include <linux/atomic.h>	/* reverse-disguise hit counters */
#include <linux/rcupdate.h>	/* the entry hook's match runs under rcu_read_lock, like the probes */
#include <linux/statfs.h>	/* struct kstatfs: the snapshot the reverse statfs face answers from */
#include <linux/mm.h>		/* struct vm_area_struct (the maps reverse face) */
#include <linux/kdev_t.h>	/* MAJOR/MINOR, to render dev:ino the way proc does */
#include <linux/kernel.h>	/* scnprintf, for the same rendering */
#include <linux/security.h>	/* security_secctx_to_secid */
#include "susfs_abi.h"
#include "susfs_log.h"
#include "susfs.h"		/* susfs_expose_proc, sus_path_lsm_active */
#include "symbol_resolver.h"	/* find_kernel_symbol_exact */
#include "ksu_umount_gate.h"	/* susfs_is_current_proc_umounted_app (issue #34) */
#include "ksu_allow_gate.h"	/* the control-node gate is su's own */
#include "susfs_kprobe.h"
#include "susfs_or_entry.h"	/* susfs_or_entry_armed(): the forward direction lives there now */

#define SUS_OR_MAX 64
/* The ABI fields are char[256]; matching that width stops a legal long path from being truncated into a rule for a
 * different path. */
#define OR_PATH_MAX 256

/* UID_SCHEME lives in susfs_abi.h, mirroring upstream susfs.h, where the enum sits next to the ABI structs. */

/* FUSE is the one filesystem upstream refuses outright (susfs.c:824-829): the daemon resolves the name itself, so a
 * kernel-side swap does nothing or makes the request happen twice.  susfs_def.h:49-51 carries the constant for the same
 * reason: neither <linux/magic.h> (absent from this tree) nor <uapi/linux/magic.h> defines it; fs/fuse/fuse_i.h does. */
#ifndef FUSE_SUPER_MAGIC
#define FUSE_SUPER_MAGIC 0x65735546
#endif

/* Upstream's app threshold (susfs_def.h:122-125: TIF_PROC_UMOUNTED && current_uid().val >= 10000) is not a macro any
 * more: both gates that used it now go through susfs_is_current_proc_umounted_app(), whose uid half is that same literal
 * 10000 (ksu_umount_gate.h) and whose other half is KernelSU's answer. */

/* SELinux context of the su/ksu domain, resolved to a sid at init.  Upstream gets the sid from KernelSU itself
 * (susfs_set_sid(KERNEL_SU_CONTEXT, &susfs_ksu_sid), 10_enable_susfs_for_ksu.patch:2496); an LKM has to resolve the
 * string.  "u:r:ksu:s0" is the SukiSU variant this device runs (KERNEL_SU_DOMAIN "ksu"), stock KernelSU uses
 * "u:r:su:s0" - override with susfs_guard_lkm.or_su_ctx.  Kept separate from sus_mount's su_ctx and avc_spoof's
 * avc_su_ctx: parameters are per name, and defaulting to the wrong domain must not change other features' gating. */
static char or_su_ctx[128] = "u:r:ksu:s0";
module_param_string(or_su_ctx, or_su_ctx, sizeof(or_su_ctx), 0644);

static u32 or_su_sid;

/* security_cred_getsecid() is an EXPORT_SYMBOL, but GKI's module symbol list is not guaranteed to
 * carry it, so it is resolved in kallsyms like sus_mount.c does.  The wrapper needs __nocfi: kCFI
 * validates the type hash at a call through a function pointer. */
static void (*or_cred_getsecid)(const struct cred *cred, u32 *secid);

/* vfs_statfs, resolved by name: add time takes the target's statfs snapshot with it, and once the
 * entry holds no path that snapshot is the reverse statfs face's only source for those numbers. */
static int (*or_vfs_statfs_fn)(const struct path *path, struct kstatfs *buf);

/* The one indirect call into the kernel this file still makes, in its own __nocfi noinline function: a checked indirect
 * call to a kernel symbol is a CFI panic rather than an error return, and or_add() must stay checkable (the
 * cfi_sites.py gate looks for exactly this, including a helper that LTO inlined back into a caller without __nocfi). */
static __nocfi noinline int or_statfs_snapshot(const struct path *path, struct kstatfs *out)
{
	if (!or_vfs_statfs_fn)
		return -EOPNOTSUPP;
	return or_vfs_statfs_fn(path, out);
}

struct sus_or_entry {
	char target_pathname[OR_PATH_MAX];
	char redirected_pathname[OR_PATH_MAX];
	unsigned long target_ino;
	dev_t target_dev;
	/* Reverse direction: the redirected inode is the lookup key, and target_path is what the reporters above show instead. */
	unsigned long redirected_ino;
	dev_t redirected_dev;
	/* Inode generation, both directions.  A rule keeps numbers instead of references now, and numbers
	 * are reused: delete a redirected file and create another one on the same filesystem and the new
	 * file can land on the old inode number, at which point the rule would hijack an unrelated file.
	 * i_generation is what tells a reused number apart from the object the rule was registered for
	 * (the same field NFS export handles use).  Filesystems that do not maintain it report 0, which
	 * compares equal to itself - i.e. on those this is no worse than matching the number alone. */
	unsigned int target_igen;
	unsigned int redirected_igen;
	/* Reverse direction, second number: fdinfo also prints mnt_id, and for the redirected file that is the mount the
	 * redirection really opened - which can differ from the target's (e.g. /system/etc/hosts vs
	 * /data/local/tmp/hosts).  Cached at add time: the reporter has only numbers to work with. */
	unsigned long target_mnt_id;
	/* Snapshot taken at add time with vfs_statfs(), which the reverse statfs face answers from.
	 * Nothing in this entry holds a reference of any kind - no dentry, no mount, no inode - and
	 * that is what keeps a rule from pinning a filesystem in place. */
	struct kstatfs spoofed_kstatfs;
	int uid_scheme;
	/* Set while the slot is being rewritten or has been deleted.  The reader checks this with
	 * READ_ONCE and the writer clears it LAST, so false means the rest of the entry is complete. */
	bool dead;
};

static struct sus_or_entry or_entries[SUS_OR_MAX];
static int nor;		/* highest index ever handed out, plus one: the scan bound */
/* Slots that were handed out and have been taken back (or_reclaim_slot()).  A slot returns here once
 * a grace period has passed, which is what turns the UPPER BOUND into "how many rules exist at the
 * same time" instead of "how many times a rule was ever added". */
static unsigned int or_free[SUS_OR_MAX];
static unsigned int or_free_n;
/* How many rules are live right now, which is what the name-layer hook reads before it resolves anything: the hook runs
 * for every open() on the system, so a table that once had a rule must not keep costing a path lookup after that rule
 * was deleted.  Written under or_lock, read locklessly (susfs.h states the contract). */
unsigned int susfs_open_redirect_rules;
static DEFINE_MUTEX(or_lock);

/* Reverse-disguise bookkeeping.  The counters are the only way to tell a hook that never fires from one that fires and
 * matches nothing: a kprobe registers against a symbol's out-of-line copy, which GKI's full LTO may leave with no live
 * call sites (measured: five probes registered, zero hits). */
static atomic_t or_rev_dpath_hits = ATOMIC_INIT(0);
static atomic_t or_rev_statfs_hits = ATOMIC_INIT(0);

/* No release path for a rule's FILES, because an entry holds no reference to them: no dentry, no mount, no inode, only
 * numbers, strings and the statfs snapshot (file-level note).  The SLOT a rule occupies does come back, though - see
 * or_reclaim_slot(): a deleted rule's slot is handed out again after a grace period, which is what keeps the table from
 * being a budget of "how many rules were ever added". */
static void or_resolve_su_sid(void)
{
	int err;

	if (or_su_sid)
		return;
	if (!or_su_ctx[0])
		return;
	err = security_secctx_to_secid(or_su_ctx, strlen(or_su_ctx), &or_su_sid);
	if (err) {
		pr_warn("open_redirect: secctx_to_secid(%s) failed %d\n",
			or_su_ctx, err);
		or_su_sid = 0;
		return;
	}
	SUSFS_LOGI("open_redirect: su ctx \"%s\" -> sid %u (stock KernelSU uses \"u:r:su:s0\", override with susfs_guard_lkm.or_su_ctx)\n",
		or_su_ctx, or_su_sid);
}

/* Upstream susfs_is_current_ksu_domain() = (current_sid() == susfs_ksu_sid) (10_enable_susfs_for_ksu.patch:2484-2486);
 * current_sid() lives in SELinux's private objsec.h, so the LSM-agnostic security_cred_getsecid() is used instead. */
static __nocfi bool or_in_su_domain(void)
{
	u32 sid = 0;

	/* Unresolved symbol or unresolvable context: "not su" would be a guess, and for schemes 1/2 that guess redirects
	 * the very process the rule exists to spare.  or_add() refuses those schemes instead. */
	if (!or_cred_getsecid || !or_su_sid)
		return false;
	/* interrupt context: reading current->cred and walking the (static) LSM hook list never sleeps. */
	or_cred_getsecid(current_cred(), &sid);
	return sid == or_su_sid;
}

/* Upstream's reverse-disguise gate, verbatim in shape: SUSFS_IS_INODE_OPEN_REDIRECT (susfs_def.h:148-151) = flag bit AND
 * susfs_is_current_proc_umounted_app().  TIF_PROC_UMOUNTED is set by the SUSFS-patched KernelSU, which this kernel does not
 * have, so the app half is KernelSU's own ksu_uid_should_umount() answer (ksu_umount_gate.h, issue #34) rather than the
 * `uid >= 10000` proxy that also covered the manager and su-granted apps. */
static bool or_reverse_visible(void)
{
	return susfs_is_current_proc_umounted_app();
}

/* uid_scheme decision, mirroring upstream's switch in susfs_open_redirect_spoof_do_sys_openat() (susfs.c:941-964). */
static bool or_uid_matches(int scheme)
{
	switch (scheme) {
	case UID_NON_APP_PROC:			/* susfs.c:942-945 */
		return current_uid().val % 100000 < 10000;
	case UID_ROOT_PROC_EXCEPT_SU_PROC:	/* susfs.c:946-949 */
		return current_uid().val == 0 && !or_in_su_domain();
	case UID_NON_SU_PROC:			/* susfs.c:950-953 */
		return !or_in_su_domain();
	case UID_UMOUNTED_APP_PROC:		/* susfs.c:954-957 */
		/* Upstream: TIF_PROC_UMOUNTED && uid >= 10000 (susfs_def.h:122-125).  The flag is set
		 * by the SUSFS-patched KernelSU and this kernel has no such patch, so the same
		 * question is put to KernelSU by uid instead (ksu_umount_gate.h, issue #34).  What
		 * that substitution cannot reproduce is the flag's per-THREAD grain: a process of an
		 * umounted uid that was not forked from zygote answers yes here and no upstream. */
		return susfs_is_current_proc_umounted_app();
	case UID_UMOUNTED_PROC:			/* susfs.c:958-961 */
		/* Upstream: TIF_PROC_UMOUNTED alone, with no uid condition at all - which is how it
		 * covers init-derived processes.  Same substitution as above, and the uid half is
		 * deliberately NOT added here: that is the whole difference between this scheme and
		 * the one above. */
		return susfs_is_current_proc_umounted();
	default:				/* susfs.c:962-963 */
		return false;
	}
}

static struct sus_or_entry *or_find_by_path(const char *target)
{
	int i;

	for (i = 0; i < READ_ONCE(nor); i++) {
		if (READ_ONCE(or_entries[i].dead))
			continue;
		if (!strcmp(or_entries[i].target_pathname, target))
			return &or_entries[i];
	}
	return NULL;
}

static struct sus_or_entry *or_find_by_inode(unsigned long ino, dev_t dev, unsigned int gen)
{
	int i;

	for (i = 0; i < READ_ONCE(nor); i++) {
		/* dead is cleared LAST by the writer, so skipping dead entries also skips any entry whose
		 * fields are still being written. */
		if (READ_ONCE(or_entries[i].dead))
			continue;
		smp_rmb();
		if (or_entries[i].target_ino == ino &&
		    or_entries[i].target_dev == dev &&
		    or_entries[i].target_igen == gen)
			return &or_entries[i];
	}
	return NULL;
}

/* The name-layer entry hook asks this one, with the path the caller is about to open: true when a live
 * rule redirects that inode, in which case the rule's redirect target has been copied into `buf`.
 *
 * The copy happens under rcu_read_lock and is mandatory, not politeness: a slot is recycled once a
 * grace period has passed after the rule was deleted (or_reclaim_slot()), so a pointer into an
 * entry may not be used after the lookup returns.  A name that does not fit leaves buf alone and
 * counts as "no rule" - the caller then runs the original syscall, which is always correct.
 * The table read is lockless for the same reason the probes' is: `dead` is cleared LAST by the
 * writer, so a non-dead entry has complete fields. */
bool susfs_open_redirect_match_path(const struct path *path, char *buf, size_t buflen)
{
	struct inode *inode;
	struct sus_or_entry *e;
	bool hit = false;

	if (!READ_ONCE(nor) || !path->dentry)
		return false;
	inode = d_inode(path->dentry);
	if (!inode)
		return false;

	rcu_read_lock();
	e = or_find_by_inode(inode->i_ino, inode->i_sb->s_dev, inode->i_generation);
	if (e && or_uid_matches(e->uid_scheme) && e->redirected_pathname[0] &&
	    strscpy(buf, e->redirected_pathname, buflen) >= 0)
		hit = true;
	rcu_read_unlock();
	return hit;
}

/* Reverse direction: keyed on the redirected (really opened) inode, published like or_find_by_inode(). */
static struct sus_or_entry *or_find_by_redirected_inode(unsigned long ino, dev_t dev, unsigned int gen)
{
	int i;

	for (i = 0; i < READ_ONCE(nor); i++) {
		if (READ_ONCE(or_entries[i].dead))
			continue;
		smp_rmb();
		if (or_entries[i].redirected_ino == ino &&
		    or_entries[i].redirected_dev == dev &&
		    or_entries[i].redirected_igen == gen)
			return &or_entries[i];
	}
	return NULL;
}

/* Reverse direction for the one caller that has only numbers: /proc/<pid>/fdinfo/N prints "mnt_id:\t<i>" and "ino:\t<j>"
 * for the file an fd points at and no device, so this lookup is by ino alone; two rules sharing that ino refuses to
 * answer (a missed disguise beats disguising an unrelated file).  On success: the target's ino and mount id (0 unknown).
 * Only the inode number is available here, so unlike the other lookups this one cannot also compare i_generation - a
 * number that a filesystem reused stays indistinguishable from this caller's point of view. */
bool susfs_open_redirect_spoof_ids(unsigned long ino, unsigned long *out_ino,
				   unsigned long *out_mnt_id)
{
	struct sus_or_entry *e = NULL;
	int i, hits = 0;
	unsigned long tino = 0, tmnt = 0;

	if (!ino || !out_ino || !or_reverse_visible())
		return false;
	/* The numbers are copied out inside the rcu section: rule slots are recycled after a grace
	 * period (or_reclaim_slot()), so an entry may not be read after this function returns. */
	rcu_read_lock();
	for (i = 0; i < READ_ONCE(nor); i++) {
		if (READ_ONCE(or_entries[i].dead))
			continue;
		smp_rmb();
		if (or_entries[i].redirected_ino != ino)
			continue;
		e = &or_entries[i];
		if (++hits > 1)
			break;
	}
	if (hits == 1) {
		tino = e->target_ino;
		tmnt = e->target_mnt_id;
	}
	rcu_read_unlock();
	if (hits != 1)
		return false;
	*out_ino = tino;
	if (out_mnt_id)
		*out_mnt_id = tmnt;
	return true;
}

/* Is `target` another rule's redirected path?  Upstream refuses to touch such a name (susfs.c:867-881): that name
 * belongs to the reverse entry of an existing rule, and replacing it would silently break that rule's disguise. */
static bool or_is_redirected_path(const char *target)
{
	int i;

	for (i = 0; i < READ_ONCE(nor); i++) {
		if (READ_ONCE(or_entries[i].dead))
			continue;
		if (!strcmp(or_entries[i].redirected_pathname, target))
			return true;
	}
	return false;
}

/* ---- forward: not here any more (kernel/susfs_or_entry.c) ----
 * The vfs_open face worked by replacing the path argument with a cached target path, which meant every rule had to keep a
 * dentry and a mount reference alive - and those are exactly what keeps a filesystem from being unmounted while a rule
 * points into it.  The same direction is done one layer up, at the NAME: the caller's own path is resolved with
 * filename_lookup() (process context, before the syscall runs) and the target is opened with filp_open() and handed back
 * as a fd.  Nothing is cached there either, so this file holds no reference of any kind any more: what remains are the
 * numbers, the snapshot, the two names, and the three reverse faces below.
 * Matching note that survives the move: upstream leaves the lookup loop entirely when the inode matches but the scheme
 * does not (goto out_srcu_read_unlock, susfs.c:945/949/953/957/961), so there a non-matching entry also suppresses the
 * remaining same-inode entries; here the first live (ino, dev) match is the only candidate - the same outcome for
 * distinct inodes, while rules sharing one inode (hard links) differ only in which entry wins (upstream: newest
 * hash_add_rcu first, here: slot order). */

/* ---- reverse: d_path(path, buf, buflen) ----
 * Covers readlink("/proc/<pid>/fd/N"): proc_pid_readlink() -> do_proc_readlink() -> d_path(&path, tmp, PAGE_SIZE)
 * (fs/proc/base.c, upstream: patch:1062-1083).  Upstream replays the literal string given at add time, and so does this
 * now: the path argument is left alone and the name d_path() produced is rewritten in the return probe.
 *
 * The /proc/<pid>/maps NAME column is NOT here: show_map_vma() prints it with seq_file_path() -> seq_path(), which calls
 * __d_path() directly, never d_path() (fs/seq_file.c); a probe on __d_path registers cleanly and never fires here (full
 * LTO inlines the primitive; measured dpath_seq=0 while the maps line still named the redirected file), so the name is
 * rewritten out of the already-printed line - see or_maps_ret(). */
struct or_dpath_args {
	char *buf;
	int buflen;
	/* The name is COPIED here in the entry handler, not pointed at: rule slots are recycled after a
	 * grace period (or_reclaim_slot()), and the return handler runs long after the entry handler
	 * returned - with no rcu_read_lock in between to cover a slot being handed out again. */
	char target[OR_PATH_MAX];
};

static int or_dpath_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	const struct path *path = (const struct path *)regs->regs[0];
	struct or_dpath_args *a = (struct or_dpath_args *)ri->data;
	struct inode *inode;
	struct sus_or_entry *e;
	bool hit = false;

	a->target[0] = '\0';
	if (!READ_ONCE(nor) || IS_ERR_OR_NULL(path) || !path->dentry)
		return 0;
	if (!or_reverse_visible())
		return 0;
	inode = d_backing_inode(path->dentry);
	if (!inode)
		return 0;

	/* The lookup and the copy are one rcu section: that is what makes it safe for or_del() to
	 * recycle the slot afterwards (see or_reclaim_slot()). */
	rcu_read_lock();
	e = or_find_by_redirected_inode(inode->i_ino, inode->i_sb->s_dev, inode->i_generation);
	if (e && e->target_pathname[0] &&
	    strscpy(a->target, e->target_pathname, sizeof(a->target)) >= 0)
		hit = true;
	rcu_read_unlock();
	if (!hit)
		return 0;

	a->buf = (char *)regs->regs[1];
	a->buflen = (int)regs->regs[2];
	atomic_inc(&or_rev_dpath_hits);
	return 0;
}

static int or_dpath_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	const struct or_dpath_args *a = (const struct or_dpath_args *)ri->data;
	char *ret;

	if (!a->target[0] || !a->buf || a->buflen <= 0)
		return 0;
	ret = (char *)regs->regs[0];
	if (IS_ERR(ret))
		return 0;		/* d_path failed: keep the error the caller expects */
	/* Checked before writing: strscpy() truncates by filling the whole buffer and NUL-terminating
	 * its last byte, so "it did not fit" would still have destroyed the name the kernel produced.
	 * A target name that long leaves that name alone instead. */
	if (strnlen(a->target, (size_t)a->buflen) >= (size_t)a->buflen)
		return 0;
	strscpy(a->buf, a->target, (size_t)a->buflen);
	/* d_path() may return a pointer into the middle of the buffer (it trims a leading " (deleted)"); the rewritten name
	 * starts at buf, so the return value has to move with it or the caller reads from the wrong offset. */
	regs->regs[0] = (unsigned long)a->buf;
	return 0;
}

/* ---- reverse: vfs_statfs(path, buf) ----
 * Upstream answers statfs()/fstatfs() of the redirected file with the target's kstatfs snapshot, taken with vfs_statfs()
 * at add time (susfs.c:1029-1046, susfs.c:851).  This does the same instead of swapping in a cached target path: the
 * snapshot is copied into the return-probe data and written over the caller's buffer, so there is no target path to
 * hold - which is what used to keep a filesystem busy while a rule pointed into it. */
struct or_statfs_args {
	struct kstatfs *buf;	/* the caller's buffer, taken in the ENTRY probe - see or_statfs_entry() */
	struct kstatfs kstatfs;
	bool hit;
};

static int or_statfs_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	const struct path *path = (const struct path *)regs->regs[0];
	struct or_statfs_args *a = (struct or_statfs_args *)ri->data;
	struct inode *inode;
	struct sus_or_entry *e;

	a->hit = false;
	a->buf = NULL;
	if (!READ_ONCE(nor) || IS_ERR_OR_NULL(path) || !path->dentry)
		return 0;
	if (!or_reverse_visible())
		return 0;
	inode = d_backing_inode(path->dentry);
	if (!inode)
		return 0;

	/* One rcu section for the lookup and the snapshot copy: rule slots are recycled after a grace
	 * period (or_reclaim_slot()), so nothing inside an entry may be read outside it. */
	rcu_read_lock();
	e = or_find_by_redirected_inode(inode->i_ino, inode->i_sb->s_dev, inode->i_generation);
	if (!e) {
		rcu_read_unlock();
		return 0;
	}

	/* Second argument of vfs_statfs().  Read HERE, not in the return probe: a return probe only has the return value,
	 * the argument registers belong to whatever the callee last did with them (measured on 5.15 - the rewrite went to
	 * a stale x1 and userspace kept seeing the redirected file's own numbers). */
	a->buf = (struct kstatfs *)regs->regs[1];
	if (!susfs_ptr_plausible(a->buf)) {
		rcu_read_unlock();
		return 0;
	}

	/* The snapshot is copied here as well, not in the return probe: the entry could be rewritten in between, and the
	 * copy is what the caller's buffer will be filled from, so it has to be one consistent moment. */
	a->kstatfs = e->spoofed_kstatfs;
	rcu_read_unlock();
	a->hit = true;
	atomic_inc(&or_rev_statfs_hits);
	return 0;
}

static int or_statfs_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	const struct or_statfs_args *a = (const struct or_statfs_args *)ri->data;

	if (!a->hit || !a->buf || (long)regs->regs[0] != 0)
		return 0;		/* the kernel refused the statfs: its answer stands */
	*a->buf = a->kstatfs;
	return 0;
}

static struct kretprobe kr_or_dpath = {
	.kp.symbol_name = "d_path",
	.entry_handler = or_dpath_entry,
	.handler = or_dpath_ret,
	.data_size = sizeof(struct or_dpath_args),
	.maxactive = 16,
};


static struct kretprobe kr_or_vfs_statfs = {
	.kp.symbol_name = "vfs_statfs",
	.entry_handler = or_statfs_entry,
	.handler = or_statfs_ret,
	.data_size = sizeof(struct or_statfs_args),
	.maxactive = 16,
};

/* ---- reverse face 3: the dev:ino columns of /proc/<pid>/maps (and smaps) ----
 * The first attempt put this on show_vma_header_prefix() - args 6 and 7 of that call ARE the two columns - and it
 * registered and never fired once (measured: the vma_hdr counter stayed 0 over every run; clang's full LTO leaves no
 * out-of-line copy).  So the already-formatted line is rewritten instead: its two numbers come from vma->vm_file, i.e.
 * from the REDIRECTED inode, and replacing the "<maj>:<min> <ino>" run with the target's keeps the line consistent with
 * the name face 1 (d_path) already disguises - without it a detector reads the target's name next to the redirected
 * file's device, which no file on this system can produce.  The run is rendered as fs/proc/task_mmu.c does it
 * (seq_put_hex_ll for major/minor: lowercase, min width 2; seq_put_decimal_ull for the ino), both surrounding spaces
 * included, so pgoff and name stay safe. */
static atomic_t or_rev_maps_hits = ATOMIC_INIT(0);
static atomic_t or_rev_maps_rewrites = ATOMIC_INIT(0);	/* the maj:min ino run */
static atomic_t or_rev_maps_names = ATOMIC_INIT(0);	/* the name column */

struct or_maps_args {
	struct seq_file *m;
	struct vm_area_struct *vma;
};

static int or_maps_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct or_maps_args *a = (struct or_maps_args *)ri->data;

	a->m = (struct seq_file *)regs->regs[0];
	a->vma = (struct vm_area_struct *)regs->regs[1];
	return 0;
}

/* Replace the first occurrence of old[0..old_len) in the seq_file buffer with new[0..new_len); growing is allowed as
 * long as the buffer has room, and without room the line is left alone rather than truncated. */
static bool or_buf_replace(struct seq_file *m, const char *old, size_t old_len,
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

static int or_maps_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	const struct or_maps_args *a = (const struct or_maps_args *)ri->data;
	struct seq_file *m = a->m;
	struct vm_area_struct *vma = a->vma;
	struct inode *inode;
	struct sus_or_entry *e;
	char old[48], new[48];
	int old_len, new_len;

	if (!susfs_ptr_plausible(m) || !susfs_ptr_plausible(vma) ||
	    !m->buf || !m->count || !vma->vm_file)
		return 0;
	if (!or_reverse_visible())
		return 0;
	inode = file_inode(vma->vm_file);
	if (!inode)
		return 0;

	/* One rcu section around the lookup and everything read from the entry: rule slots are recycled
	 * after a grace period (or_reclaim_slot()), so nothing inside an entry may be touched outside
	 * this section.  Nothing here sleeps - the two buffer rewrites below are memmove/memcpy. */
	rcu_read_lock();
	e = or_find_by_redirected_inode(inode->i_ino, inode->i_sb->s_dev, inode->i_generation);
	if (!e) {
		rcu_read_unlock();
		return 0;
	}

	atomic_inc(&or_rev_maps_hits);

	old_len = scnprintf(old, sizeof(old), "%02x:%02x %lu",
			    (unsigned int)MAJOR(inode->i_sb->s_dev),
			    (unsigned int)MINOR(inode->i_sb->s_dev),
			    (unsigned long)inode->i_ino);
	new_len = scnprintf(new, sizeof(new), "%02x:%02x %lu",
			    (unsigned int)MAJOR(e->target_dev),
			    (unsigned int)MINOR(e->target_dev),
			    (unsigned long)e->target_ino);
	/* Keep the column width: the kernel padded the name out to a fixed column, so a shorter run would pull the name
	 * left - a line that does not line up with its neighbours is visible on its own. */
	while (new_len < old_len && new_len < (int)sizeof(new) - 1)
		new[new_len++] = ' ';
	if (old_len > 0 && new_len > 0 &&
	    or_buf_replace(m, old, (size_t)old_len, new, (size_t)new_len))
		atomic_inc(&or_rev_maps_rewrites);

	/* The name column as well: show_map_vma() prints it via seq_file_path()/seq_path()/__d_path(), and (see the d_path
	 * block above) a probe on that primitive never fires here - so the rendered name is matched as the REGISTERED
	 * redirected path, and a rule through a symlink or another mount namespace misses instead of mislabelling a file. */
	if (e->redirected_pathname[0] && e->target_pathname[0]) {
		size_t rlen = strlen(e->redirected_pathname);
		size_t tlen = strlen(e->target_pathname);

		if (or_buf_replace(m, e->redirected_pathname, rlen,
				   e->target_pathname, tlen))
			atomic_inc(&or_rev_maps_names);
	}
	rcu_read_unlock();
	return 0;
}

static struct kretprobe kr_or_maps = {
	.kp.symbol_name = "show_map_vma",	/* fs/proc/task_mmu.c */
	.entry_handler = or_maps_entry,
	.handler = or_maps_ret,
	.data_size = sizeof(struct or_maps_args),
	.maxactive = 16,
};
static bool or_maps_registered;

/* ---- reverse face 4: /proc/<pid>/fdinfo/N ----
 * fdinfo prints "ino:\t<i>" for the file an fd points at - for a rule, the REDIRECTED one - so a detector holding an fd
 * on the target is handed the inode of the file the redirection really opened.  Upstream rewrites it in the same function
 * it rewrites mnt_id in (susfs_open_redirect_spoof_seq_show, patch:1171-1200).
 * It belongs to THIS feature, not to sus_mount's: it has to fire as soon as one rule exists, whether or not the
 * mount-hiding switch is on.  Measured with the first version (a sus_mount seq_show kretprobe, registered only when that
 * feature is enabled): with a rule present and hide off, fdinfo kept naming the redirected inode and the probe had
 * entry=0.  fdinfo has no device column, so the lookup is by ino alone and refuses when two rules share a redirected ino;
 * state is per-instance (ri->data) because seq_show() may sleep in seq_printf() and the task can migrate CPUs. */
static atomic_t or_rev_fdinfo_hits = ATOMIC_INIT(0);
static atomic_t or_rev_fdinfo_rewrites = ATOMIC_INIT(0);

static int or_fdinfo_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct seq_file **slot = (struct seq_file **)ri->data;

	*slot = (struct seq_file *)regs->regs[0];
	return 0;
}

/* Find `label` in the already formatted buffer and parse the decimal that follows it; false when
 * the label is absent or has no digits.  procfs prints inode and mount ids as unsigned long, so
 * keep the full native width here instead of the 32-bit limit that hid large inode numbers from
 * the rewrite. */
static bool or_fdinfo_find_dec(struct seq_file *m, const char *label, size_t label_len,
			       size_t *out_pos, size_t *out_len, unsigned long *out_val)
{
	char *buf = m->buf;
	size_t count = m->count, i, pos = 0, len = 0;
	unsigned long v = 0;

	for (i = 0; i + label_len < count; i++) {
		if (!memcmp(buf + i, label, label_len)) {
			pos = i + label_len;
			break;
		}
	}
	if (!pos)
		return false;
	while (pos + len < count && len < sizeof(unsigned long) * 3 &&
	       buf[pos + len] >= '0' && buf[pos + len] <= '9') {
		v = v * 10 + (unsigned long)(buf[pos + len] - '0');
		len++;
	}
	if (!len)
		return false;
	*out_pos = pos;
	*out_len = len;
	*out_val = v;
	return true;
}

/* Write new_val over the len digits at pos, growing or shrinking as needed.  The replacement can be LONGER (measured:
 * 926498 -> 10166500 was refused by an earlier shrink-only version, so the hook reported hits with zero rewrites);
 * growing needs room in the seq_file buffer, and without room the line is left alone rather than truncated. */
static bool or_fdinfo_write_dec(struct seq_file *m, size_t pos, size_t len,
				unsigned long new_val)
{
	char digits[sizeof(unsigned long) * 3];
	size_t count = m->count, i, n = 0;
	unsigned long v = new_val;

	while (v) {
		digits[n++] = (char)('0' + v % 10);
		v /= 10;
	}
	if (!n)
		return false;
	if (n > len && count + (n - len) >= m->size)
		return false;
	for (i = 0; i < n / 2; i++) {
		char t = digits[i];

		digits[i] = digits[n - 1 - i];
		digits[n - 1 - i] = t;
	}
	if (n != len)
		memmove(m->buf + pos + n, m->buf + pos + len, count - (pos + len));
	memcpy(m->buf + pos, digits, n);
	m->count = count - len + n;
	return true;
}

static int or_fdinfo_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct seq_file *m = *(struct seq_file **)ri->data;
	size_t pos, len;
	unsigned long old = 0, new_ino = 0, new_mnt = 0;
	int rewrites = 0;

	if (!m || (long)regs_return_value(regs) != 0)
		return 0;
	if (!m->buf || !m->count)
		return 0;
	/* Only fdinfo's own file is rewritten, and above 5.15 this probe sits on ALL FOUR functions
	 * named seq_show (see kr_or_fdinfo[]): the file being read is m->file, and
	 * /proc/<pid>/fdinfo/<fd> (and .../task/<tid>/fdinfo/<fd>) has a dentry whose parent is named
	 * "fdinfo".  The "ino:\t" / "mnt_id:\t" label tests below already make the scan a no-op for
	 * another file; this keeps it off other files' buffers in the first place. */
	if (!m->file || !m->file->f_path.dentry || !m->file->f_path.dentry->d_parent ||
	    strcmp(m->file->f_path.dentry->d_parent->d_name.name, "fdinfo") != 0)
		return 0;
	if (!or_reverse_visible())
		return 0;

	atomic_inc(&or_rev_fdinfo_hits);

	/* The ino is the rule's key and the same rule knows the target's mount id, so one lookup answers both lines.  ino
	 * first: it sits after mnt_id in the line, so shrinking or growing it cannot move that one. */
	if (or_fdinfo_find_dec(m, "ino:\t", 5, &pos, &len, &old) &&
	    old && susfs_open_redirect_spoof_ids(old, &new_ino, &new_mnt) &&
	    new_ino != old && or_fdinfo_write_dec(m, pos, len, new_ino))
		rewrites++;

	/* mnt_id, when a rule matched.  sus_mount rewrites this same label from ITS table and both probes are on the same
	 * function, so for an fd that is both "inside a hidden mount" and "the redirected file" the two return handlers race
	 * for which id wins - cosmetic (both are ids the caller could have been shown), and invisible in either module alone. */
	if (new_mnt && or_fdinfo_find_dec(m, "mnt_id:\t", 8, &pos, &len, &old) &&
	    old != new_mnt && or_fdinfo_write_dec(m, pos, len, new_mnt))
		rewrites++;

	if (rewrites)
		atomic_inc(&or_rev_fdinfo_rewrites);
	return 0;
}

/* fs/proc/fd.c's fdinfo callback is reached through single_open(file, seq_show, inode): no table
 * holds its address, so it can only be found by name - and ABOVE 5.15 THE NAME IS NOT UNIQUE.  The
 * DDK trees carry four symbols called exactly `seq_show` on android14-6.1 and android15-6.6 (one on
 * 5.10/5.15), and register_kprobe(.symbol_name = ...) attaches to whichever kallsyms lists first.
 * That is not a theoretical hazard: it is exactly how this rewrite silently stopped working in
 * sus_mount on those kernels (mountinfo showed the disguised id while /proc/<pid>/fdinfo/N printed
 * the real one - a one-file oracle), and the registration here was the same shape.  So enumerate
 * every match and hook all of them; the return handler decides by the file it sees, which makes an
 * unrelated seq_show cost one comparison and change nothing. */
#define OR_FDINFO_MAX 8

static struct kretprobe kr_or_fdinfo[OR_FDINFO_MAX];
static int n_or_fdinfo_probes;

static bool or_registered;
static bool or_dpath_registered;
static bool or_statfs_registered;

/* Set by susfs_open_redirect_exit() while it holds or_lock; registration refuses afterwards.
 * The /proc node does NOT hold a module reference (proc_create(..., NULL, ...) takes no owner),
 * so a write that is already inside or_proc_write() can reach or_register() while the module is
 * being torn down: without this flag the forward probe would be registered again after
 * or_unregister() cleared or_registered, and its handler would outlive the module. */
static bool or_exiting;

/* The forward direction is the feature, but it is no longer a probe in this file: it lives at the name layer
 * (kernel/susfs_or_entry.c, installed on the syscall entries).  A rule is useless if that layer is not armed - a rule that
 * cannot fire is worse than no rule - so "arming" here means checking it, and the failure is reported to the caller. */
static int or_register(void)
{
	if (or_exiting)
		return -ENODEV;
	if (or_registered)
		return 0;
	if (!susfs_or_entry_armed())
		return -ENODEV;
	or_registered = true;
	SUSFS_LOGI("susfs_open_redirect: forward direction is the name layer (armed)\n");
	return 0;
}

/* Reverse-disguise hooks: best effort.  The forward redirect is already live and each of these only closes one report
 * path, so a failure must not reject the rule - but never silent either: registering proves the symbol exists, not that
 * the kernel's call sites reach it (GKI's full LTO inlines across translation units), hence the hit counters read back
 * from /proc/susfs_open_redirect.  Called from the rule-management paths (process context, may sleep). */
static void or_register_reverse(void)
{
	int rc;

	if (or_exiting)
		return;

	if (!or_dpath_registered) {
		rc = register_kretprobe(&kr_or_dpath);
		if (rc) {
			susfs_krp_forget_addr(&kr_or_dpath);
			pr_warn("open_redirect: register_kretprobe(d_path) failed %d - readlink not disguised (or already inlined)\n",
				rc);
		} else {
			or_dpath_registered = true;
			SUSFS_LOGI("susfs_open_redirect: reverse hook installed (d_path)\n");
		}
	}

	if (!or_statfs_registered) {
		rc = register_kretprobe(&kr_or_vfs_statfs);
		if (rc) {
			susfs_krp_forget_addr(&kr_or_vfs_statfs);
			pr_warn("open_redirect: register_kretprobe(vfs_statfs) failed %d - statfs not disguised (or already inlined)\n",
				rc);
		} else {
			or_statfs_registered = true;
			SUSFS_LOGI("susfs_open_redirect: reverse hook installed (vfs_statfs)\n");
		}
	}
	if (!or_maps_registered) {
		rc = register_kretprobe(&kr_or_maps);
		if (rc) {
			susfs_krp_forget_addr(&kr_or_maps);
			pr_warn("open_redirect: register_kretprobe(show_map_vma) failed %d - the maps dev:ino stays the redirected file's\n",
				rc);
		} else {
			or_maps_registered = true;
			SUSFS_LOGI("susfs_open_redirect: reverse hook installed (show_map_vma)\n");
		}
	}
	if (!n_or_fdinfo_probes) {
		unsigned long addrs[OR_FDINFO_MAX];
		int i, n;

		/* Every symbol with this name - see kr_or_fdinfo[] for why one is not enough. */
		n = ksu_find_symbol_all("seq_show", addrs, OR_FDINFO_MAX);
		if (n <= 0)
			pr_warn("open_redirect: seq_show not resolved - fdinfo names the redirected inode\n");
		for (i = 0; i < n; i++) {
			struct kretprobe *kr = &kr_or_fdinfo[i];

			kr->kp.addr = (void *)addrs[i];
			kr->entry_handler = or_fdinfo_entry;
			kr->handler = or_fdinfo_ret;
			kr->data_size = sizeof(struct seq_file *);
			kr->maxactive = 16;
			rc = register_kretprobe(kr);
			if (rc) {
				pr_warn("open_redirect: register_kretprobe(seq_show @%px) failed %d\n",
					(void *)addrs[i], rc);
				susfs_krp_forget_addr(kr);
				continue;
			}
			n_or_fdinfo_probes++;
		}
		if (n_or_fdinfo_probes)
			SUSFS_LOGI("susfs_open_redirect: reverse hook installed (seq_show/fdinfo on %d/%d symbol(s))\n",
				   n_or_fdinfo_probes, n);
		else
			pr_warn("open_redirect: no seq_show kretprobe could be registered - fdinfo names the redirected inode\n");
	}
}

/* Caller MUST hold or_lock: disarming outside it is what let a concurrent or_register() put the
 * probes back after they were removed - and those probes would then point into a module that is
 * about to be freed.  susfs_open_redirect_exit() takes the lock and sets or_exiting before it
 * calls this, so no registration can slip in between. */
static void or_unregister(void)
{
	{
		int i;

		for (i = 0; i < OR_FDINFO_MAX; i++) {
			if (!kr_or_fdinfo[i].kp.addr)
				continue;
			unregister_kretprobe(&kr_or_fdinfo[i]);
			susfs_krp_forget_addr(&kr_or_fdinfo[i]);
		}
		n_or_fdinfo_probes = 0;
	}
	if (or_maps_registered) {
		unregister_kretprobe(&kr_or_maps);
		susfs_krp_forget_addr(&kr_or_maps);
		or_maps_registered = false;
	}
	if (or_statfs_registered) {
		unregister_kretprobe(&kr_or_vfs_statfs);
		susfs_krp_forget_addr(&kr_or_vfs_statfs);
		or_statfs_registered = false;
	}
	if (or_dpath_registered) {
		unregister_kretprobe(&kr_or_dpath);
		susfs_krp_forget_addr(&kr_or_dpath);
		or_dpath_registered = false;
	}

	/* Nothing of the forward direction to take down: it is the name layer's hook pointer, cleared by
	 * susfs_or_entry_exit() from this layer's exit before the reverse probes come down. */
	or_registered = false;
	SUSFS_LOGI("susfs_open_redirect: hooks removed\n");
}

/* ---- /proc/susfs_open_redirect ---- */
static int or_proc_show(struct seq_file *m, void *v);
static int or_proc_open(struct inode *inode, struct file *file);
static ssize_t or_proc_write(struct file *file, const char __user *buf,
			     size_t len, loff_t *off);

static const struct proc_ops or_proc_ops = {
	.proc_open = or_proc_open,
	.proc_read = seq_read,
	.proc_write = or_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static struct proc_dir_entry *or_proc_entry;

int susfs_open_redirect_init(void)
{
	int i;

	/* Every slot starts OUT of service.  The array is static, so a fresh slot would otherwise read
	 * as dead == false - i.e. as a complete rule - and a lockless reader scans up to `nor`, which
	 * grows before its slot is filled.  With the flag raised here, "never used" and "reclaimed" are
	 * the same state, or_add() can publish a slot by simply growing the bound, and no reader has to
	 * order its load of `nor` against its load of `dead` (arm64 may reorder those two loads, so a
	 * barrier in the writer would not have been enough - see the note in or_add()). */
	for (i = 0; i < SUS_OR_MAX; i++)
		WRITE_ONCE(or_entries[i].dead, true);

	or_cred_getsecid =
		(void *)find_kernel_symbol_exact("security_cred_getsecid");
	if (!or_cred_getsecid)
		pr_warn("open_redirect: security_cred_getsecid not found - schemes 1/2 will be refused\n");
	or_resolve_su_sid();

	/* Only the /proc node is optional.  This gate must never return early and skip other work: use_proc is a policy
	 * switch, not a feature switch.  0777 so DAC passes and sus_path's LSM layer answers ENOENT;
	 * without that layer the node would be world-writable, so it is not created at all (see susfs_kstat_init()). */
	if (susfs_control_node_allowed()) {
		or_proc_entry = proc_create("susfs_open_redirect", 0777, NULL,
					    &or_proc_ops);
		if (!or_proc_entry)
			pr_warn("proc_create(susfs_open_redirect) failed\n");
	} else {
		SUSFS_LOGI("susfs_open_redirect: /proc node not created (expose_proc=%d lsm=%d)\n",
			(int)susfs_expose_proc, (int)sus_path_lsm_active());
	}

	/* The name layer is the forward direction now, so resolve what it needs and take the target's
	 * statfs snapshot with it.  or_register() refuses a rule while it is not armed: a rule that
	 * cannot fire is worse than no rule.  (The boolean is the layer's state, not or_registered:
	 * that one only flips when the first rule is added, so it reads "lazy" before then.) */
	or_vfs_statfs_fn = (void *)find_kernel_symbol_exact("vfs_statfs");
	if (!or_vfs_statfs_fn)
		pr_warn("open_redirect: vfs_statfs not found - the reverse statfs face will report a zeroed snapshot\n");
	susfs_or_entry_init();

	SUSFS_LOGI("susfs_open_redirect: %d rules (forward %s, proc %d)\n", nor,
		susfs_or_entry_armed() ? "armed" : "unavailable", or_proc_entry != NULL);
	return 0;
}

void susfs_open_redirect_exit(void)
{
	int i;

	/* Stop being reachable from the syscall table first: the hook pointer is cleared before the
	 * probes come down, so no entry that is still installed can call into this layer's exit. */
	susfs_or_entry_exit();

	/* "Stop accepting registrations" and "drop the probes" are one critical section: a /proc
	 * write holds no module reference, so it can be sitting inside or_proc_write() right now,
	 * and it would otherwise walk into or_register() after or_unregister() had cleared
	 * or_registered - re-registering probes with handlers that outlive the module. */
	mutex_lock(&or_lock);
	or_exiting = true;
	or_unregister();
	mutex_unlock(&or_lock);

	if (or_proc_entry) {
		proc_remove(or_proc_entry);
		or_proc_entry = NULL;
	}

	/* Nothing can reach these any more: the probes are already gone.  There is nothing to release
	 * either - an entry holds numbers, a kstatfs snapshot and two strings, and the paths it parsed
	 * when it was added were dropped right there.  This used to walk the table path_put()ing each
	 * entry's cached struct path. */
	for (i = 0; i < READ_ONCE(nor); i++)
		or_entries[i].dead = true;
	nor = 0;
	or_free_n = 0;		/* nothing is left to hand out: every slot is dead again */
	WRITE_ONCE(susfs_open_redirect_rules, 0);	/* the count the name layer reads; hook is already gone */
}

static int or_proc_show(struct seq_file *m, void *v)
{
	int i;

	mutex_lock(&or_lock);
	if (!susfs_open_redirect_rules) {
		seq_puts(m, "(empty)\n");
	} else {
		for (i = 0; i < READ_ONCE(nor); i++) {
			if (READ_ONCE(or_entries[i].dead))
				continue;
			seq_printf(m, "%s -> %s uid=%d (ino=%lu dev=%lu mnt=%lu | rev: ino=%lu dev=%lu)\n",
				   or_entries[i].target_pathname,
				   or_entries[i].redirected_pathname,
				   or_entries[i].uid_scheme,
				   or_entries[i].target_ino,
				   (unsigned long)or_entries[i].target_dev,
				   or_entries[i].target_mnt_id,
				   or_entries[i].redirected_ino,
				   (unsigned long)or_entries[i].redirected_dev);
		}
	}
	/* "registered" is not "reached": a hook whose counter stays 0 across a real read means GKI inlined its call sites
	 * and that surface is NOT disguised - hence the counters below (fdinfo's surface and provenance are in the face-4
	 * block above).  mnt_id is NOT rewritten here: that id belongs to sus_mount's table. */
	seq_printf(m, "hooks: forward=%d dpath=%d statfs=%d maps=%d fdinfo=%d | rev hits: dpath=%d statfs=%d maps=%d/%d/%d fdinfo=%d/%d | su_sid=%u\n",
		   susfs_or_entry_armed(), or_dpath_registered, or_statfs_registered,
		   or_maps_registered, n_or_fdinfo_probes > 0,
		   atomic_read(&or_rev_dpath_hits),
		   atomic_read(&or_rev_statfs_hits),
		   atomic_read(&or_rev_maps_hits),
		   atomic_read(&or_rev_maps_rewrites),
		   atomic_read(&or_rev_maps_names),
		   atomic_read(&or_rev_fdinfo_hits),
		   atomic_read(&or_rev_fdinfo_rewrites),
		   or_su_sid);
	mutex_unlock(&or_lock);
	return 0;
}

static int or_proc_open(struct inode *inode, struct file *file)
{
	/* 0777 is deliberate (the ENOENT contract comes from sus_path's hidden set, not from the mode), so refuse non-root
	 * callers here too - see the note in susfs_enable_log.c's log_proc_open(). */
	if (!susfs_control_visible())
		return -ENOENT;
	return single_open(file, or_proc_show, NULL);
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

static void or_reclaim_slot(struct sus_or_entry *e);

static int or_add(const char *target, const char *redirected, int scheme)
{
	struct sus_or_entry *e;
	struct path tp, rp;
	struct inode *ti, *ri;
	struct kstatfs spoofed_kstatfs;
	int rc;

	/* upstream susfs.c:792-796 */
	if (scheme < UID_NON_APP_PROC || scheme > UID_UMOUNTED_PROC)
		return -EINVAL;

	/* Both come from char[256] ABI fields (supercall) or a NUL-terminated command buffer (proc write); reject the
	 * unterminated case instead of letting strcmp()/kern_path() read past the struct or truncate a path into a rule. */
	if (!susfs_abi_path_ok(target, OR_PATH_MAX) ||
	    !susfs_abi_path_ok(redirected, OR_PATH_MAX))
		return -ENAMETOOLONG;

	rc = kern_path(target, LOOKUP_FOLLOW, &tp);
	if (rc)
		return rc;
	/* The statfs snapshot the reverse face answers from, taken here while the target's path is in
	 * hand.  The path itself is dropped before this function returns; only the snapshot stays. */
	memset(&spoofed_kstatfs, 0, sizeof(spoofed_kstatfs));
	(void)or_statfs_snapshot(&tp, &spoofed_kstatfs);
	ti = d_backing_inode(tp.dentry);
	if (!ti) {
		path_put(&tp);
		return -ENOENT;
	}

	/* resolve redirected; only its numbers are kept (nothing is cached any more) */
	rc = kern_path(redirected, LOOKUP_FOLLOW, &rp);
	if (rc) {
		path_put(&tp);
		return rc;
	}
	ri = d_backing_inode(rp.dentry);
	if (!ri) {
		path_put(&rp);
		path_put(&tp);
		return -ENOENT;
	}

	/* upstream susfs.c:824-829 - FUSE is refused outright, on either side, and rejecting is the whole handling: no
	 * silent rewrite, no partial rule.  (d_sb is the dentry's superblock: what upstream reads through inode->i_sb.) */
	if (tp.dentry->d_sb->s_magic == FUSE_SUPER_MAGIC ||
	    rp.dentry->d_sb->s_magic == FUSE_SUPER_MAGIC) {
		pr_warn("open_redirect: FUSE fs is not supported for open_redirect feature\n");
		path_put(&rp);
		path_put(&tp);
		return -EINVAL;
	}

	/* Schemes 1 and 2 are decisions about the KernelSU su domain.  Upstream always has that sid (KernelSU hands it
	 * over at setuid-hook setup), an LKM resolves the context itself - and if it does not resolve, "not in su domain"
	 * is true for the su process too, i.e. the one process the rule exists to spare would be redirected.  Refuse. */
	if (scheme == UID_ROOT_PROC_EXCEPT_SU_PROC ||
	    scheme == UID_NON_SU_PROC) {
		or_resolve_su_sid();
		if (!or_su_sid) {
			pr_warn("open_redirect: scheme %d needs a resolvable su domain - set or_su_ctx=u:r:su:s0 (currently \"%s\")\n",
				scheme, or_su_ctx);
			path_put(&rp);
			path_put(&tp);
			return -EOPNOTSUPP;
		}
	}

	/* Register the hooks BEFORE touching any entry: a rule that is listed but cannot fire (hook missing) silently does
	 * nothing while looking configured - worse than no rule at all. */
	rc = or_register();
	if (rc) {
		path_put(&rp);
		path_put(&tp);
		pr_warn("open_redirect: hook registration failed %d\n", rc);
		return rc;
	}
	or_register_reverse();

	/* upstream susfs.c:867-881: a name that another rule already uses as its redirected path belongs to that rule's
	 * reverse entry and must not be taken over. */
	e = or_find_by_path(target);
	if (!e && or_is_redirected_path(target)) {
		pr_warn("open_redirect: '%s' cannot be added because it is used for reversed lookup only\n",
			target);
		path_put(&rp);
		path_put(&tp);
		return -EINVAL;
	}

	/* Capacity is checked before anything is retired, so a full table cannot silently kill the rule
	 * that was working: the only way to fail here is a NEW rule with no free slot and no unused slot
	 * left.  Rewriting an existing rule takes the slot the rewrite releases, so it never fails on
	 * capacity - see the reclaim below. */
	if (!e && !or_free_n && nor >= SUS_OR_MAX) {
		path_put(&rp);
		path_put(&tp);
		return -ENOSPC;
	}

	if (e) {
		/* Rewriting a rule: take the old slot back (grace period included), which is also what makes
		 * the new entry possible even with every slot in use.  The live count does not change (one
		 * rule out, one in below), so it is only touched in the branch that adds a rule. */
		or_reclaim_slot(e);
		e = NULL;
	} else {
		/* A new rule: make it visible to the name-layer hook before the slot itself is filled.  That is the safe
		 * direction - the hook then resolves one name it would not have had to resolve, instead of missing a rule. */
		WRITE_ONCE(susfs_open_redirect_rules, READ_ONCE(susfs_open_redirect_rules) + 1);
	}

	/* A reclaimed slot first, a never-used one otherwise.  Both are already out of service (dead is
	 * raised for every slot in susfs_open_redirect_init() and stays raised while a slot is on the
	 * free list), so growing the bound cannot publish a half-written entry: a lockless reader that
	 * sees the new `nor` finds dead == true and skips the slot.  Ordering the flag against the bound
	 * was tried and is not enough on arm64, which may reorder the reader's load of `nor` against its
	 * load of `dead` - raising the flag up front is what removes the window instead of narrowing
	 * it. */
	if (or_free_n) {
		e = &or_entries[or_free[--or_free_n]];
	} else {
		e = &or_entries[nor++];
	}

	/* Then fill it in, and publish by clearing the flag last.  Every field is written before that
	 * store, the snapshot and both pathnames included - so a reader that passed its own `dead` check
	 * sees a complete rule. */
	strscpy(e->target_pathname, target, OR_PATH_MAX);
	strscpy(e->redirected_pathname, redirected, OR_PATH_MAX);
	e->target_ino = ti->i_ino;
	e->target_dev = ti->i_sb->s_dev;
	e->target_igen = ti->i_generation;
	e->target_mnt_id = (unsigned long)real_mount(tp.mnt)->mnt_id;
	e->redirected_ino = ri->i_ino;
	e->redirected_dev = ri->i_sb->s_dev;
	e->redirected_igen = ri->i_generation;
	e->spoofed_kstatfs = spoofed_kstatfs;
	e->uid_scheme = scheme;
	smp_wmb();
	WRITE_ONCE(e->dead, false);	/* publish last: readers key off this */

	/* Both parsed paths are dropped right here: the entry keeps the numbers and the snapshot, and
	 * the names as strings, so the rule holds nothing that a filesystem teardown would wait for. */
	path_put(&rp);
	path_put(&tp);
	return 0;
}

/* The second half of a reclaim, without the wait: clear a slot that nobody can be looking at any more
 * and put it back on the free list.  Split out so or_clear_all() can pay for one grace period for all
 * the rules it drops instead of one each.
 *
 * The caller MUST have raised `dead` and waited a grace period before calling this: the flag is what
 * stops a NEW reader from matching the slot, and the wait is what covers the readers that got past it
 * (they copy what they need out of the entry while they hold rcu_read_lock).  The assertion below
 * states that requirement instead of raising the flag here, because raising it here would silently
 * accept a caller that skipped the wait. */
static void or_release_slot(struct sus_or_entry *e)
{
	WARN_ON_ONCE(!READ_ONCE(e->dead));

	e->target_pathname[0] = '\0';
	e->redirected_pathname[0] = '\0';
	e->target_ino = 0;
	e->target_dev = 0;
	e->target_igen = 0;
	e->target_mnt_id = 0;
	e->redirected_ino = 0;
	e->redirected_dev = 0;
	e->redirected_igen = 0;
	memset(&e->spoofed_kstatfs, 0, sizeof(e->spoofed_kstatfs));
	e->uid_scheme = 0;
	smp_wmb();
	/* Cannot overflow while the accounting is right: only a live slot is pushed, and a live slot is
	 * off the list until it is handed out again.  The check is here so a future path that frees the
	 * same slot twice says so instead of writing past the array - or_free_n sits right after it. */
	WARN_ON_ONCE(or_free_n >= SUS_OR_MAX);
	if (or_free_n < SUS_OR_MAX)
		or_free[or_free_n++] = (unsigned int)(e - or_entries);
}

/* Retire one slot: raise the flag, wait a grace period, then clear and free it.  The wait is the whole
 * point - every reader copies what it needs out of an entry inside its own rcu_read_lock (the
 * name-layer hook copies the target name, the d_path face copies it into the probe's per-instance
 * data, the statfs face copies the snapshot, the maps and fdinfo faces rewrite the formatted line
 * before they leave), so once synchronize_rcu() returns nobody can still be looking at these fields.
 *
 * Called with or_lock held, i.e. from process context: it sleeps, and that is fine because the only
 * callers are the rule-management paths (a /proc write or the supercall).  Nothing else may hold a
 * pointer into the table across a sleep. */
static void or_reclaim_slot(struct sus_or_entry *e)
{
	WRITE_ONCE(e->dead, true);
	smp_wmb();
	synchronize_rcu();
	or_release_slot(e);
}

/* Drop every rule at once - the "clear" command.  One grace period covers all of them: the slots are
 * marked dead first, so a reader inside its rcu section has either already copied what it needs or
 * skips them, and after synchronize_rcu() the fields can be cleared and the slots handed out again.
 * Called with or_lock held (it sleeps), like or_reclaim_slot() - and unlike a loop over or_del() it
 * waits once, not once per rule, and never hands a callee the inside of a table it is clearing. */
static void or_clear_all(void)
{
	unsigned long marked = 0;
	int i;

	BUILD_BUG_ON(SUS_OR_MAX > BITS_PER_LONG);

	for (i = 0; i < nor; i++) {
		if (READ_ONCE(or_entries[i].dead))
			continue;
		WRITE_ONCE(or_entries[i].dead, true);
		marked |= 1UL << i;
	}

	/* The live count drops before the wait, not after: the name-layer hook reads it on every open, and
	 * every rule is already dead at this point - a lookup during the grace period would only resolve a
	 * name to find nothing. */
	WRITE_ONCE(susfs_open_redirect_rules, 0);

	if (marked) {
		smp_wmb();
		synchronize_rcu();
		for (i = 0; i < nor; i++) {
			if (marked & (1UL << i))
				or_release_slot(&or_entries[i]);
		}
	}
}

/* Only the rules whose target path matches are dropped; the reverse entry is not
 * a separate object here, so it goes with the rule by definition. */
static void or_del(const char *target)
{
	struct sus_or_entry *e;

	e = or_find_by_path(target);
	if (!e || READ_ONCE(e->dead))
		return;

	/* The slot goes back to the free list after a grace period, so the fields may be cleared and the
	 * slot handed out again - the pointers this used to leave behind are gone (see or_reclaim_slot()),
	 * and a name that a reader is still looking at is safe because every reader copies it out under
	 * rcu_read_lock. */
	or_reclaim_slot(e);

	if (READ_ONCE(susfs_open_redirect_rules))
		WRITE_ONCE(susfs_open_redirect_rules, READ_ONCE(susfs_open_redirect_rules) - 1);
}

/* Slots are never compacted (that array move was itself part of the race).  The kprobes stay registered for the whole
 * module lifetime: unregistering and re-registering them was observed to leave the hook silently gone after a burst of
 * add/del cycles, while an empty rule table already makes the lookups miss - so staying registered costs nothing. */

static ssize_t or_proc_write(struct file *file, const char __user *buf,
			     size_t len, loff_t *off)
{
	char cmd[640];
	char *argv[8];
	int argc, err;
	long scheme;

	/* Same gate as or_proc_open(): an fd opened before the opener dropped privileges must not become a way in. */
	if (!susfs_control_visible())
		return -ENOENT;

	if (len >= sizeof(cmd))
		len = sizeof(cmd) - 1;
	if (copy_from_user(cmd, buf, len))
		return -EFAULT;
	cmd[len] = 0;

	argc = split_ws(cmd, argv, 8);
	if (argc == 0)
		return len;

	mutex_lock(&or_lock);
	err = -EINVAL;

	if (!strcmp(argv[0], "add_open_redirect") && argc == 4) {
		if (kstrtol(argv[3], 10, &scheme))
			err = -EINVAL;
		else
			err = or_add(argv[1], argv[2], (int)scheme);
	} else if (!strcmp(argv[0], "del") && argc == 2) {
		or_del(argv[1]);
		err = 0;
	} else if (!strcmp(argv[0], "clear")) {
		/* All rules at once: mark every live slot dead, wait ONE grace period for the readers, then
		 * clear them and put them on the free list.  Going through or_del() per rule would wait a
		 * grace period per rule with the rule lock held, and it would hand the inside of the table to
		 * a callee that clears the very slot it was given. */
		or_clear_all();
		err = 0;
	}

	mutex_unlock(&or_lock);

	if (err) {
		pr_warn("open_redirect proc write '%s' -> err %d\n", argv[0], err);
		return err;	/* surface the failure; success keeps returning len */
	}
	return len;
}

/* supercall: CMD_SUSFS_ADD_OPEN_REDIRECT */
void susfs_open_redirect_supercall(void __user **arg)
{
	struct st_susfs_open_redirect info = {0};
	int err;

	if (copy_from_user(&info, (void __user *)*arg, sizeof(info))) {
		info.err = -EFAULT;
		goto out;
	}

	mutex_lock(&or_lock);
	err = or_add(info.target_pathname, info.redirected_pathname,
		     info.uid_scheme);
	mutex_unlock(&or_lock);
	info.err = err;
out:
	/* upstream writes back only ->err for input-type commands */
	if (copy_to_user(&((struct st_susfs_open_redirect __user *)*arg)->err,
			 &info.err, sizeof(info.err)))
		pr_warn("open_redirect supercall copy_to_user failed\n");
}
