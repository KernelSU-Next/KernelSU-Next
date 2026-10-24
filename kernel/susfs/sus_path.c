// SPDX-License-Identifier: GPL-2.0
/*
 * sus_path.c - SUSFS SUS_PATH for the LKM, in two independent layers.
 *
 * Upstream sets AS_FLAGS_SUS_PATH on inode->i_mapping, skips the entry inside
 * filldir64() (fs/readdir.c) and hides it from path-based access by patching
 * fs/namei.c (link_path_walk returns -ENOENT; __lookup_slow/lookup_open redo the
 * lookup with the fake qstr "..5.u.S").  This LKM can touch neither - filldir64 is
 * static and LTO-inlined, namei.c is compiled into the kernel - so it reproduces
 * both effects: the getdents64 buffer is rewritten on return (layer 1, by (ino,
 * name) - a dirent record carries no dev, see sus_path_is_hidden()), and two LSM
 * hooks answer registered inodes with -ENOENT (layer 2:
 * inode_getattr for stat/fstatat/statx, inode_permission for
 * open/exec/chmod/truncate/...).
 *
 * Matching mirrors upstream: exact inode identity (not name substrings), any number
 * of registered paths, hidden wherever that inode is reached ('..', '//', relative
 * paths, symlinks, hard links, /proc/self/root/...), and the upstream gate - app
 * processes only and never a file owned by the caller (sus_path_gate_ok;
 * hide_from_apps=0 disables the gate for a root shell).  A path registered before it
 * exists is kept and hidden once it appears, upstream's CMD_SUSFS_ADD_SUS_PATH_LOOP /
 * LH_SUS_PATH_LOOP behaviour (sus_path_resolve_pending()).
 *
 * Upstream's FUSE_SUPER_MAGIC branch (susfs.c:71-84, :151-166, :195-212) has no
 * equivalent here on purpose - see the note above sus_path_inode_hidden().
 */
#include <linux/module.h>
#include <linux/delay.h>	/* ndelay, for the timing cover */
#include <linux/random.h>	/* prandom_u32_max, for its jitter */
#include <linux/tracepoint.h>
#include <trace/events/syscalls.h>
#include <asm/syscall.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/namei.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/compat.h>
#include <linux/workqueue.h>	/* the pending-resolution retry timer */
#include <linux/mutex.h>	/* serialises the first rule's arming */
#include <linux/limits.h>
#include <linux/cred.h>
#include <linux/atomic.h>
#include <linux/wait.h>
#include <linux/proc_fs.h>	/* proc_create() for /proc/susfs_path */
#include <linux/seq_file.h>	/* single_open()/seq_write() for the same node */
#include <linux/version.h>	/* LINUX_VERSION_CODE: the setxattr hook's first argument */
#include "susfs_abi.h"
#include "susfs_log.h"
#include "susfs.h"	/* susfs_abi_path_ok */
#include "symbol_resolver.h"	/* find_kernel_symbol_exact, for optional compat probes */
#include "ksu_umount_gate.h"	/* susfs_is_current_proc_umounted_app (issue #34) */
#include "ksu_allow_gate.h"	/* the control-node gate is su's own */

#include "lsm_hook.h"
#include "fsnotify_lifetime.h"

/* Bounce buffer for the dirent rewrite: one record at a time, so only a single
 * record - not a whole listing - has to fit; one that does not fit is left
 * unfiltered and the rewrite stops there (counted and logged). */
#define DIRENT_BUF_SIZE 65536
#define SUS_PATH_MAX_ENTRIES 8192
/* Deferred resolution of rules whose path does not exist yet - upstream's
 * CMD_SUSFS_ADD_SUS_PATH_LOOP semantics (sus_path_resolve_pending()): retry every
 * SUS_PATH_PENDING_RETRY_S seconds, stop after SUS_PATH_PENDING_TRIES timer attempts
 * (the rule itself stays registered), at most SUS_PATH_PENDING_BUDGET lookups per pass. */
#define SUS_PATH_PENDING_RETRY_S 2
#define SUS_PATH_PENDING_TRIES 60
#define SUS_PATH_PENDING_BUDGET 128
/* Longest registered path; 256 matches the ABI's target_pathname field. */
#define SUS_PATH_LEN 256

/* Directory-listing syscall numbers.
 *
 * The native one is this build tree's own UAPI macro (<asm/unistd.h> via syscalls.h): syscall
 * numbers are ABI, not per-device - every arm64 kernel from 5.10 to 6.18 numbers getdents64 61,
 * because userspace binaries have the number baked into their SVC sequence.  The AArch32 pair
 * has no macro in an arm64 tree, so it is written out from the 32-bit ARM table
 * (arch/arm/tools/syscall.tbl): getdents 141, getdents64 217.
 *
 * A number on its own still does not identify the call, and the two tables collide by design:
 * arm64's native 141 is getpriority, its 106 delete_module, its 217 add_key, and its
 * 195/196/197 shmctl/shmat/shmdt, while AArch32 gives 141/217 to the two getdents and
 * 195/196/197 to stat64/lstat64/fstat64.  Every match is therefore gated on the caller's ABI -
 * which table entry the call came through now, and the same way in susfs_kstat.c for the stat
 * family.  The two listing numbers themselves live in susfs.h: the table entries that drive this
 * filter (susfs_kstat.c) have to name them too. */

struct linux_dirent64 {
    u64 d_ino;
    s64 d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

/* One registered path, kept for two independent mechanisms: the dirent filter hides
 * the entry (dev+ino+name) on every listing ABI this kernel reaches, and the LSM
 * hooks reject path-based access by the ihold'ed inode pointer - what upstream's
 * AS_FLAGS_SUS_PATH inode flag achieves.  dev is diagnostics only: the dirent filter
 * never sees the listing's fd or superblock. */
struct sus_path_entry {
    struct list_head list;
    struct list_head retiring_list;
    struct super_block *sb;
    /* ihold'ed once resolved, NULL while PENDING (registered for a path that does
     * not exist yet, upstream's _LOOP variant); a pending rule pins nothing. */
    struct inode *inode;
    /* The i_op this inode had before sus_path_iop_arm() pointed it at our copy (NULL
     * when nothing was swapped).  Restored before the ihold() is dropped, so the inode
     * is still alive and the store is safe. */
    const struct inode_operations *orig_iop;
    u64 dev;
    u64 ino;
    char name[NAME_MAX + 1];
    /* The path as registered, stored without a trailing slash
     * (path_len == strlen(path)). */
    char path[SUS_PATH_LEN];
    unsigned int path_len;
    /* Resolution pass that last tried this entry (0 = never): stops one pass from
     * retrying the same rule, without holding a pointer across kern_path(). */
    unsigned int pass;
    /* One of the module's own control nodes (/proc/susfs_*): hidden from every
     * non-root caller, not only apps - see sus_path_entry_gate_any(). */
    bool self_protect;
    /* Pre-relax mode, 0 = this rule did not touch it (sus_path_relax_mode()). */
    umode_t orig_mode;
};

/* Defined next to the readlink stub further down, but called from the pending resolver
 * (which runs inside sus_path_lock) and from both add paths, so declare them first. */
static int sus_path_iop_arm(struct sus_path_entry *e);
static void sus_path_iop_restore(struct sus_path_entry *e);
/* Counted when the i_op copy could not be allocated: the readlink face then stays as it
 * was (a registered plain file answers EINVAL where stat answers ENOENT) instead of
 * failing the registration - the rule still hides everything the other faces cover. */
static atomic_t n_iop_arm_fail = ATOMIC_INIT(0);

/* Let DAC through, so that the LSM layer is the only thing that can refuse: DAC runs before the
 * LSM chain and answers EACCES for a denying mode before any hook could turn it into ENOENT.
 * Relaxing once at registration - the one moment the inode is in hand - costs nothing afterwards,
 * unlike the old per-check DAC probes.  It is not disguised back on the way out: only non-root
 * callers are hidden at all, and root can see the file anyway.  The inode is ihold'ed for the
 * rule's lifetime, so it cannot be evicted and re-read with the original mode. */
static void sus_path_relax_mode(struct sus_path_entry *e)
{
    umode_t mode;

    if (!e->inode)
        return;

    mode = READ_ONCE(e->inode->i_mode);
    if ((mode & (S_IRWXU | S_IRWXG | S_IRWXO)) == 0777)
        return;                 /* already open, or another rule did it */

    e->orig_mode = mode;
    WRITE_ONCE(e->inode->i_mode, (mode & ~(umode_t)(S_IRWXU | S_IRWXG | S_IRWXO)) | 0777);
}

static void sus_path_restore_mode(struct sus_path_entry *e)
{
    if (!e->inode || !e->orig_mode)
        return;

    WRITE_ONCE(e->inode->i_mode, e->orig_mode);
    e->orig_mode = 0;
}

static void sus_path_entry_set_path(struct sus_path_entry *e, const char *path);

static LIST_HEAD(sus_path_list);
static LIST_HEAD(sus_path_retiring);
static DEFINE_SPINLOCK(sus_path_lock);
static DEFINE_MUTEX(sus_path_lifecycle_lock);
static DECLARE_WAIT_QUEUE_HEAD(sus_path_release_wait);
static unsigned int sus_path_count;
static bool sus_path_stopping;
/* BTF-backed filesystem lifetime cleanup is optional on older GKI kernels. */
static bool sus_path_lifetime_watch;

/* Rules waiting for their path to appear, counted so every fast path can bail out at
 * once; guarded by sus_path_lock. */
static atomic_t sus_path_n_pending = ATOMIC_INIT(0);
static unsigned int sus_path_pass_gen;      /* guarded by sus_path_lock */
static atomic_t sus_path_pending_tries = ATOMIC_INIT(0);
/* One resolution pass at a time: the supercall and the retry timer would race on the
 * same entries and on the per-entry pass marker. */
static DEFINE_MUTEX(sus_path_pending_lock);

static void sus_path_pending_work(struct work_struct *w);
static DECLARE_DELAYED_WORK(sus_path_pending_wq, sus_path_pending_work);

static void sus_path_retire_entry(struct sus_path_entry *e, struct list_head *doomed)
{
    list_move_tail(&e->list, doomed);
    sus_path_count--;
    if (e->inode)
        list_add_tail(&e->retiring_list, &sus_path_retiring);
    else
        atomic_dec(&sus_path_n_pending);
}

static bool sus_path_release_one(struct super_block *sb, const char *path,
                                 bool ordinary)
{
    struct sus_path_entry *e, *found = NULL;
    LIST_HEAD(doomed);

    mutex_lock(&sus_path_lifecycle_lock);
    spin_lock(&sus_path_lock);
    list_for_each_entry(e, &sus_path_list, list) {
        if ((sb && e->sb != sb) || (path && strcmp(e->path, path)) ||
            (ordinary && e->self_protect))
            continue;
        found = e;
        sus_path_retire_entry(e, &doomed);
        break;
    }
    spin_unlock(&sus_path_lock);
    if (found)
        sus_path_restore_mode(found);
    mutex_unlock(&sus_path_lifecycle_lock);
    if (!found)
        return false;
    if (found->inode) {
        /* Before the reference is dropped: after iput() the inode may be evicted, and
         * the store would be into freed memory.  See sus_path_iop_restore(). */
        sus_path_iop_restore(found);
        iput(found->inode);
        spin_lock(&sus_path_lock);
        list_del(&found->retiring_list);
        spin_unlock(&sus_path_lock);
        wake_up_all(&sus_path_release_wait);
    }
    kfree(found);
    return true;
}

static bool sus_path_releases_pending(struct super_block *sb)
{
    struct sus_path_entry *e;
    bool pending = false;

    spin_lock(&sus_path_lock);
    list_for_each_entry(e, &sus_path_retiring, retiring_list) {
        if (!sb || e->sb == sb) {
            pending = true;
            break;
        }
    }
    spin_unlock(&sus_path_lock);
    return pending;
}

static void sus_path_sb_release(struct super_block *sb)
{
    unsigned int removed = 0;

    while (sus_path_release_one(sb, NULL, false))
        removed++;
    wait_event(sus_path_release_wait, !sus_path_releases_pending(sb));
    if (removed)
        SUSFS_LOGI("sus_path: filesystem cleanup released %u rule(s)\n", removed);
}

/* legacy/debug: hide a single exact filename everywhere (empty = disabled) */
static char hide_name[NAME_MAX + 1];
module_param_string(hide_name, hide_name, sizeof(hide_name), 0644);

static char *dirent_tmp;
/* Isolation test: with no_extra=1 the LSM replacement and the dirent filter are
 * not registered at all. */
static int no_extra;
module_param(no_extra, int, 0644);

/* Guards dirent_tmp, one global scratch buffer: the probes can fire concurrently, and without this
 * two listings compact into the same buffer and one process can get another directory's entries.
 * sus_path_lock cannot be reused - sus_path_is_hidden() takes it inside the traversal. */
static DEFINE_SPINLOCK(sus_path_buf_lock);

/* Dirent rewrites that stopped early (record too large for the bounce buffer, or a uaccess fault):
 * the counter and the ratelimited log make "the listing was not filtered" visible. */
static atomic_t n_dirent_rewrite_fail = ATOMIC_INIT(0);
/* Chunks that were entirely hidden and needed a placeholder record instead of EOF -
 * see sus_path_filter(). */
static atomic_t n_dirent_all_hidden = ATOMIC_INIT(0);

/* ---- the resolver's own exemption ----
 * The background resolution of a pending rule has to ask the VFS itself (kern_path), and that walk
 * passes through our own hooks once the rule HAS an inode, so it would be answered -ENOENT and
 * re-resolution could never succeed - measured on the device: without the exemption the rule stays
 * pending forever.  Upstream has no such trap (its hiding is a flag on an inode, not a refusal to
 * answer) and walks under override_creds(ksu_cred) from the workqueue (susfs.c:139, reverted at
 * susfs.c:171) - see sus_path_override_creds().  Exactly one task is exempt, the one inside the
 * resolve call, so every other process keeps being hidden for the whole window; it is cleared right
 * after kern_path() returns, on every path. */
static struct task_struct *sus_path_resolver;

static inline bool sus_path_is_resolver(void)
{
    return READ_ONCE(sus_path_resolver) == current;
}

/* Upstream resolves its _LOOP list from a workqueue worker under
 * override_creds(ksu_cred) (susfs.c:139 ... revert_creds() at :171): a worker's creds are not the
 * ones the path should be visible to, and SELinux, DAC and our own gate can tell the difference.
 * Measured on device: a kworker walks as uid 0 in the kernel domain, so
 * kern_path("/data/local/tmp/...") comes back -EACCES and the rule stays pending forever
 * (pend: last-rc=-13).  ksu_cred lives in the `kernelsu` module and
 * find_kernel_symbol_exact() deliberately refuses module symbols ("ignore symbol ... of module
 * ..."), so the creds of whoever registered the rule are saved instead - one reference, released
 * as soon as the last pending rule is gone (sus_path_maybe_drop_caller_cred()) or on unload - and
 * that process can by definition reach the path. */
static const struct cred *sus_path_pending_cred;
static DEFINE_MUTEX(sus_path_cred_lock);
static atomic_t sus_path_used_caller_cred = ATOMIC_INIT(0);

/* Called from the supercall (process context) when a rule is registered pending. */
static void sus_path_save_caller_cred(void)
{
    const struct cred *new = get_cred(current_cred());
    const struct cred *old;

    mutex_lock(&sus_path_cred_lock);
    old = sus_path_pending_cred;
    sus_path_pending_cred = new;
    mutex_unlock(&sus_path_cred_lock);
    if (old)
        put_cred(old);
}

/* Returns the cred revert_creds() wants, and stores in @borrowed the reference THIS call took.
 * get_cred() is what keeps the cred alive for the walk (the table's entry can be replaced
 * meanwhile), and revert_creds() only puts the reference override_creds() itself took - so
 * @borrowed has to be put here too, or every pass leaks one cred and pins the registering
 * process' user_ns/ucounts until the module is unloaded. */
static const struct cred *sus_path_override_creds(const struct cred **borrowed)
{
    const struct cred *cred;

    *borrowed = NULL;
    mutex_lock(&sus_path_cred_lock);
    cred = sus_path_pending_cred;
    if (cred)
        get_cred(cred);
    mutex_unlock(&sus_path_cred_lock);
    if (!cred)
        return NULL;

    *borrowed = cred;
    atomic_inc(&sus_path_used_caller_cred);
    return override_creds(cred);
}

static void sus_path_revert_creds(const struct cred *saved, const struct cred *borrowed)
{
    if (saved)
        revert_creds(saved);
    if (borrowed)
        put_cred(borrowed);
}

static void sus_path_drop_caller_cred(void)
{
    const struct cred *old;

    mutex_lock(&sus_path_cred_lock);
    old = sus_path_pending_cred;
    sus_path_pending_cred = NULL;
    mutex_unlock(&sus_path_cred_lock);
    if (old)
        put_cred(old);
}

/* The saved creds are only needed while a rule is still waiting for its walk.  Release them as
 * soon as the last pending rule is gone - resolved, deleted or cleared - instead of holding one
 * reference (and with it the registering task's user_ns/ucounts) until the module is unloaded.
 * A rule that is STILL pending keeps them, because the next retry (or the next add that re-arms
 * the timer) needs them. */
static void sus_path_maybe_drop_caller_cred(void)
{
    const struct cred *old = NULL;

    if (atomic_read(&sus_path_n_pending))
        return;

    /* Re-checked under the lock: sus_path_save_caller_cred() takes this same lock, so an add that
     * registered a pending rule in between has stored its own cred by the time this section runs.
     * Dropping it there would send the resolver back to walking with the worker's creds - measured
     * to come back -EACCES under /data, which is why the caller's creds are saved at all.
     *
     * One window stays open and heals itself: the list_add and the pending increment happen under
     * sus_path_lock, while sus_path_save_caller_cred() runs after it, so for a few instructions a
     * pending rule can exist without creds.  A retry pass that lands exactly there walks once with
     * the worker's creds (-EACCES, recorded in pend_last_rc), and the next pass one
     * SUS_PATH_PENDING_RETRY_S later has them; the rule stays pending throughout. */
    mutex_lock(&sus_path_cred_lock);
    if (!atomic_read(&sus_path_n_pending)) {
        old = sus_path_pending_cred;
        sus_path_pending_cred = NULL;
    }
    mutex_unlock(&sus_path_cred_lock);
    if (old)
        put_cred(old);
}

/* What the pending machinery did, for hide_list: "still pending" has to be
 * distinguishable from "the timer never ran" and from "the walk keeps failing". */
static atomic_t sus_path_pend_passes = ATOMIC_INIT(0);      /* resolve passes run */
static atomic_t sus_path_pend_ticks = ATOMIC_INIT(0);       /* timer ticks run */
static atomic_t sus_path_pend_walks = ATOMIC_INIT(0);       /* walks that succeeded */
static atomic_t sus_path_pend_lost = ATOMIC_INIT(0);        /* walk ok, rule gone */
static atomic_t sus_path_pend_last_rc = ATOMIC_INIT(0);     /* last walk result */
static atomic_t sus_path_pend_logged_rc = ATOMIC_INIT(1);   /* rc already reported */

/* ---- gates ----
 * Defined up here because every decision layer below asks the same question. */

/* Upstream gates on susfs_is_current_proc_umounted_app() && is_i_uid_not_allowed(): only app
 * processes KernelSU would umount modules for, and never a file owned by the caller.  TIF_PROC_
 * UMOUNTED is a SUSFS-specific thread flag this LKM cannot set, so the first half is KernelSU's
 * own answer asked directly (susfs_is_current_proc_umounted_app(), ksu_umount_gate.h).  Hiding from
 * every `uid >= 10000` - manager and su-granted apps included - was issue #34.
 * hide_from_apps=0 applies the hidden set to every process including root - handy when testing from
 * an adb shell. */
static int hide_from_apps = 1;
module_param(hide_from_apps, int, 0644);

/* UID half of the upstream gate: a PENDING rule has no inode to ask about ownership
 * (sus_path_entry_gate_any()). */
static inline bool sus_path_gate_uid_ok(void)
{
    if (!hide_from_apps)
        return true;
    return susfs_is_current_proc_umounted_app();
}

/* Full upstream gate for the LSM layer: app process, and the file is not owned by the caller
 * (upstream is_i_uid_not_allowed()).  hide_from_apps=0 must bypass the WHOLE gate, ownership check
 * included - otherwise a root-owned file would still be skipped for root (0 != 0 is false) and
 * disabling the gate would silently do nothing for exactly the case it is meant for. */
static inline bool sus_path_gate_ok(struct inode *inode)
{
    if (!hide_from_apps)
        return true;
    if (!susfs_is_current_proc_umounted_app())
        return false;
    return current_uid().val != inode->i_uid.val;
}

/* Per-rule gate.  A self_protect rule is one of this module's own control nodes, which must be
 * invisible to EVERY non-root caller: the ordinary gate needs an app uid, so a probe running as
 * system (1000) or shell (2000) would read /proc/susfs_kstat straight out of the listing - exactly
 * the trace this module exists to avoid.  Root keeps access to manage the module; ordinary rules
 * keep the upstream semantics untouched. */
static inline bool sus_path_entry_gate_inode(const struct sus_path_entry *e,
                                             struct inode *inode)
{
    if (e->self_protect)
        return !susfs_control_visible();
    return sus_path_gate_ok(inode);
}

/* Gate for the layers that match by (ino, name) or by path string rather than by the inode being
 * accessed: the dirent filter and the path matcher.  Applying the uid half alone made them answer
 * differently from the LSM layer for the case upstream's gate is really about - a file owned by the
 * calling app disappeared from the listing while stat()/open() still succeeded, and a listing that
 * omits a file the caller can open is a far louder signal than either behaviour alone.  The rule
 * keeps its inode ihold'ed, so the ownership question is asked of that very inode: no cached uid,
 * and a chown() moves both layers together.  A rule not resolved yet falls back to the uid half. */
static inline bool sus_path_entry_gate_any(const struct sus_path_entry *e)
{
    if (e->self_protect)
        return !susfs_control_visible();
    if (!e->inode)
        return sus_path_gate_uid_ok();
    return sus_path_gate_ok(e->inode);
}

static bool sus_path_is_hidden(u64 ino, const char *name)
{
    struct sus_path_entry *e;
    bool hidden = false;

    /* The one resolving task is never answered "hidden": its own walk would be
     * refused by this very table (see the block above sus_path_resolver). */
    if (sus_path_is_resolver())
        return false;

    /* A dirent is identified by (d_ino, name) and nothing else here - the filter never sees the fd
     * or the superblock, so unlike the LSM layer (the ihold'ed inode pointer, then (dev, i_ino)) it
     * cannot tell one filesystem from another.  The two layers therefore disagree about one object
     * in a narrow case: a same-named entry that reuses the same d_ino on ANOTHER filesystem is
     * dropped from the listing - the rule's dev is not consulted - while its stat/open still work,
     * because the LSM layer rejects that (dev, i_ino) pair.  The collision is not exotic - most
     * filesystems number inodes from low values, so a common basename is enough - but the direction
     * is the safe one: a listing that hides more than the file layer does never leaks a registered
     * name.  The alternative is not free either: matching the name alone would hide unrelated
     * entries, which is exactly why the only name-only mode in this file is the explicit opt-in
     * debug parameter hide_name.  A rule whose inode reports 0 has no identity to match, and
     * d_ino 0 is what some filesystems use for "unknown", so such a rule is hidden by the
     * by-inode layers only (sus_path_supercall()). */
    if (ino) {
        spin_lock(&sus_path_lock);
        list_for_each_entry(e, &sus_path_list, list) {
            if (e->ino && e->ino == ino && !strcmp(e->name, name) &&
                sus_path_entry_gate_any(e)) {
                hidden = true;
                break;
            }
        }
        spin_unlock(&sus_path_lock);
    }

    if (!hidden && hide_name[0])
        hidden = !strcmp(name, hide_name);

    return hidden;
}

/* The same question sus_path_is_hidden() answers, for the pre-emission filter in sus_dirent.c:
 * that layer decides whether to EMIT a record, so it needs the verdict before any byte is
 * written, not after.  Same predicate - and the same isolation switch as
 * sus_path_dirent_filter(): with no_extra=1 this layer is not registered at all (sus_path_init()
 * says so in the log), so a rule a supercall registers afterwards must not start filtering
 * listings while stat/open keep succeeding.  That mismatch is the one the two-layer design exists
 * to avoid, and the switch belongs here rather than at the call site so that both entries into the
 * predicate answer the same way. */
bool sus_path_dirent_hidden(u64 ino, const char *name)
{
    if (READ_ONCE(no_extra))
        return false;

    /* The "no rule, no work" short circuit sus_path_dirent_filter() opens with, for the same
     * reason: without it every record of every listing takes this file's spinlock, even on a
     * device that has no rules at all - and the pre-emission layer calls this once per record,
     * where the post-hoc filter called it once per syscall. */
    if (!READ_ONCE(sus_path_count) && !hide_name[0])
        return false;

    return sus_path_is_hidden(ino, name);
}

/* ---- deferred resolution: upstream's CMD_SUSFS_ADD_SUS_PATH_LOOP ----
 * Upstream does not resolve at add time: susfs_add_sus_path_loop() (susfs.c:99-132) checks for an
 * empty string only and stores the path on LH_SUS_PATH_LOOP; susfs_run_sus_path_loop()
 * (susfs.c:134-172) walks that list with kern_path() later, triggered by susfs_run_extra_works()
 * (susfs.c:1451-1457) from ksu_handle_extra_susfs_work()
 * (KernelSU/10_enable_susfs_for_ksu.patch:1599-1607) as zygote marks an app TIF_PROC_UMOUNTED
 * (patch:1669, patch:1719), and entries are never removed, so every spawn retries all of them.
 * Semantics: "registered now, hidden as soon as the path shows up", no attempt limit, the trigger
 * is an event.
 * With no zygote hook here the rule is registered at once with inode == NULL ("pending"): nothing
 * hides it yet (the dirent filter needs (ino, name), the LSM hooks the inode) but it holds its
 * place, and sus_path_resolve_pending() retries from sus_path_supercall() (every add) and from a
 * bounded retry timer.  The resolving task is exempt from our own hiding and walks with the
 * caller's creds (sus_path_resolver, sus_path_override_creds()).  A rule still missing when the
 * timer gives up hides NOTHING until it resolves; the next add re-arms the timer.
 */

/* Basename of a registered path: what the getdents64 filter compares d_name against,
 * and what the table shows while the inode is unknown.  Stored paths never have a
 * trailing slash, so the text after the last '/' is the whole name. */
static void sus_path_basename(const char *path, char *dst, size_t size)
{
    const char *slash = strrchr(path, '/');

    if (slash && slash[1])
        path = slash + 1;
    strscpy(dst, path, size);
}

static int sus_path_resolve_pending(void)
{
    char path[SUS_PATH_LEN];
    unsigned int gen;
    int budget = SUS_PATH_PENDING_BUDGET;
    int resolved = 0;

    if (READ_ONCE(sus_path_stopping) || !atomic_read(&sus_path_n_pending))
        return 0;
    /* trylock: the supercall must never block behind a pass sleeping in kern_path(). */
    if (!mutex_trylock(&sus_path_pending_lock))
        return 0;
    if (READ_ONCE(sus_path_stopping)) {
        mutex_unlock(&sus_path_pending_lock);
        return 0;
    }
    atomic_inc(&sus_path_pend_passes);

    /* Pass marker.  0 means "never attempted", so generation 0 is skipped. */
    spin_lock(&sus_path_lock);
    gen = ++sus_path_pass_gen;
    if (!gen)
        gen = ++sus_path_pass_gen;
    spin_unlock(&sus_path_lock);

    while (budget-- > 0) {
        struct sus_path_entry *e, *slot;
        struct inode *inode = NULL;
        const struct cred *saved;
        const struct cred *borrowed = NULL;
        struct path p;
        char name[NAME_MAX + 1];
        bool published = false;
        bool have_path = false;
        int rc;

        path[0] = '\0';
        spin_lock(&sus_path_lock);
        slot = NULL;
        list_for_each_entry(e, &sus_path_list, list) {
            if (!e->inode && e->pass != gen) {
                memcpy(path, e->path, e->path_len + 1);
                e->pass = gen;
                slot = e;
                break;
            }
        }
        spin_unlock(&sus_path_lock);
        if (!slot)
            break;              /* every pending rule was attempted */

        name[0] = '\0';

        /* Exempt THIS task only, for the walk's duration, and undo it immediately
         * whatever the walk answers - every other process stays hidden throughout. */
        WRITE_ONCE(sus_path_resolver, current);
        saved = sus_path_override_creds(&borrowed);
        rc = kern_path(path, LOOKUP_FOLLOW, &p);
        have_path = !rc;
        sus_path_revert_creds(saved, borrowed);
        WRITE_ONCE(sus_path_resolver, NULL);

        atomic_set(&sus_path_pend_last_rc, rc);
        if (!rc) {
            atomic_inc(&sus_path_pend_walks);
            inode = d_inode(p.dentry);
            if (inode) {
                strscpy(name, p.dentry->d_name.name, sizeof(name));
                ihold(inode);
            }
        } else if (rc != atomic_read(&sus_path_pend_logged_rc)) {
            /* Report each distinct answer once: a rule that never resolves has to be
             * distinguishable from a timer that never ran, and the rc (-ENOENT,
             * -EACCES, -ENOTDIR) says which it is without logging the hidden path. */
            atomic_set(&sus_path_pend_logged_rc, rc);
            SUSFS_LOGI("sus_path: pending walk rc=%d (%d pending, pass %u)\n",
                    rc, atomic_read(&sus_path_n_pending), gen);
        }

        /* The entry is re-found rather than used across the sleep: the table can be
         * changed while we are away (another add, or module exit tearing it down), so
         * only the path string is carried over - plus a pass marker no other pass can
         * have set on a fresh entry. */
        mutex_lock(&sus_path_lifecycle_lock);
        spin_lock(&sus_path_lock);
        slot = NULL;
        list_for_each_entry(e, &sus_path_list, list) {
            if (!sus_path_stopping && e->pass == gen && !e->inode &&
                !strcmp(e->path, path)) {
                slot = e;
                break;
            }
        }
        if (slot && inode && READ_ONCE(sus_path_lifetime_watch)) {
            spin_unlock(&sus_path_lock);
            rc = susfs_sb_watch_add(inode->i_sb);
            if (rc)
                atomic_set(&sus_path_pend_last_rc, rc);
            spin_lock(&sus_path_lock);
        }
        if (slot && inode && !rc) {
            slot->sb = inode->i_sb;
            slot->dev = (u64)inode->i_sb->s_dev;
            slot->ino = (u64)inode->i_ino;
            strscpy(slot->name, name, sizeof(slot->name));
            slot->inode = inode;
            inode = NULL;               /* the table holds the reference now */
            atomic_dec(&sus_path_n_pending);
            resolved++;
            published = true;
            /* Relax INSIDE the section that owns `slot`.  `slot` is a raw pointer into the
             * table, and sus_path_command()'s del/clear list_del() an entry under this lock
             * but iput()+kfree() it OUTSIDE it and without sus_path_pending_lock, so
             * touching `slot` after the unlock is a use-after-free write into freed memory
             * (and into the freed entry's inode pointer with it).  The inode is ihold'ed
             * here, so relaxing it needs no further lifetime argument. */
            sus_path_relax_mode(slot);
            /* Make do_readlinkat()'s gate true for this inode as well; see
             * sus_path_inode_readlink().  Safe here: slot->inode is ihold'ed, and the
             * copy table is an atomic-allocation spinlock, so nothing sleeps. */
            if (sus_path_iop_arm(slot))
                atomic_inc(&n_iop_arm_fail);
        }
        spin_unlock(&sus_path_lock);
        mutex_unlock(&sus_path_lifecycle_lock);

        if (inode) {
            iput(inode);                /* the rule is gone, or already resolved */
        } else if (!published && !rc) {
            atomic_inc(&sus_path_pend_lost);    /* walk ok, nothing to publish */
        }
        if (have_path)
            path_put(&p);
    }

    if (resolved)
        SUSFS_LOGI("sus_path: resolved %d pending rule(s), %d still unresolved\n",
                resolved, atomic_read(&sus_path_n_pending));

    mutex_unlock(&sus_path_pending_lock);

    /* This pass may have resolved the last pending rule, and nothing else will need the
     * registering process' creds once none is left. */
    sus_path_maybe_drop_caller_cred();
    return resolved;
}

/* A rule was registered for a path that is not there yet: try once right away (an
 * earlier add in the same batch may be what made this rule necessary), then let the
 * timer keep trying.  Called from sus_path_supercall()'s task_work, i.e. process
 * context. */
static void sus_path_pending_arm(void)
{
    if (READ_ONCE(sus_path_stopping) || !atomic_read(&sus_path_n_pending))
        return;

    sus_path_resolve_pending();
    spin_lock(&sus_path_lock);
    if (!sus_path_stopping && atomic_read(&sus_path_n_pending)) {
        atomic_set(&sus_path_pending_tries, 0);
        schedule_delayed_work(&sus_path_pending_wq, SUS_PATH_PENDING_RETRY_S * HZ);
    }
    spin_unlock(&sus_path_lock);
}

/* Retry timer, bounded on purpose: upstream's equivalent runs once per app spawn (a
 * free trigger) while this one costs a periodic work item, and a rule whose path never
 * appears must not keep it alive forever.  A tick that cannot resolve anything is not
 * silent: the walk's own rc is logged once per distinct value by
 * sus_path_resolve_pending(), and hide_list carries the pass/tick/walk counters, so
 * "the timer never ran" and "the walk keeps failing" are told apart without guessing. */
static void sus_path_pending_work(struct work_struct *w)
{
    int resolved;

    if (READ_ONCE(sus_path_stopping))
        return;
    atomic_inc(&sus_path_pend_ticks);
    resolved = sus_path_resolve_pending();

    if (!atomic_read(&sus_path_n_pending))
        return;                 /* every rule has its inode now */

    if (resolved > 0)
        atomic_set(&sus_path_pending_tries, 0);     /* progress: keep trying */

    if (atomic_inc_return(&sus_path_pending_tries) > SUS_PATH_PENDING_TRIES) {
        SUSFS_LOGI("sus_path: %d rule(s) still pending after %d retries (last walk rc=%d) - retry timer stops; they hide nothing until they resolve, the next add tries again\n",
                atomic_read(&sus_path_n_pending), SUS_PATH_PENDING_TRIES,
                atomic_read(&sus_path_pend_last_rc));
        return;
    }
    spin_lock(&sus_path_lock);
    if (!sus_path_stopping && atomic_read(&sus_path_n_pending))
        schedule_delayed_work(&sus_path_pending_wq, SUS_PATH_PENDING_RETRY_S * HZ);
    spin_unlock(&sus_path_lock);
}

/* ---------------------------------------------------------------------------
 * LSM hooks - make path-based access report ENOENT.
 *
 * Upstream patches fs/namei.c (link_path_walk returns -ENOENT; __lookup_slow/lookup_open redo the
 * lookup with the fake qstr "..5.u.S") - impossible for a module, so the two hooks every
 * path-based operation has to pass are replaced: inode_getattr (vfs_getattr() calls
 * security_inode_getattr() before it ever looks at the inode: stat/fstatat/statx) and
 * inode_permission (open/exec/chmod/truncate/chdir/readdir/...).  Matching on the inode pointer
 * covers '//', './', relative paths, symlinks (followed), hard links, bind mounts and
 * /proc/self/root/...; unlink/rename operate on the PARENT's inode, hence the name-based hooks.
 * ------------------------------------------------------------------------- */

/* Every replacement in this file is registered with KSU_LSM_HOOK_INSERT and can only
 * ADD a denial - it can never suppress another LSM.
 *
 * Below 6.12 that is literally an insertion: each replacement owns a struct security_hook_list
 * node of ours, and the kernel's dispatcher reaches that node before any registered LSM, SELinux
 * included.  call_int_hook() returns the first non-zero answer, so on the path where nothing is
 * hidden the replacement returns 0 and the chain continues into SELinux: its decision -
 * including the -ECHILD it answers an RCU walk with in inode_permission - is still what
 * the caller gets.
 *
 * From 6.12 there is no hook list left to insert into, because the members are static calls:
 * ksu_lsm_hook_insert_scall() takes over SELinux's own slot for that member instead and keeps the
 * function it displaced in `hook.original`.  There `return 0` no longer reaches SELinux's decision
 * - the displaced function is not in the chain any more - so it has to be run explicitly, on the
 * not-hidden path and only there: that is SUS_LSM_PASS_ORIG below, whose NULL check is what keeps
 * "nothing was displaced" from jumping to NULL rather than answering "no opinion".  The failure
 * the old shape of this file had to avoid stays the one to avoid here too: losing the original is
 * FAIL OPEN in the worst place - an operation SELinux denies allowed while every rule still
 * looked installed. */
static int sus_path_inode_getattr(const struct path *path);
static int sus_path_inode_permission(struct inode *inode, int mask);
static int sus_path_lsm_readlink(struct dentry *dentry);

/* The signatures MUST match the LSM hook types exactly, and must NOT be __nocfi:
 * this kernel uses kCFI with cross-module checks, so the call site compares type
 * hashes.  A mismatched signature panics, and __nocfi panics just as hard because
 * the function then emits no hash at all.  These assertions turn any mistake into
 * a build failure instead of a reboot.  NOTE: the address-of is required -
 * typeof(fn) is the function type while the hook field is a function pointer. */
#define LSM_HOOK_FN_TYPE(member) typeof(((union security_list_options *)0)->member)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#define SUS_LSM_PASS_ORIG(hook, member, ...)						\
	do {										\
		LSM_HOOK_FN_TYPE(member) __susfs_orig;				\
		/* Load-load fence before the read.  The switch that sent this call		\
		 * here is a plain store of key->func (the publish order in			\
		 * ksu_lsm_hook_insert_scall()), and on a weakly ordered CPU the load	\
		 * of original may be satisfied before the load of key->func that			\
		 * preceded it - which is how this reads NULL on an armed hook and drops	\
		 * SELinux's decision for that call.  The writer-side smp_wmb() closes	\
		 * the propagation half, this closes the reader half; it is one			\
		 * dmb ishld and only on the non-hidden path. */				\
		smp_rmb();								\
		__susfs_orig = (LSM_HOOK_FN_TYPE(member))READ_ONCE((hook).original);	\
		/* Through the trampoline, never directly: a checked indirect call into	\
		 * the kernel panics instead of failing (see SUS_LSM_ORIG_CALL above). */	\
		if (__susfs_orig)							\
			return sus_path_orig_##member(__susfs_orig, __VA_ARGS__);	\
	} while (0)
#else
#define SUS_LSM_PASS_ORIG(hook, member, ...) do { } while (0)
#endif

static_assert(__builtin_types_compatible_p(LSM_HOOK_FN_TYPE(inode_getattr),
					   typeof(&sus_path_inode_getattr)),
	      "inode_getattr hook signature mismatch");
static_assert(__builtin_types_compatible_p(LSM_HOOK_FN_TYPE(inode_permission),
					   typeof(&sus_path_inode_permission)),
	      "inode_permission hook signature mismatch");
static_assert(__builtin_types_compatible_p(LSM_HOOK_FN_TYPE(inode_readlink),
					   typeof(&sus_path_lsm_readlink)),
	      "inode_readlink hook signature mismatch");

/* ---- name-based operations ----
 *
 * unlink/rmdir/rename/link never permission-check the FILE, only the PARENT directory
 * (may_delete/may_create), so inode_permission never sees the target and an app that can write the
 * containing directory can delete or rename a hidden file straight out of hiding.  Each operation has
 * a hook carrying the target's dentry: security_inode_unlink/rmdir(dir, dentry),
 * security_inode_rename(old_dir, old_dentry, new_dir, new_dentry),
 * security_inode_link(old_dentry, dir, new_dentry).  Upstream does this earlier, inside namei, which
 * also covers a parent directory that itself denies the caller; being after DAC is enough for the case
 * that matters here (a writable parent).  The create family (inode_create/mkdir/mknod/symlink) is
 * deliberately not here: the target does not exist yet, so there is no inode to match against. */
static int sus_path_inode_unlink(struct inode *dir, struct dentry *dentry);
static int sus_path_inode_rmdir(struct inode *dir, struct dentry *dentry);
static int sus_path_inode_rename(struct inode *old_dir, struct dentry *old_dentry,
				 struct inode *new_dir, struct dentry *new_dentry);
static int sus_path_inode_link(struct dentry *old_dentry, struct inode *dir,
			       struct dentry *new_dentry);

static_assert(__builtin_types_compatible_p(LSM_HOOK_FN_TYPE(inode_unlink),
					   typeof(&sus_path_inode_unlink)),
	      "inode_unlink hook signature mismatch");
static_assert(__builtin_types_compatible_p(LSM_HOOK_FN_TYPE(inode_rmdir),
					   typeof(&sus_path_inode_rmdir)),
	      "inode_rmdir hook signature mismatch");
static_assert(__builtin_types_compatible_p(LSM_HOOK_FN_TYPE(inode_rename),
					   typeof(&sus_path_inode_rename)),
	      "inode_rename hook signature mismatch");
static_assert(__builtin_types_compatible_p(LSM_HOOK_FN_TYPE(inode_link),
					   typeof(&sus_path_inode_link)),
	      "inode_link hook signature mismatch");

static struct ksu_lsm_hook sus_path_unlink_hook =
	KSU_LSM_HOOK_INSERT(inode_unlink, (void *)sus_path_inode_unlink);

static struct ksu_lsm_hook sus_path_rmdir_hook =
	KSU_LSM_HOOK_INSERT(inode_rmdir, (void *)sus_path_inode_rmdir);

static struct ksu_lsm_hook sus_path_rename_hook =
	KSU_LSM_HOOK_INSERT(inode_rename, (void *)sus_path_inode_rename);

static struct ksu_lsm_hook sus_path_link_hook =
	KSU_LSM_HOOK_INSERT(inode_link, (void *)sus_path_inode_link);

/* ---- metadata and attribute operations ----
 *
 * These syscalls stop before any check we otherwise hook, so a hidden file could still be probed - in
 * the setattr case modified - while answering "permission denied" rather than "no such file".  Each has
 * an LSM hook carrying the dentry (or the path): sb_statfs for statfs/fstatfs; inode_setattr for
 * chmod/chown/truncate/utimes; inode_getxattr/_listxattr and inode_setxattr/_removexattr for the xattr
 * calls; path_notify for inotify_add_watch and fanotify_mark.  inode_setattr is the one that also
 * closes the error-code leak: without it an app got EPERM or EACCES from the owner check, which says
 * the file exists. */
/* ---- the setxattr/removexattr FIRST argument, per kernel version ----
 *
 * These two are the only hooks here whose first parameter is not a dentry/inode/path: it is the
 * id-mapping the syscall came in through, and its type moved twice (upstream
 * include/linux/lsm_hook_defs.h): no first argument before v5.12; `struct user_namespace *mnt_userns`
 * from v5.12 (added with idmapped mounts), replaced by `struct mnt_idmap *idmap` in v6.3 ("fs: add
 * mnt_idmap").
 * The trees this module builds against agree: android12-5.10 declares no first argument,
 * android13-5.15/android14-6.1 declare mnt_userns, android15-6.6 declares idmap.  Three spellings are
 * needed because the macro has to work in a parameter list (with a name), in a function-pointer type
 * (no name) and as the leading call argument - and before 5.12 all three vanish.  The static_asserts
 * below make a wrong branch a build failure, never a silently mis-typed hook (a mismatched signature
 * is a kCFI panic at load time).
 *
 * inode_setattr, the other hook of this pair's family, got the id-mapping later than this gate's
 * v6.3: upstream v6.9 - and no release this module builds for shipped it before 6.12 (6.6 is
 * LTS-frozen and keeps the dentry/iattr pair) - which is why the setattr replacement has its own
 * gate below (SUS_SETATTR_MNT_ID_DECL/ARG, >= 6.12) rather than sharing this one: the version
 * boundary is not the same, and a hook declared with the wrong shape is a kCFI panic at load
 * time, not a warning. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
#define SUS_XATTR_MNT_ID_DECL	struct mnt_idmap *idmap,
#define SUS_XATTR_MNT_ID_TYPE	struct mnt_idmap *,
#define SUS_XATTR_MNT_ID_ARG	idmap,
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
#define SUS_XATTR_MNT_ID_DECL	struct user_namespace *mnt_userns,
#define SUS_XATTR_MNT_ID_TYPE	struct user_namespace *,
#define SUS_XATTR_MNT_ID_ARG	mnt_userns,
#else	/* before idmapped mounts the hook never received an id mapping */
#define SUS_XATTR_MNT_ID_DECL
#define SUS_XATTR_MNT_ID_TYPE
#define SUS_XATTR_MNT_ID_ARG
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#define SUS_SETATTR_MNT_ID_DECL	struct mnt_idmap *idmap,
#define SUS_SETATTR_MNT_ID_ARG	idmap,
#else
#define SUS_SETATTR_MNT_ID_DECL
#define SUS_SETATTR_MNT_ID_ARG
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
/* A hook that TAKEN OVER SELinux's slot still has to run SELinux's decision, which it does by
 * calling the saved original through `hook.original` (SUS_LSM_PASS_ORIG below).  That call
 * goes through these trampolines instead of being made at the call site, and they are
 * __nocfi, because an INSTRUMENTED indirect call into the kernel panics here rather than
 * failing: the type id this module expects and the one the kernel emitted for its own
 * function do not agree.  Measured on a 5.15 device - the first call through a
 * kallsyms-resolved pointer died with
 *
 *     Kernel panic - not syncing: CFI failure (target: kallsyms_on_each_symbol+0x0/0x1e4)
 *
 * `noinline` is not decoration either.  LTO treats the attribute as a property of the
 * function it was written on, so it inlines an unchecked call back into a caller that lacks
 * it and the check reappears THERE - which is how that panic ended up in
 * susfs_sus_mount_supercall() instead of in the function that made the call.
 *
 * One trampoline per hook member, because each has to be called through its own prototype
 * (and the two idmap arguments are version-dependent, hence the DECL/ARG macros); the macro
 * only saves repeating the cast and the two attributes.  tools/cfi_sites.py asserts on the
 * built .ko that no other function in this module carries a checked call. */
#define SUS_LSM_ORIG_CALL(member, ...)						\
	static __nocfi noinline int sus_path_orig_##member(			\
		LSM_HOOK_FN_TYPE(member) fn, __VA_ARGS__)

SUS_LSM_ORIG_CALL(inode_getattr, const struct path *path)
{
	return fn(path);
}

SUS_LSM_ORIG_CALL(inode_permission, struct inode *inode, int mask)
{
	return fn(inode, mask);
}

SUS_LSM_ORIG_CALL(inode_readlink, struct dentry *dentry)
{
	return fn(dentry);
}

SUS_LSM_ORIG_CALL(inode_unlink, struct inode *dir, struct dentry *dentry)
{
	return fn(dir, dentry);
}

SUS_LSM_ORIG_CALL(inode_rmdir, struct inode *dir, struct dentry *dentry)
{
	return fn(dir, dentry);
}

SUS_LSM_ORIG_CALL(inode_rename, struct inode *old_dir, struct dentry *old_dentry,
		  struct inode *new_dir, struct dentry *new_dentry)
{
	return fn(old_dir, old_dentry, new_dir, new_dentry);
}

SUS_LSM_ORIG_CALL(inode_link, struct dentry *old_dentry, struct inode *dir,
		  struct dentry *new_dentry)
{
	return fn(old_dentry, dir, new_dentry);
}

SUS_LSM_ORIG_CALL(sb_statfs, struct dentry *dentry)
{
	return fn(dentry);
}

SUS_LSM_ORIG_CALL(inode_setattr, SUS_SETATTR_MNT_ID_DECL
		  struct dentry *dentry, struct iattr *attr)
{
	return fn(SUS_SETATTR_MNT_ID_ARG dentry, attr);
}

SUS_LSM_ORIG_CALL(inode_getxattr, struct dentry *dentry, const char *name)
{
	return fn(dentry, name);
}

SUS_LSM_ORIG_CALL(inode_listxattr, struct dentry *dentry)
{
	return fn(dentry);
}

SUS_LSM_ORIG_CALL(inode_setxattr, SUS_XATTR_MNT_ID_DECL
		  struct dentry *dentry, const char *name, const void *value,
		  size_t size, int flags)
{
	return fn(SUS_XATTR_MNT_ID_ARG dentry, name, value, size, flags);
}

SUS_LSM_ORIG_CALL(inode_removexattr, SUS_XATTR_MNT_ID_DECL
		  struct dentry *dentry, const char *name)
{
	return fn(SUS_XATTR_MNT_ID_ARG dentry, name);
}

SUS_LSM_ORIG_CALL(path_notify, const struct path *path, u64 mask,
		  unsigned int obj_type)
{
	return fn(path, mask, obj_type);
}
#endif	/* >= 6.12: the slot-takeover path is the only one that calls an original */

static int sus_path_sb_statfs(struct dentry *dentry);
static int sus_path_inode_setattr(SUS_SETATTR_MNT_ID_DECL
				  struct dentry *dentry, struct iattr *attr);
static int sus_path_inode_getxattr(struct dentry *dentry, const char *name);
static int sus_path_inode_listxattr(struct dentry *dentry);
static int sus_path_inode_setxattr(SUS_XATTR_MNT_ID_DECL
				   struct dentry *dentry, const char *name,
				   const void *value, size_t size, int flags);
static int sus_path_inode_removexattr(SUS_XATTR_MNT_ID_DECL
				      struct dentry *dentry, const char *name);
static int sus_path_path_notify(const struct path *path, u64 mask,
				unsigned int obj_type);

static_assert(__builtin_types_compatible_p(LSM_HOOK_FN_TYPE(sb_statfs),
					   typeof(&sus_path_sb_statfs)),
	      "sb_statfs hook signature mismatch");
static_assert(__builtin_types_compatible_p(LSM_HOOK_FN_TYPE(inode_setattr),
					   typeof(&sus_path_inode_setattr)),
	      "inode_setattr hook signature mismatch");
static_assert(__builtin_types_compatible_p(LSM_HOOK_FN_TYPE(inode_getxattr),
					   typeof(&sus_path_inode_getxattr)),
	      "inode_getxattr hook signature mismatch");
static_assert(__builtin_types_compatible_p(LSM_HOOK_FN_TYPE(inode_listxattr),
					   typeof(&sus_path_inode_listxattr)),
	      "inode_listxattr hook signature mismatch");
static_assert(__builtin_types_compatible_p(LSM_HOOK_FN_TYPE(inode_setxattr),
					   typeof(&sus_path_inode_setxattr)),
	      "inode_setxattr hook signature mismatch");
static_assert(__builtin_types_compatible_p(LSM_HOOK_FN_TYPE(inode_removexattr),
					   typeof(&sus_path_inode_removexattr)),
	      "inode_removexattr hook signature mismatch");
static_assert(__builtin_types_compatible_p(LSM_HOOK_FN_TYPE(path_notify),
					   typeof(&sus_path_path_notify)),
	      "path_notify hook signature mismatch");

static struct ksu_lsm_hook sus_path_statfs_hook =
	KSU_LSM_HOOK_INSERT(sb_statfs, (void *)sus_path_sb_statfs);

static struct ksu_lsm_hook sus_path_setattr_hook =
	KSU_LSM_HOOK_INSERT(inode_setattr, (void *)sus_path_inode_setattr);

static struct ksu_lsm_hook sus_path_getxattr_hook =
	KSU_LSM_HOOK_INSERT(inode_getxattr, (void *)sus_path_inode_getxattr);

static struct ksu_lsm_hook sus_path_listxattr_hook =
	KSU_LSM_HOOK_INSERT(inode_listxattr, (void *)sus_path_inode_listxattr);

static struct ksu_lsm_hook sus_path_setxattr_hook =
	KSU_LSM_HOOK_INSERT(inode_setxattr, (void *)sus_path_inode_setxattr);

static struct ksu_lsm_hook sus_path_removexattr_hook =
	KSU_LSM_HOOK_INSERT(inode_removexattr, (void *)sus_path_inode_removexattr);

static struct ksu_lsm_hook sus_path_notify_hook =
	KSU_LSM_HOOK_INSERT(path_notify, (void *)sus_path_path_notify);

static struct ksu_lsm_hook sus_path_getattr_hook =
	KSU_LSM_HOOK_INSERT(inode_getattr, (void *)sus_path_inode_getattr);

static struct ksu_lsm_hook sus_path_perm_hook =
	KSU_LSM_HOOK_INSERT(inode_permission, (void *)sus_path_inode_permission);

static struct ksu_lsm_hook sus_path_readlink_hook =
	KSU_LSM_HOOK_INSERT(inode_readlink, (void *)sus_path_lsm_readlink);

static atomic_t n_enoent_getattr = ATOMIC_INIT(0);
static atomic_t n_enoent_perm = ATOMIC_INIT(0);

/* Store the registered path without a trailing slash, so "path/" and "path" both match
 * "path" and "path/child". */
static void sus_path_entry_set_path(struct sus_path_entry *e, const char *path)
{
    size_t n = strnlen(path, SUS_PATH_LEN - 1);

    while (n > 1 && path[n - 1] == '/')
        n--;
    memcpy(e->path, path, n);
    e->path[n] = '\0';
    e->path_len = (unsigned int)n;
}

/*
 * Inode identity is the whole criterion here: deliberately NO FUSE branch, so do not "port"
 * upstream's (susfs.c:71-84 in add, :151-166 in susfs_run_sus_path_loop(), :195-212 in
 * susfs_is_inode_sus_path()).  It is a no-op: get_fuse_inode() is container_of(inode, struct
 * fuse_inode, inode) (fs/fuse/fuse_i.h) over an inode embedded in that struct and upstream's `inode`
 * comes from d_backing_inode() (include/linux/dcache.h), so it flags fi->inode.i_mapping and
 * inode->i_mapping - the same word twice; only an i_mapping NULL check identical to the generic one
 * above it (susfs.c:65-69) and a log line remain.  Our ihold'ed `struct inode *` is just as
 * object-scoped and cannot be recycled, so FUSE and CONFIG_FUSE_BPF=y passthrough (this kernel's
 * gki_defconfig) need nothing special.
 * The dirent filter is where FUSE costs both implementations something: ilookup(buf->sb, d_ino)
 * upstream and our (d_ino, name) match both fail when a FUSE inode is hashed by nodeid while
 * inode->i_ino is the daemon's attr.ino and d_ino whatever the daemon replied (fs/fuse/inode.c) -
 * upstream then skips the entry unfiltered (patch:1752-1760).  A name-only fallback would hide
 * same-named entries elsewhere in the superblock, so none is added.
 */
/* The key is IDENTITY, not the object: the inode pointer is only a cache in front of it.  A pointer
 * hit cannot be wrong, so it answers immediately; everything else is decided by (dev, i_ino), the
 * same key the mount layer uses for its identity records (s_dev + root inode number), and the
 * slow-path hit counter tells an operator whether the cache is doing its job and, when it is not,
 * that the rule's object is not the object readers get.
 * Why the object alone is not enough (both measured on hardware): after a quick rmmod + insmod a rule
 * can hold an inode no reader ever sees again while a NEW object carries the same (dev, i_ino) - seen
 * as ptr_equal=0 in sus_path_probe with this module's control nodes readable by uid 2000 again; and
 * ihold() keeps an inode alive but NOT hashed, so it can be unhashed while its number is handed out
 * again.  (dev, i_ino) is sound because a rule holds its inode from registration until del / clear /
 * unload: while that inode is the hashed one, its number cannot be reused.
 * One tier more, for this module's own control nodes only: another INSTANCE of the same filesystem
 * keeps the inode number but has its own s_dev (measured, a container's /proc: the same 4026535268
 * with dev 1048754 against the main /proc's 20), so their identity is (filesystem type, i_ino) -
 * procfs numbers come from a global allocator (proc_alloc_inum), so that pair can only be this
 * module's node while it is loaded.  Ordinary rules do not get that tier: across two mounts of one
 * type the same number can stand for two different files. */
static atomic_t n_identity_hits = ATOMIC_INIT(0);

static bool sus_path_inode_hidden(struct inode *inode)
{
    struct sus_path_entry *e;
    bool hidden = false, by_identity = false;
    u64 dev;

    if (!inode || !READ_ONCE(sus_path_count))
        return false;

    /* See sus_path_resolver: the resolving task must not be answered by its own table,
     * or its walk of that very path is refused. */
    if (sus_path_is_resolver())
        return false;

    dev = (u64)inode->i_sb->s_dev;

    spin_lock(&sus_path_lock);
    list_for_each_entry(e, &sus_path_list, list) {
        /* Fast path: the exact object the rule resolved. */
        if (e->inode == inode) {
            if (sus_path_entry_gate_inode(e, inode)) {
                hidden = true;
                break;
            }
            continue;
        }
        /* Pending rules have no identity yet (ino == 0) and are the LSM layer's blind
         * spot by construction: nothing exists at their path to be accessed. */
        if (!e->ino || e->ino != (u64)inode->i_ino)
            continue;
        if (e->dev == dev) {
            by_identity = true;
            if (sus_path_entry_gate_inode(e, inode)) {
                hidden = true;
                break;
            }
            continue;
        }
        /* Another instance of the same filesystem: control nodes only, see above. */
        if (e->self_protect && e->inode &&
            e->inode->i_sb->s_type == inode->i_sb->s_type) {
            by_identity = true;
            if (sus_path_entry_gate_inode(e, inode)) {
                hidden = true;
                break;
            }
        }
    }
    spin_unlock(&sus_path_lock);

    if (by_identity)
        atomic_inc(&n_identity_hits);

    return hidden;
}

/* The same decision, published for the layers that hold the inode the kernel resolved instead
 * of a path they would have to resolve again (susfs_opath.c post-checks the file that an
 * open(..., O_PATH) produced).  The key has to be the inode and never a path string: the
 * caller's path was already resolved kernel-side, under that caller's own root/cwd/mount
 * namespace and RCU walk, so re-resolving it here would be a second lookup - a rename, rmdir
 * or mount in between points it somewhere else, which is the TOCTOU this whole layer exists to
 * avoid - and the string is not even available without reading the caller's registers.
 * One implementation, so the O_PATH answer cannot drift from what
 * inode_getattr/inode_permission answer. */
bool sus_path_inode_hidden_public(struct inode *inode)
{
    return sus_path_inode_hidden(inode);
}

/* Whether any rule is live right now.  The create-family entries run for every
 * mkdirat/symlinkat/mknodat/linkat on the system, so they test this before they resolve a
 * path: an empty table costs one load instead of a lookup, and deleting the last rule takes
 * that cost away again. */
bool sus_path_rules_present(void)
{
    return READ_ONCE(sus_path_count) > 0;
}

static int sus_path_inode_getattr(const struct path *path)
{
    struct inode *inode;

    if (path && path->dentry) {
        inode = d_inode(path->dentry);
        if (sus_path_inode_hidden(inode)) {
            atomic_inc(&n_enoent_getattr);
            return -ENOENT;
        }
    }
    /* Not hidden: no opinion.  Below 6.12 the chain continues into SELinux, whose answer (and its
     * AVC record) is what the caller gets; from 6.12 on SELinux's own slot for this member is the
     * one this hook took over, so its decision is run here instead (SUS_LSM_PASS_ORIG) - same
     * answer, same AVC record, just no longer by returning 0. */
    SUS_LSM_PASS_ORIG(sus_path_getattr_hook, inode_getattr, path);
    return 0;
}

static int sus_path_inode_permission(struct inode *inode, int mask)
{
    if (sus_path_inode_hidden(inode)) {
        atomic_inc(&n_enoent_perm);
        return -ENOENT;
    }
    /* Not hidden: 0, so SELinux still decides.  This matters most here: for an RCU walk
     * SELinux answers -ECHILD and the ref-walk retry in inode_permission() depends on
     * that answer reaching the caller unchanged - which from 6.12 makes the pass-through
     * below mandatory rather than optional, because that answer is no longer produced by
     * somebody else down the chain. */
    SUS_LSM_PASS_ORIG(sus_path_perm_hook, inode_permission, inode, mask);
    return 0;
}

/* Shared counter: the name-based ops answer one question, and the interesting fact is
 * that they fire at all. */
static atomic_t n_enoent_nameop = ATOMIC_INIT(0);

/* A negative dentry has no inode, which is exactly the "not hidden" answer. */
static bool sus_path_dentry_hidden(const struct dentry *dentry)
{
    struct inode *inode;

    if (!dentry)
        return false;
    inode = d_inode(dentry);
    return inode && sus_path_inode_hidden(inode);
}

/* Answer readlinkat here, before vfs_readlink() and SELinux's EACCES. */
static atomic_t n_enoent_readlink_lsm = ATOMIC_INIT(0);
static int sus_path_lsm_readlink(struct dentry *dentry)
{
    if (sus_path_dentry_hidden(dentry)) {
        atomic_inc(&n_enoent_readlink_lsm);
        return -ENOENT;
    }
    SUS_LSM_PASS_ORIG(sus_path_readlink_hook, inode_readlink, dentry);
    return 0;
}

/* ---- readlink: the one face gated on i_op ----
 *
 * do_readlinkat() reaches security_inode_readlink() and vfs_readlink() only when
 *
 *         d_is_symlink(dentry) || inode->i_op->readlink
 *
 * holds.  For a registered PLAIN FILE neither is true, so the caller got -EINVAL -
 * which says "the path is there" - while the stat family answered ENOENT.  That
 * disagreement is the tell.  The LSM readlink hook above answers that face before the
 * i_op readlink body; below 6.12 it sits at the chain head, from 6.12 at SELinux's slot.
 *
 * So the gate itself is what gets changed: a registered inode is pointed at a copy of
 * its i_op whose readlink slot is ours.  That makes the condition true, and the slot
 * then answers exactly what the stat family answers, through the same predicate (and
 * therefore the same per-caller gate); the copy changes only that slot and writes no
 * read-only memory.
 *
 * The copy is shared per original i_op and lives as long as the module, so there is no
 * per-inode lifetime to manage: the inode's field simply reverts to the original (or is
 * freed with the inode) and nothing dangles. */

static atomic_t n_enoent_readlink = ATOMIC_INIT(0);

struct sus_path_iops {
    struct list_head list;
    const struct inode_operations *orig;
    struct inode_operations copy;
};

static LIST_HEAD(sus_path_iops_list);
/* A spinlock, not a mutex: the lookup below runs from readlink(2), which may not sleep,
 * and the arm path below runs from inside sus_path_lock's section during a pending
 * resolution.  Allocation is GFP_ATOMIC for the same reason - it happens once per file
 * system that carries a registered path. */
static DEFINE_SPINLOCK(sus_path_iops_lock);

/* An indirect call into the kernel panics on a CFI mismatch instead of failing, so it
 * carries the same __nocfi noinline treatment as SUS_LSM_ORIG_CALL. */
static __nocfi noinline int sus_path_call_orig_readlink(const struct inode_operations *orig,
							struct dentry *dentry,
							char __user *buf, int buflen)
{
    return orig->readlink(dentry, buf, buflen);
}

/* Find the original i_op behind one of our copies (NULL if it is not ours).  The list
 * is as long as the number of distinct file systems that carry a registered path. */
static const struct inode_operations *sus_path_iops_orig_of(const struct inode_operations *copy)
{
    struct sus_path_iops *fi;
    const struct inode_operations *orig = NULL;
    unsigned long flags;

    if (!copy)
        return NULL;
    spin_lock_irqsave(&sus_path_iops_lock, flags);
    list_for_each_entry(fi, &sus_path_iops_list, list) {
        if (&fi->copy == copy) {
            orig = fi->orig;
            break;
        }
    }
    spin_unlock_irqrestore(&sus_path_iops_lock, flags);
    return orig;
}

static int sus_path_inode_readlink(struct dentry *dentry, char __user *buf, int buflen)
{
    struct inode *inode = d_inode(dentry);
    const struct inode_operations *orig;

    if (sus_path_inode_hidden(inode)) {
        atomic_inc(&n_enoent_readlink);
        return -ENOENT;
    }

    /* Not hidden for this caller: answer the way the file system would.  Reached when a
     * rule was deleted (or its gate closed) while the inode still carries our copy. */
    orig = sus_path_iops_orig_of(inode ? inode->i_op : NULL);
    if (orig && orig->readlink)
        return sus_path_call_orig_readlink(orig, dentry, buf, buflen);
    return -EINVAL;			/* what a plain file gets from the kernel */
}

/* Get the shared copy for an original i_op, creating it once. */
static const struct inode_operations *sus_path_iops_for(const struct inode_operations *orig)
{
    struct sus_path_iops *fi;
    unsigned long flags;

    spin_lock_irqsave(&sus_path_iops_lock, flags);
    list_for_each_entry(fi, &sus_path_iops_list, list) {
        if (fi->orig == orig) {
            spin_unlock_irqrestore(&sus_path_iops_lock, flags);
            return &fi->copy;
        }
    }
    fi = kzalloc(sizeof(*fi), GFP_ATOMIC);
    if (!fi) {
        spin_unlock_irqrestore(&sus_path_iops_lock, flags);
        return NULL;
    }
    fi->orig = orig;
    memcpy(&fi->copy, orig, sizeof(fi->copy));
    fi->copy.readlink = sus_path_inode_readlink;
    list_add(&fi->list, &sus_path_iops_list);
    spin_unlock_irqrestore(&sus_path_iops_lock, flags);
    return &fi->copy;
}

/* Point a resolved entry's inode at our copy.  Caller holds a reference to the inode
 * (e->inode is ihold'ed), so it cannot go away under this store. */
static int sus_path_iop_arm(struct sus_path_entry *e)
{
    const struct inode_operations *copy;

    if (!e->inode || !e->inode->i_op || e->orig_iop)
        return 0;
    if (sus_path_iops_orig_of(e->inode->i_op))		/* already ours */
        return 0;
    copy = sus_path_iops_for(e->inode->i_op);
    if (!copy)
        return -ENOMEM;
    e->orig_iop = e->inode->i_op;
    WRITE_ONCE(e->inode->i_op, copy);
    return 0;
}

/* Put it back.  Must run while the ihold() is still held - see the four iput() sites. */
static void sus_path_iop_restore(struct sus_path_entry *e)
{
    if (!e || !e->inode || !e->orig_iop)
        return;
    WRITE_ONCE(e->inode->i_op, e->orig_iop);
    e->orig_iop = NULL;
}

/* Called from sus_path_exit(): the copies are ours, so they go with the module.  Every
 * inode that pointed at one has already been restored by then (each entry restores
 * before its iput()). */
static void sus_path_iops_free(void)
{
    struct sus_path_iops *fi, *tmp;
    unsigned long flags;

    spin_lock_irqsave(&sus_path_iops_lock, flags);
    list_for_each_entry_safe(fi, tmp, &sus_path_iops_list, list) {
        list_del(&fi->list);
        kfree(fi);
    }
    spin_unlock_irqrestore(&sus_path_iops_lock, flags);
}

static int sus_path_nameop_hit(void)
{
    atomic_inc(&n_enoent_nameop);
    return -ENOENT;
}

static int sus_path_inode_unlink(struct inode *dir, struct dentry *dentry)
{
    if (sus_path_dentry_hidden(dentry))
        return sus_path_nameop_hit();
    SUS_LSM_PASS_ORIG(sus_path_unlink_hook, inode_unlink, dir, dentry);
    return 0;
}

static int sus_path_inode_rmdir(struct inode *dir, struct dentry *dentry)
{
    if (sus_path_dentry_hidden(dentry))
        return sus_path_nameop_hit();
    SUS_LSM_PASS_ORIG(sus_path_rmdir_hook, inode_rmdir, dir, dentry);
    return 0;
}

static int sus_path_inode_rename(struct inode *old_dir, struct dentry *old_dentry,
				 struct inode *new_dir, struct dentry *new_dentry)
{
    /* Both ends matter: moving a hidden file out of hiding, and overwriting a hidden
     * file through its target name. */
    if (sus_path_dentry_hidden(old_dentry) || sus_path_dentry_hidden(new_dentry))
        return sus_path_nameop_hit();
    SUS_LSM_PASS_ORIG(sus_path_rename_hook, inode_rename, old_dir, old_dentry,
                      new_dir, new_dentry);
    return 0;
}

static int sus_path_inode_link(struct dentry *old_dentry, struct inode *dir,
			       struct dentry *new_dentry)
{
    /* A hard link is a second name for the same inode: creating one while the file is
     * hidden leaves the new name unhidden, so it is refused too. */
    if (sus_path_dentry_hidden(old_dentry))
        return sus_path_nameop_hit();
    SUS_LSM_PASS_ORIG(sus_path_link_hook, inode_link, old_dentry, dir, new_dentry);
    return 0;
}

/* Counted apart from the name operations: "the object cannot be probed or changed" and
 * "the name is not usable" fail for different reasons (see the block above). */
static atomic_t n_enoent_meta = ATOMIC_INIT(0);

static int sus_path_meta_hit(void)
{
    atomic_inc(&n_enoent_meta);
    return -ENOENT;
}

static int sus_path_sb_statfs(struct dentry *dentry)
{
    if (sus_path_dentry_hidden(dentry))
        return sus_path_meta_hit();
    SUS_LSM_PASS_ORIG(sus_path_statfs_hook, sb_statfs, dentry);
    return 0;
}

static int sus_path_inode_setattr(SUS_SETATTR_MNT_ID_DECL
				  struct dentry *dentry, struct iattr *attr)
{
    if (sus_path_dentry_hidden(dentry))
        return sus_path_meta_hit();
    SUS_LSM_PASS_ORIG(sus_path_setattr_hook, inode_setattr,
                      SUS_SETATTR_MNT_ID_ARG dentry, attr);
    return 0;
}

static int sus_path_inode_getxattr(struct dentry *dentry, const char *name)
{
    if (sus_path_dentry_hidden(dentry))
        return sus_path_meta_hit();
    SUS_LSM_PASS_ORIG(sus_path_getxattr_hook, inode_getxattr, dentry, name);
    return 0;
}

static int sus_path_inode_listxattr(struct dentry *dentry)
{
    if (sus_path_dentry_hidden(dentry))
        return sus_path_meta_hit();
    SUS_LSM_PASS_ORIG(sus_path_listxattr_hook, inode_listxattr, dentry);
    return 0;
}

static int sus_path_inode_setxattr(SUS_XATTR_MNT_ID_DECL
				   struct dentry *dentry, const char *name,
				   const void *value, size_t size, int flags)
{
    if (sus_path_dentry_hidden(dentry))
        return sus_path_meta_hit();
    SUS_LSM_PASS_ORIG(sus_path_setxattr_hook, inode_setxattr,
                      SUS_XATTR_MNT_ID_ARG dentry, name, value, size, flags);
    return 0;
}

static int sus_path_inode_removexattr(SUS_XATTR_MNT_ID_DECL
				      struct dentry *dentry, const char *name)
{
    if (sus_path_dentry_hidden(dentry))
        return sus_path_meta_hit();
    SUS_LSM_PASS_ORIG(sus_path_removexattr_hook, inode_removexattr,
                      SUS_XATTR_MNT_ID_ARG dentry, name);
    return 0;
}

static int sus_path_path_notify(const struct path *path, u64 mask, unsigned int obj_type)
{
    /* inotify_add_watch and fanotify_mark arrive here; their callers do not run an
     * inode permission check on the target. */
    if (path && sus_path_dentry_hidden(path->dentry))
        return sus_path_meta_hit();
    SUS_LSM_PASS_ORIG(sus_path_notify_hook, path_notify, path, mask, obj_type);
    return 0;
}

/* Every hook below the LSM layer is only worth its cost once something is registered: kprobe
 * entry costs a brk trap per hit.  With no rules there is nothing to answer, so they used to be
 * armed on the first rule and torn down with the module - the "no rules, no cost" effect of nop'ing
 * a patched call site, but through the kernel's own register/unregister paths instead of hand-written
 * text patching.  The LSM hooks are exempt: pointer swaps, already cost-free.
 * Nothing is left to arm that way: the listing filter rides the stat family's tracepoint, and its
 * callback bails out on an empty rule table (and on no_extra) by itself, so this flag now only makes
 * the "hooks armed" line - and the no_extra warning beside it - one-shot (sus_path_hooks_arm()). */
static bool hooks_armed;
/* Secondary LSM hooks that could not be registered: the core two are fatal, these only mean one
 * class of operation is uncovered - reported through hide_list as well as the log. */
static int n_lsm_ext_fail;
static const char *first_lsm_ext_fail;
/* Serialises the first rule's arming: two rules arriving at once (a supercall task_work and a
 * module_init caller) would both pass the hooks_armed check, and the second arm would stack a
 * second layer on top of the first one's. */
static DEFINE_MUTEX(sus_path_arm_lock);

/* ---- the listing filter ----
 *
 * The filter has to run AFTER the kernel has built the directory chain, and no LSM hook can do
 * it: the chain is built inside the filesystem, entry by entry, with no per-entry callback a
 * module can reach.  That leaves the syscall itself, and the hook is the table entry for that
 * number, which the stat family's layer already carries (susfs_kstat.c): the chain is complete
 * once the original returns, and the arguments are the wrapper's own - regs->regs[1] is the
 * buffer, read before the original ran because a syscall's return value lands in regs->regs[0].
 * For a 32-bit caller that register holds the zero-extended user pointer, which is the address to
 * use as is.
 *
 * Which call this is, has to be decided by (number, ABI) - not by the number alone.  Syscall
 * numbers are ABI, not per-device, and the two tables collide by design: arm64's native 141 is
 * getpriority, its 106 delete_module, its 217 add_key and its 195/196/197 shmctl/shmat/shmdt,
 * while AArch32 gives 141/217 to the two getdents (and 195/196/197 to stat64/lstat64/fstat64).
 * Matching the bare number made every native getpriority look like an AArch32 getdents, so the
 * filter parsed a nice value (1..40) as a byte count and the `who` tid as a user buffer -
 * a ratelimited warning per call.  sus_path_dirent_layout_id() is that gate; the stat family in
 * susfs_kstat.c carries its own gate of the same shape (an is_compat_task() branch per number
 * set), not this function.
 *
 * It also removes what made the kretprobe version fragile: a kretprobe binds one symbol name,
 * so it had to guess which of __arm64_sys_getdents64 / __do_sys_getdents64 / __se_sys_getdents64
 * (and the three compat spellings) survives as a symbol - LTO inlines the single-caller ones
 * away - and whether the one that armed takes its arguments through a pt_regs pointer or as
 * ordinary C arguments.  A syscall number and the caller's ABI are both knowable at exit. */

/* Record layouts: both reachable ABIs are NUL-terminated with an explicit d_reclen, so nothing
 * needs the d_namlen/computed-length variant old_readdir would have needed. */
/* The AArch32 pair, from the 32-bit ARM table (arch/arm/tools/syscall.tbl) - an arm64 tree has
 * no macro for them.  Which body each one reaches was read off this kernel's own tables
 * (arch/arm64/include/asm/unistd32.h + kernel/sys32.c, which builds the compat table as
 * `[nr] = __arm64_##sym`): AArch32 217 is mapped to the NATIVE sys_getdents64, so its records
 * are `struct linux_dirent64`; AArch32 141 is the separate compat body, so its records are
 * `struct compat_linux_dirent`.  The native number comes from this build tree's own UAPI
 * (<asm/unistd.h> via syscalls.h): getdents64 is 61 on every arm64 kernel, because userspace
 * has the number baked into its SVC sequence. */
struct sus_dirent64_compat {
    u32 d_ino;
    u32 d_off;
    unsigned short d_reclen;
    char d_name[];
};

enum {
    SUS_DIRENT_L64 = 0,     /* struct linux_dirent64      (native and AArch32 61/217) */
    SUS_DIRENT_COMPAT,      /* struct compat_linux_dirent (AArch32 141)              */
    SUS_DIRENT_N,
};

struct sus_dirent_layout {
    unsigned char id;
    unsigned char ino_size;     /* 8 native, 4 compat */
    unsigned char name_off;     /* offset of d_name */
    unsigned char reclen_off;   /* offset of d_reclen */
    unsigned char align;        /* what d_reclen is rounded up to: 8 native, 4 compat */
    bool type_at_tail;          /* compat has no d_type field: it is the record's last byte */
};

static const struct sus_dirent_layout sus_dirent_l64 = {
    .id = SUS_DIRENT_L64,
    .ino_size = 8,
    .name_off = offsetof(struct linux_dirent64, d_name),
    .reclen_off = offsetof(struct linux_dirent64, d_reclen),
    .align = sizeof(u64),
    .type_at_tail = false,
};

static const struct sus_dirent_layout sus_dirent_compat = {
    .id = SUS_DIRENT_COMPAT,
    .ino_size = 4,
    .name_off = offsetof(struct sus_dirent64_compat, d_name),
    .reclen_off = offsetof(struct sus_dirent64_compat, d_reclen),
    .align = sizeof(u32),
    .type_at_tail = true,
};

/* "Did this ABI reach us at all" is a different claim from "did we hide something": only the pair
 * can tell a dead probe from a working one, and the AArch32 layout is the one a 64-bit test tool
 * cannot exercise. */
static atomic_t n_dirent_calls[SUS_DIRENT_N];

/* Nothing is armed any more, and the first rule no longer has a flag to set: the tracepoint is
 * registered unconditionally with the stat family, and what keeps its callback cheap is the
 * callback's own first checks - no_extra (a real gate, see sus_path_dirent_filter()), ret <= 0,
 * and "no rule and no hide_name" - which all run before the first uaccess.  The per-ABI split of
 * the counters below is what then tells a listing call that never arrived from one that was
 * answered "nothing to filter" and from one that really was filtered. */
static atomic_t n_dirent_no_filter[SUS_DIRENT_N];   /* sus_path_filter() found no buffer/knows the layout not */
static atomic_t n_dirent_syscall_bad[SUS_DIRENT_N]; /* return <= 0: error, or an empty listing  */
static atomic_t n_dirent_buf_null[SUS_DIRENT_N];    /* NULL user buffer: nothing to rewrite      */
static atomic_t n_dirent_no_rule[SUS_DIRENT_N];     /* count==0 and hide_name unset: nothing to hide */

static const char *sus_path_dirent_abi_name(int lay_id)
{
    return lay_id == SUS_DIRENT_COMPAT ? "getdents (AArch32 141)"
                                       : "getdents64 (native 61 + AArch32 217)";
}

static long sus_path_filter(unsigned long buf, long count,
                            const struct sus_dirent_layout *lay);

/* Which layout this syscall denotes for a task running the given ABI, or -1 for "not a listing
 * call" - the gate the note above is about. */
int sus_path_dirent_layout_id(long syscall_nr, bool compat)
{
    if (compat) {
        if (syscall_nr == SUS_NR_GETDENTS_COMPAT)
            return SUS_DIRENT_COMPAT;
        if (syscall_nr == SUS_NR_GETDENTS64_COMPAT)
            return SUS_DIRENT_L64;
        return -1;
    }
    return syscall_nr == __NR_getdents64 ? SUS_DIRENT_L64 : -1;
}

/* Called from the stat layer's entry tail in susfs_kstat.c with the caller's own arguments;
 * lay_id comes from sus_path_dirent_layout_id().  Returns the byte count the caller may parse.
 * A counted call is one that really is a listing that produced bytes: the per-ABI counter is
 * what tells a working path from one that never fires. */
long sus_path_dirent_filter(int lay_id, unsigned long buf, long ret)
{
    /* no_extra is the isolation switch, and it is a real gate here rather than the log line
     * sus_path_hooks_arm() prints.  With the switch on, nothing else of this layer is registered
     * (sus_path_init() returns before the LSM hooks go in), so a rule added later through the
     * supercall - which still bumps sus_path_count - would otherwise start filtering listings while
     * stat/open keep succeeding: a listing that omits a name a caller can open is the one mismatch
     * the two-layer design exists to avoid, and it is not what "the LSM layer and the dirent filter
     * are OFF" claims.  This is also where the switch is cheapest: one READ_ONCE on a listing
     * syscall, before any argument is read, so with no_extra=1 the per-ABI counters stay at zero
     * and /proc/susfs_kstat says the filter really did nothing. */
    if (READ_ONCE(no_extra))
        return ret;

    /* The counters are indexed by this id, so an id that did not come from the classifier is
     * answered with "nothing filtered" instead of being used as an index. */
    if (lay_id < 0 || lay_id >= SUS_DIRENT_N)
        return ret;

    if (ret <= 0) {
        atomic_inc(&n_dirent_syscall_bad[lay_id]);
        return ret;
    }
    return sus_path_filter(buf, ret, lay_id == SUS_DIRENT_COMPAT ? &sus_dirent_compat
                                                                 : &sus_dirent_l64);
}

int sus_path_dirent_stat_line(char *buf, size_t size)
{
    return scnprintf(buf, size,
        "  dirent (via the stat layer's entries): l64=%d compat=%d | no-filter(l64=%d compat=%d) "
        "| bad-ret(l64=%d compat=%d) null-buf(l64=%d compat=%d) "
        "no-rule(l64=%d compat=%d) rewrite-fail=%d all-hidden=%d pending=%d\n",
        atomic_read(&n_dirent_calls[SUS_DIRENT_L64]),
        atomic_read(&n_dirent_calls[SUS_DIRENT_COMPAT]),
        atomic_read(&n_dirent_no_filter[SUS_DIRENT_L64]),
        atomic_read(&n_dirent_no_filter[SUS_DIRENT_COMPAT]),
        atomic_read(&n_dirent_syscall_bad[SUS_DIRENT_L64]),
        atomic_read(&n_dirent_syscall_bad[SUS_DIRENT_COMPAT]),
        atomic_read(&n_dirent_buf_null[SUS_DIRENT_L64]),
        atomic_read(&n_dirent_buf_null[SUS_DIRENT_COMPAT]),
        atomic_read(&n_dirent_no_rule[SUS_DIRENT_L64]),
        atomic_read(&n_dirent_no_rule[SUS_DIRENT_COMPAT]),
        atomic_read(&n_dirent_rewrite_fail),
        atomic_read(&n_dirent_all_hidden),
        atomic_read(&sus_path_n_pending));
}

static void sus_path_hooks_arm(void)
{
    mutex_lock(&sus_path_arm_lock);
    if (hooks_armed || !READ_ONCE(sus_path_count)) {
        mutex_unlock(&sus_path_arm_lock);
        return;
    }

    hooks_armed = true;

    /* Two layers, and neither of them is a syscall entry: the LSM slots (registered with the
     * module above) decide every path-based access by inode - ABI-independent, so 32-bit callers
     * are covered too, and undodgeable through a different spelling, a symlink, a hard link or a
     * bind mount; the stat layer's table entries does the listing filter, which no LSM hook can
     * do (sus_path_dirent_filter()).
     * Nothing else is needed: entry-layer hooks matched the caller's path STRING, which the LSM
     * match already covers more thoroughly, and a hook that cannot fire reads as coverage - so
     * those layers are DELETED, not disabled (see the note above sus_path_init()).
     * no_extra is the isolation switch: no LSM, no dirent filter.
     *
     * The listing filter rides a tracepoint it does not own (the stat family registers it), so a
     * failed registration there would leave this layer silently dead: that is not checked from here
     * any more, and the per-ABI call counters in /proc/susfs_kstat are what answers the same
     * question from the other side - a listing call that never arrives keeps them at zero. */
    if (no_extra)
        pr_warn("sus_path: no_extra - dirent filter off\n");

    SUSFS_LOGI("sus_path: hooks armed (LSM + dirent rewrite on the stat layer's table entries)\n");
    mutex_unlock(&sus_path_arm_lock);
}

/* Rewrite the dirent chain the kernel just produced, dropping the entries whose
 * (d_ino, name) pair is registered; returns the byte count the caller may parse.
 * `lay` selects which ABI's record layout is walked (linux_dirent64 or the AArch32
 * compat_linux_dirent).
 * Records move one at a time through dirent_tmp, from `offset` to `written`, always to an
 * address at or before their own, so nothing unread is overwritten and the listing does not
 * have to fit in the buffer at all (the old code gave up once the 64 KB scratch was full).
 * The return value always describes what is really in the caller's buffer: the compacted
 * length on success (0 if every entry was hidden, `count` if none was); on a uaccess failure
 * the bytes handed back whole, which is still a valid record chain the caller re-reads on its
 * next getdents64; and `count` when nothing was written back, i.e. "nothing was filtered" -
 * the old behaviour returned `count` with a *partially* compacted buffer, telling the caller
 * to parse bytes that were no longer records.
 *
 * Filtering only the prefix is not enough: getdents64 hands the caller a length, not a secret,
 * so a process that ignores that length (or scans the whole buffer for names, which the
 * module's own test tooling does) would still read a dropped name out of the bytes the chain
 * no longer covers.  The tail past the compacted chain - and the tail of the placeholder
 * record's name - is therefore zeroed before returning.  The one path left unscubbed is a
 * scrub that itself fails (uaccess); it is counted and logged, and the chunk is still handed
 * back as the compacted chain, so a caller that honours the returned length never parses the
 * bytes that stayed behind. */
static long sus_path_filter(unsigned long buf, long count,
                            const struct sus_dirent_layout *lay)
{
    long offset = 0;        /* read position in the caller's chain */
    long written = 0;       /* bytes of the compacted chain already handed back */
    unsigned short head_reclen = 0;     /* first record's length, for the placeholder */
    bool failed = false;
    bool counted = false;   /* this chunk was already counted as a rewrite failure */
    char *tmp;
    int id = sus_dirent_l64.id;         /* which ABI's counters to move */

    if (lay == &sus_dirent_compat)
        id = sus_dirent_compat.id;

    atomic_inc(&n_dirent_calls[id]);

    if (!buf) {
        atomic_inc(&n_dirent_buf_null[id]);
        return count;
    }

    if (!READ_ONCE(sus_path_count) && !hide_name[0]) {
        atomic_inc(&n_dirent_no_rule[id]);
        return count;
    }

    spin_lock(&sus_path_buf_lock);

    /* uaccess under a spinlock may not fault: if the page is not resident the copy would
     * sleep here.  Disabled, a faulting copy fails, and every failure path answers "no
     * filtering" rather than guessing. */
    pagefault_disable();

    tmp = dirent_tmp;
    if (!tmp) {
        pagefault_enable();
        spin_unlock(&sus_path_buf_lock);
        atomic_inc(&n_dirent_no_filter[id]);
        return count;
    }

    while (offset < count) {
        unsigned long long ino = 0;
        unsigned short reclen;
        char name[NAME_MAX + 1];
        long nlen;
        bool hide;

        if (copy_from_user(&reclen, (void __user *)(buf + offset + lay->reclen_off),
                           sizeof(reclen))) {
            failed = true;
            break;
        }
        /* d_reclen is filesystem-supplied: bound it before it is used as a copy length, as
         * a step and before the bounce buffer is indexed. */
        if (reclen < lay->name_off + 1 ||
            offset + reclen > count ||
            reclen > DIRENT_BUF_SIZE) {
            failed = true;
            break;
        }

        /* The ino is the only fixed-width, ABI-dependent field: 8 bytes in linux_dirent64, 4 in the
         * AArch32 compat record.  A 32-bit record exists only when the number fit
         * (compat_filldir answers -EOVERFLOW otherwise), so the low 4 bytes are the whole value. */
        if (lay->ino_size == 8) {
            if (copy_from_user(&ino, (void __user *)(buf + offset), sizeof(u64))) {
                failed = true;
                break;
            }
        } else {
            u32 ino32;

            if (copy_from_user(&ino32, (void __user *)(buf + offset), sizeof(ino32))) {
                failed = true;
                break;
            }
            ino = ino32;
        }

        nlen = strnlen_user((void __user *)(buf + offset + lay->name_off),
                            sizeof(name) - 1);
        if (nlen == 0) {            /* no readable NUL in the name field */
            failed = true;
            break;
        }
        if (nlen >= sizeof(name))   /* longer than NAME_MAX: cannot match */
            nlen = sizeof(name) - 1;
        if (nlen > reclen - lay->name_off) {
            failed = true;
            break;
        }
        if (copy_from_user(name, (void __user *)(buf + offset + lay->name_off), nlen)) {
            failed = true;
            break;
        }
        name[nlen] = 0;
        if (!head_reclen)
            head_reclen = reclen;

        hide = sus_path_is_hidden((u64)ino, name);

        if (hide) {
            /* Dropped.  Every record after it moves down by its length, so the
             * remaining records can no longer stay where they are. */
            offset += reclen;
            continue;
        }

        /* A record only needs the bounce buffer once something ahead of it was
         * dropped; until then written == offset and it is already in place. */
        if (written != offset) {
            if (copy_from_user(tmp, (void __user *)(buf + offset), reclen)) {
                failed = true;
                break;
            }
            if (copy_to_user((void __user *)(buf + written), tmp, reclen)) {
                failed = true;
                break;
            }
        }
        written += reclen;
        offset += reclen;
    }

    pagefault_enable();
    spin_unlock(&sus_path_buf_lock);

    if (failed) {
        atomic_inc(&n_dirent_rewrite_fail);
        counted = true;
        pr_warn_ratelimited("sus_path: dirent rewrite stopped at %ld/%ld bytes (returned %ld)\n",
                            offset, count, written ? written : count);
        if (!written)
            return count;       /* nothing was written back: claim no filtering */
    }

        /* Everything in this chunk was hidden, and returning 0 here would be read as
         * end-of-directory: the caller stops and never sees the visible entries of the next
         * chunk.  So one record is left behind as a placeholder - d_ino = 0 with an empty
         * name.  readdir() returns it like any other record (bionic and glibc both step by
         * d_reclen and never look at d_ino), so the caller sees one entry without a name and
         * the next readdir() continues after it - still better than a directory that ends
         * early, and nothing is hidden by relying on a skip neither libc performs. */
    if (!failed && count > 0 && written == 0) {
        char zero_ino[8] = {0};
        char zbyte = 0;         /* the native layout's d_type field */
        /* The smallest record this ABI can express: header, one byte of name, and for the
         * compat layout the type byte that lives in the last one. */
        unsigned short min_reclen = ALIGN(lay->name_off + (lay->type_at_tail ? 2 : 1),
                                          lay->align);

        /* head_reclen is the first record's own length, read above with this ABI's layout; the
         * buffer still holds it untouched because written == 0 means nothing was moved.  The
         * placeholder is only d_ino = 0 plus the empty name: everything else the record held
         * (d_off, d_reclen, d_type and the name's tail - zeroing name[0] alone leaves the rest
         * of "susfs_kstat" sitting there as "usfs_kstat"), and every record behind it, is
         * cleared.  The hidden name has to be gone from the buffer, not merely invisible to a
         * caller that honours the returned length.
         *
         * The scrub runs BEFORE d_ino, d_type and d_reclen are overwritten, so a fault leaves
         * the first record's own header intact - the placeholder is never written onto a chain
         * that failed.  A stopped scrub has zeroed a prefix of [name_off, count), so records
         * behind the fault point can lose their headers too; the `return count` below then
         * claims "nothing was filtered", which such a chain no longer is.  That is a
         * copy_from_user fault inside the caller's own buffer, and it fails towards the
         * kernel's own bytes (the whole chunk is the kernel's output, not ours to leak).
         *
         * d_reclen is rewritten rather than left alone: the hidden entry's own length is its
         * name's length rounded up, and d_type is the hidden file's type, so a parser that
         * ignores d_ino == 0 still read both out of the record this leaves behind.  Shrinking it
         * to min_reclen and clearing the type byte says one entry and nothing about it.  d_off
         * stays as the hidden entry had it, deliberately: that is the cookie telldir() hands
         * back and seekdir() resumes from, and it is the one field the next call needs to be
         * right. */
        if (head_reclen >= min_reclen && head_reclen <= count &&
            !clear_user((void __user *)(buf + lay->name_off), count - lay->name_off) &&
            (lay->type_at_tail ||
             !copy_to_user((void __user *)(buf + lay->name_off - 1), &zbyte, 1)) &&
            !copy_to_user((void __user *)buf, zero_ino, lay->ino_size) &&
            !copy_to_user((void __user *)(buf + lay->reclen_off), &min_reclen,
                          sizeof(min_reclen))) {
            atomic_inc(&n_dirent_all_hidden);
            return min_reclen;
        }
        /* Could not build the placeholder: filtering would be worse than not filtering, because the
         * caller would lose the chunk entirely. */
        atomic_inc(&n_dirent_rewrite_fail);
        return count;
    }

    /* The dropped records are gone from the chain, but their bytes are still where they used to
     * be - past `written` - together with the original copy of the records that were moved down.
     * Whoever ignores the return value, or scans the buffer for names instead of walking it,
     * reads a hidden name (and its d_ino/d_off) straight out of that tail.  Zero it.  This runs
     * with the spinlock released and faults enabled, so a tail whose pages are not resident is
     * still scrubbed rather than left behind.
     *
     * A chain that stopped early (failed) leaves just as much of a dropped record behind as a
     * complete one, so this runs on that path too; the return value stays `written` either way,
     * because that is what still describes the bytes the caller may parse. */
    if (written < count && clear_user((void __user *)(buf + written), count - written)) {
        if (!counted)       /* one failure event, one count */
            atomic_inc(&n_dirent_rewrite_fail);
        pr_warn_ratelimited("sus_path: dirent tail scrub stopped at %ld/%ld bytes (%ld bytes of stale records stay in the buffer)\n",
                            written, count, count - written);
    }

    return written;
}

/* Appending to a sysfs .get buffer is bounded HERE, not at each call site: the kernel hands
 * such a callback ONE page and no length at all (fs/sysfs/file.c: sysfs_kf_seq_show() ->
 * seq_get_buf() + memset(buf, 0, PAGE_SIZE) + ops->show(kobj, priv, buf)), so
 * "n += scnprintf(buf + n, PAGE_SIZE - n, ...)" is a heap overflow waiting for the first
 * listing that fills the page: once n passes PAGE_SIZE the expression PAGE_SIZE - n is a
 * size_t underflow (about 2^64), scnprintf believes it has unlimited room, and read(2) hands
 * the caller whatever followed the page in the heap.  About 50 ordinary rules are enough. */
#define SUS_PATH_LIST_SLACK 400		/* longest line below (two names, 2x20 digits) */
static int sus_path_list_puts(char *buf, int n, bool *trunc, const char *fmt, ...)
{
    va_list args;
    int room, written;

    if (n < 0 || n >= (int)PAGE_SIZE - 1) {
        *trunc = true;
        return n;
    }
    room = (int)PAGE_SIZE - n;
    va_start(args, fmt);
    written = vsnprintf(buf + n, room, fmt, args);
    va_end(args);
    if (written >= room) {
        /* vsnprintf reports what it WOULD have written; room-1 + terminator fit. */
        *trunc = true;
        return (int)PAGE_SIZE - 1;
    }
    return n + written;
}

/* Remove every rule whose registered path is @path (normalised as sus_path_entry_set_path()
 * stores it), undoing what those rules changed; returns how many were removed, so 0 is
 * "nothing was registered under that path".  Process context: sus_path_restore_mode() writes
 * inode->i_mode and iput() can sleep and evict - both outside the spinlock, and no matcher can
 * still be holding an entry (matchers walk the list only while holding the lock). */
int sus_path_del_path(const char *path)
{
    char want[SUS_PATH_LEN];
    int removed = 0;
    int i;

    if (!path || !*path)
        return 0;
    strscpy(want, path, sizeof(want));
    for (i = (int)strlen(want); i > 1 && want[i - 1] == '/'; i--)
        want[i - 1] = '\0';
    while (removed < SUS_PATH_MAX_ENTRIES && sus_path_release_one(NULL, want, false))
        removed++;
    sus_path_maybe_drop_caller_cred();
    return removed;
}

/* View of the registered paths, for verification.  Writable so a rule can be taken back: before
 * this, rules could only be removed by unloading the module, so one mistyped `add_sus_path /data`
 * hid that path machine-wide - and left the target's mode relaxed (sus_path_relax_mode()) for just
 * as long.  Deliberately NOT a new CMD_SUSFS_* command: this node is ours, while the supercall
 * command space has to stay compatible with KernelSU/ksud.
 *
 *   echo clear        > .../hide_list     all rules
 *   echo "del /path"  > .../hide_list     one rule (exact path, trailing / ignored)
 *
 * One command layer for both front ends - /proc/susfs_path and the hide_list parameter - so the two
 * cannot drift apart: add <path> (resolved now, the caller learns the errno), del <path> (restores
 * whatever sus_path_relax_mode() relaxed), clear (every ORDINARY rule).  `clear` and `del` refuse to
 * touch this module's own control nodes (the self_protect rules): those are what makes a non-root
 * caller see ENOENT instead of the control surface at all, and the documented way to expose the
 * nodes is expose_proc=0.  Returns 0 or a negative errno; @removed_out (optional) gets the number of
 * rules dropped. */
static bool sus_path_path_is_ours(const char *path)
{
    struct sus_path_entry *e;
    bool ours = false;

    spin_lock(&sus_path_lock);
    list_for_each_entry(e, &sus_path_list, list) {
        if (e->self_protect && !strcmp(e->path, path)) {
            ours = true;
            break;
        }
    }
    spin_unlock(&sus_path_lock);
    return ours;
}

static int sus_path_command(const char *val, int *removed_out)
{
    char cmd[SUS_PATH_LEN + 16];
    const char *arg;
    int i, removed = 0;

    strscpy(cmd, val, sizeof(cmd));
    /* A shell `echo` leaves a newline behind; trim it (and trailing spaces). */
    for (i = (int)strlen(cmd) - 1; i >= 0 && (cmd[i] == '\n' || cmd[i] == '\r' || cmd[i] == ' '); i--)
        cmd[i] = '\0';

    if (!strcmp(cmd, "clear")) {
        while (removed < SUS_PATH_MAX_ENTRIES && sus_path_release_one(NULL, NULL, true))
            removed++;
    } else if (!strncmp(cmd, "add ", 4) || !strncmp(cmd, "del ", 4)) {
        bool adding = (cmd[0] == 'a');

        arg = cmd + 4;
        while (*arg == ' ')
            arg++;
        if (!*arg)
            return -EINVAL;
        /* Normalise exactly like sus_path_del_path() does, or `del /proc/susfs_kstat/`
         * would slip past the guard below and remove the module's own protection. */
        {
            size_t alen = strlen(arg);

            while (alen > 1 && arg[alen - 1] == '/')
                ((char *)arg)[--alen] = '\0';
        }
        if (!adding && sus_path_path_is_ours(arg))
            return -EPERM;
        if (adding) {
            int rc = sus_path_add_hidden(arg);

            if (rc)
                return rc;
        } else {
            removed = sus_path_del_path(arg);
        }
        SUSFS_LOGI("sus_path: %s %s, %d rule(s) removed, %d left\n",
                adding ? "add" : "del", arg, removed, sus_path_count);
    } else {
        return -EINVAL;
    }

    /* Whatever was removed above is gone for good, `clear` included: if the last pending rule went
     * with it, the registering process' creds are not needed any more either. */
    sus_path_maybe_drop_caller_cred();
    SUSFS_LOGI("sus_path: command written, %d rule(s) removed, %d left\n",
            removed, sus_path_count);
    if (removed_out)
        *removed_out = removed;
    return 0;
}

/* ---- insmod parameters must not touch the guard-protected side --------------------------------
 * module_param_cb()'s setters run during load_module()'s parse_args(), i.e. BEFORE susfs_init() has
 * reached susfs_imports_guard() and before this layer is up.  Applying `hide_list=` or
 * `sus_path_probe=` there reached sus_path_add_hidden_ex() -> kern_path()/ihold() and
 * sus_path_hooks_arm() - imports the guard's table covers (imports_guard.c), so a loader that left one
 * of them at zero jumped to address 0 before anything could refuse the load.  (That table is not the
 * whole import set: ordinary exports such as strscpy are the loader's and the build-time System.map
 * gate's business, so the validation below may use them.)
 *
 * Both setters therefore validate the shape and cache the raw value - the command form is part of
 * hide_list's contract, so it is applied later through the same entry point - and sus_path_init()
 * applies them once the guard has passed.  sus_path_ready marks the point after which they apply live,
 * and sus_path_param_lock keeps the two orders from interleaving: without it a sysfs write landing
 * between "ready = true" and the cached apply would be applied first and then overwritten by the older
 * cached value.
 *
 * One entry of that table is still reached from here on purpose: sus_path_param_lock is taken in
 * parse_args(), and mutex_lock/mutex_unlock are in that table as well - so a loader that left those two
 * at zero would jump to address 0 before susfs_init() could refuse the load.  It is not removable by a
 * local change: the mutex is what orders "cache the value" against "apply the cached value", and the
 * other mutual-exclusion primitives available here (_raw_spin_lock*, __rcu_read_*) are in that table
 * too.  Closing it needs a lock-free publication of the cached value (store, then re-check the ready
 * flag and apply live), which is a change of its own. */
static char hide_list_pending[SUS_PATH_LEN + 16];
static char sus_path_probe_pending[SUS_PATH_LEN];
static bool hide_list_given;
static bool sus_path_probe_given;
static bool sus_path_ready;
static DEFINE_MUTEX(sus_path_param_lock);

/* hide_list's grammar: `clear`, `add <path>`, `del <path>` (nothing else - a bare value is refused, so
 * a typo cannot silently replace the list).  Validation only: the command itself adds/removes rules
 * and arms the hooks, which is why it runs at init. */
static int sus_path_command_check(const char *val)
{
	char cmd[SUS_PATH_LEN + 16];
	const char *arg;
	size_t i;

	if (strlen(val) >= sizeof(cmd))
		return -EINVAL;
	strscpy(cmd, val, sizeof(cmd));
	for (i = strlen(cmd); i > 0 && (cmd[i - 1] == '\n' || cmd[i - 1] == '\r' || cmd[i - 1] == ' '); i--)
		cmd[i - 1] = '\0';

	if (!strcmp(cmd, "clear"))
		return 0;
	if (strncmp(cmd, "add ", 4) && strncmp(cmd, "del ", 4))
		return -EINVAL;
	arg = cmd + 4;
	while (*arg == ' ')
		arg++;
	return *arg ? 0 : -EINVAL;
}

static int sus_path_store_list(const char *val, const struct kernel_param *kp)
{
	int rc;

	mutex_lock(&sus_path_param_lock);
	if (sus_path_ready) {
		/* Same judgement as the pre-init path: a bad or over-long command is refused instead of being
		 * silently truncated by sus_path_command(). */
		rc = sus_path_command_check(val);
		mutex_unlock(&sus_path_param_lock);
		return rc ? rc : sus_path_command(val, NULL);
	}

	/* parse_args(): validate and cache only - see hide_list_pending. */
	rc = sus_path_command_check(val);
	if (!rc) {
		strscpy(hide_list_pending, val, sizeof(hide_list_pending));
		hide_list_given = true;
	}
	mutex_unlock(&sus_path_param_lock);
	return rc;
}

/* The listing, into a caller-supplied buffer; shared by /proc/susfs_path and hide_list so the
 * two views cannot say different things.  Returns the bytes written, clamped to the buffer. */
static int sus_path_format_list(char *buf, size_t size)
{
    struct sus_path_entry *e;
    bool trunc = false;
    int n = 0;

    n = sus_path_list_puts(buf, n, &trunc,
                   "hide_from_apps=%d  enoent: getattr=%d perm=%d nameop=%d meta=%d\n",
                   hide_from_apps, atomic_read(&n_enoent_getattr),
                   atomic_read(&n_enoent_perm), atomic_read(&n_enoent_nameop),
                   atomic_read(&n_enoent_meta));
    /* The readlink face answers in the i_op slot or, before it, in the LSM hook; report
     * both, plus whether every i_op copy could be allocated. */
    n = sus_path_list_puts(buf, n, &trunc,
                   "readlink: enoent=%d lsm_enoent=%d armfail=%d\n",
                   atomic_read(&n_enoent_readlink),
                   atomic_read(&n_enoent_readlink_lsm),
                   atomic_read(&n_iop_arm_fail));
    n = sus_path_list_puts(buf, n, &trunc,
                   "dirent: pending=%d (counters: /proc/susfs_kstat)\n",
                   atomic_read(&sus_path_n_pending));
    if (n_lsm_ext_fail)
        n = sus_path_list_puts(buf, n, &trunc,
                       "lsm: %d secondary hook(s) FAILED (first: %s) - that operation is not covered\n",
                       n_lsm_ext_fail, first_lsm_ext_fail);
    /* A normal, expected number rather than an alarm: how often a lookup had to fall through
     * to (dev, ino) because the rule's object was not the one being accessed.  Printed only
     * when non-zero; a steadily growing value is the interesting case. */
    if (atomic_read(&n_identity_hits))
        n = sus_path_list_puts(buf, n, &trunc,
                       "identity: %d hit(s) where the inode pointer did not match and (dev,ino) or (fs type,ino) answered instead\n",
                       atomic_read(&n_identity_hits));
    /* passes/ticks == 0 means the retry never ran; walks > 0 with pending > 0 means the walk
     * kept failing (last-rc says how); lost > 0 means a walk succeeded with no rule to
     * publish it. */
    n = sus_path_list_puts(buf, n, &trunc,
                   "pend: passes=%d ticks=%d walks=%d lost=%d last-rc=%d caller-cred=%d\n",
                   atomic_read(&sus_path_pend_passes),
                   atomic_read(&sus_path_pend_ticks),
                   atomic_read(&sus_path_pend_walks),
                   atomic_read(&sus_path_pend_lost),
                   atomic_read(&sus_path_pend_last_rc),
                   (int)(sus_path_pending_cred != NULL));

    spin_lock(&sus_path_lock);
    list_for_each_entry(e, &sus_path_list, list) {
        if (n > (int)PAGE_SIZE - SUS_PATH_LIST_SLACK) {
            trunc = true;
            break;
        }
        n = sus_path_list_puts(buf, n, &trunc,
                       "path=%s  dev=%llu ino=%llu name=%s%s%s\n",
                       e->path[0] ? e->path : "(?)", e->dev, e->ino, e->name,
                       e->self_protect ? "  [ours: clear/del refuse it]" : "",
                       e->inode ? "" : "  (pending: no inode yet)");
    }
    spin_unlock(&sus_path_lock);

    if (!sus_path_count)
        n = sus_path_list_puts(buf, n, &trunc, "(no paths registered)\n");
    if (trunc)
        n = sus_path_list_puts(buf, n, &trunc,
                       "(truncated: the table holds more rules than one page; the rules are intact)\n");
    return n < (int)size ? n : (int)size - 1;
}

static int sus_path_show_list(char *buf, const struct kernel_param *kp)
{
    return sus_path_format_list(buf, PAGE_SIZE);
}

/* ---- /proc/susfs_path: the same table and the same command layer as hide_list ----
 * cat it for the listing; add <path> / del <path> / clear as above.  Same contract as the other
 * control nodes: mode 0777 so DAC does not answer EACCES first, root-only through the uid check
 * in open() AND write() (an fd opened before privileges were dropped is not a way in either),
 * and registered in the self-protected set so everyone else gets ENOENT. */
static struct proc_dir_entry *sus_path_node_entry;

static int sus_path_proc_show(struct seq_file *m, void *v)
{
    char *buf = kvmalloc(PAGE_SIZE, GFP_KERNEL);
    int n;

    if (!buf)
        return -ENOMEM;
    n = sus_path_format_list(buf, PAGE_SIZE);
    if (n > 0)
        seq_write(m, buf, (size_t)n);
    kvfree(buf);
    return 0;
}

static int sus_path_proc_open(struct inode *inode, struct file *file)
{
    /* 0777 node + this check: a restrictive mode would answer EACCES (advertising that the
     * node exists) before sus_path could answer ENOENT. */
    if (!susfs_control_visible())
        return -ENOENT;
    return single_open(file, sus_path_proc_show, NULL);
}

static ssize_t sus_path_proc_write(struct file *file, const char __user *buf,
                                   size_t len, loff_t *off)
{
    char cmd[SUS_PATH_LEN + 16];
    int rc;

    /* Same reason as the open check: an fd opened before the process dropped
     * privileges must not become a way in. */
    if (!susfs_control_visible())
        return -ENOENT;
    if (len == 0)
        return 0;
    if (len >= sizeof(cmd))
        return -EINVAL;
    if (copy_from_user(cmd, buf, len))
        return -EFAULT;
    cmd[len] = '\0';

    rc = sus_path_command(cmd, NULL);
    if (rc)
        return rc;
    return len;	/* success reports the count, like every other node */
}

static const struct proc_ops sus_path_proc_ops = {
    .proc_open = sus_path_proc_open,
    .proc_read = seq_read,
    .proc_write = sus_path_proc_write,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static const struct kernel_param_ops sus_path_list_ops = {
    .get = sus_path_show_list,
    .set = sus_path_store_list,
};
/* 0600, not 0444: this listing names every hidden path, so an app must not be able to read it -
 * that would hand a detector the exact answer it looks for.  The write bit is what makes a
 * mistaken rule removable (see sus_path_store_list). */
module_param_cb(hide_list, &sus_path_list_ops, NULL, 0600);

/* ---- diagnostic: why does a registered rule not match? ----
 * echo /proc/susfs_kstat > .../parameters/sus_path_probe, then cat it: resolves the path as
 * sus_path_add_hidden_ex() does and reports the identity a reader would see next to the rule meant
 * to match it - the one question the counters cannot answer, which otherwise needs a kernel
 * debugger.  Process context only (kern_path sleeps); 0600 for the same reason hide_list is. */
static char sus_path_probe_report[640];

static int sus_path_probe_apply(const char *val)
{
    struct sus_path_entry *e;
    struct path p;
    struct inode *inode;
    char path[SUS_PATH_LEN];
    int i, rc, n, room;

    strscpy(path, val, sizeof(path));
    /* A shell `echo` leaves a newline behind; trim it (and trailing spaces). */
    for (i = (int)strlen(path) - 1; i >= 0 && (path[i] == '\n' || path[i] == '\r' || path[i] == ' '); i--)
        path[i] = '\0';
    if (!path[0])
        return -EINVAL;

    rc = kern_path(path, LOOKUP_FOLLOW, &p);
    if (rc) {
        scnprintf(sus_path_probe_report, sizeof(sus_path_probe_report),
              "path=%s: kern_path failed rc=%d\n", path, rc);
        return 0;
    }
    inode = d_inode(p.dentry);
    if (!inode) {
        path_put(&p);
        scnprintf(sus_path_probe_report, sizeof(sus_path_probe_report),
              "path=%s: no inode (negative dentry)\n", path);
        return 0;
    }

    n = scnprintf(sus_path_probe_report, sizeof(sus_path_probe_report),
              "path=%s\n  resolved: inode=%px dev=%llu ino=%llu uid=%u in_hidden_set=%d\n",
              path, inode, (unsigned long long)inode->i_sb->s_dev,
              (unsigned long long)inode->i_ino, current_uid().val,
              (int)sus_path_inode_hidden(inode));

    spin_lock(&sus_path_lock);
    list_for_each_entry(e, &sus_path_list, list) {
        if (e->dev != (u64)inode->i_sb->s_dev || e->ino != (u64)inode->i_ino)
            continue;
        room = (int)sizeof(sus_path_probe_report) - n;
        if (room < 200)
            break;
        i = scnprintf(sus_path_probe_report + n, room,
                  "  rule: inode=%px name=%s self_protect=%d ptr_equal=%d gate_inode=%d gate_any=%d\n",
                  e->inode, e->name, (int)e->self_protect,
                  (int)(e->inode == inode), (int)sus_path_entry_gate_inode(e, inode),
                  (int)sus_path_entry_gate_any(e));
        if (i >= room)
            break;
        n += i;
    }
    spin_unlock(&sus_path_lock);
    path_put(&p);
    return 0;
}

static int sus_path_probe_set(const char *val, const struct kernel_param *kp)
{
    mutex_lock(&sus_path_param_lock);
    if (sus_path_ready) {
        mutex_unlock(&sus_path_param_lock);
        return sus_path_probe_apply(val);
    }

    /* parse_args(): cache only - see hide_list_pending (the probe reads a path, nothing here does). */
    strscpy(sus_path_probe_pending, val, sizeof(sus_path_probe_pending));
    sus_path_probe_given = true;
    mutex_unlock(&sus_path_param_lock);
    return 0;
}

static int sus_path_probe_get(char *buf, const struct kernel_param *kp)
{
    return scnprintf(buf, PAGE_SIZE, "%s",
             sus_path_probe_report[0] ? sus_path_probe_report : "(no path probed yet)\n");
}

static const struct kernel_param_ops sus_path_probe_ops = {
    .get = sus_path_probe_get,
    .set = sus_path_probe_set,
};
module_param_cb(sus_path_probe, &sus_path_probe_ops, NULL, 0600);

/* Add a path to the hidden set from kernel code, bypassing the supercall; used by susfs_init() to
 * self-hide the /proc control nodes.  Same entry shape and ihold discipline as
 * sus_path_supercall(): the inode pointer is what the LSM layer matches on and it must outlive
 * path_put() below. */
static int sus_path_publish_entry(struct sus_path_entry *e, bool merge_pending)
{
    struct sus_path_entry *cur;
    int rc = 0;

    mutex_lock(&sus_path_lifecycle_lock);
    if (READ_ONCE(sus_path_stopping)) {
        mutex_unlock(&sus_path_lifecycle_lock);
        return -ESHUTDOWN;
    }
    if (e->inode && READ_ONCE(sus_path_lifetime_watch)) {
        rc = susfs_sb_watch_add(e->sb);
        if (rc) {
            mutex_unlock(&sus_path_lifecycle_lock);
            return rc;
        }
    }
    spin_lock(&sus_path_lock);
    list_for_each_entry(cur, &sus_path_list, list) {
        if ((e->inode && cur->inode == e->inode) ||
            (!e->inode && !cur->inode && !strcmp(cur->path, e->path))) {
            rc = 1;
            goto out;
        }
        if (merge_pending && e->inode && !cur->inode &&
            !strcmp(cur->path, e->path)) {
            cur->sb = e->sb;
            cur->dev = e->dev;
            cur->ino = e->ino;
            strscpy(cur->name, e->name, sizeof(cur->name));
            /* e was armed before publish: its inode points at our i_op copy and e->orig_iop
             * holds the original.  The copy now belongs to cur, so the original must move with
             * it - otherwise cur->orig_iop stays NULL, the restore on retire is a no-op, and
             * the inode keeps a pointer into this module after it is unloaded. */
            cur->orig_iop = e->orig_iop;
            e->orig_iop = NULL;
            cur->inode = e->inode;
            e->inode = NULL;
            atomic_dec(&sus_path_n_pending);
            sus_path_relax_mode(cur);
            rc = 1;
            goto out;
        }
    }
    if (sus_path_count >= SUS_PATH_MAX_ENTRIES) {
        rc = -ENOSPC;
        goto out;
    }
    list_add_tail(&e->list, &sus_path_list);
    sus_path_count++;
    if (e->inode)
        sus_path_relax_mode(e);
    else
        atomic_inc(&sus_path_n_pending);
out:
    spin_unlock(&sus_path_lock);
    mutex_unlock(&sus_path_lifecycle_lock);
    return rc;
}

static int sus_path_add_hidden_ex(const char *path, bool self_protect)
{
    struct path p;
    struct inode *inode;
    struct sus_path_entry *e;
    int rc;

    rc = kern_path(path, LOOKUP_FOLLOW, &p);
    if (rc)
        return rc;
    inode = d_inode(p.dentry);
    if (!inode) {
        path_put(&p);
        return -ENOENT;
    }
    e = kzalloc(sizeof(*e), GFP_KERNEL);
    if (!e) {
        path_put(&p);
        return -ENOMEM;
    }
    e->sb = inode->i_sb;
    e->dev = (u64)inode->i_sb->s_dev;
    e->ino = (u64)inode->i_ino;
    e->inode = inode;
    e->self_protect = self_protect;
    ihold(inode);
    if (sus_path_iop_arm(e))
        atomic_inc(&n_iop_arm_fail);
    strscpy(e->name, p.dentry->d_name.name, sizeof(e->name));
    sus_path_entry_set_path(e, path);
    INIT_LIST_HEAD(&e->list);
    rc = sus_path_publish_entry(e, false);
    if (rc) {
        sus_path_iop_restore(e);
        iput(e->inode);
        kfree(e);
    }
    path_put(&p);
    if (rc < 0)
        return rc;
    sus_path_hooks_arm();
    return 0;
}

/* Register one of the module's own control nodes: same table, flagged so the gate hides it from
 * every non-root caller rather than only from apps (sus_path_entry_gate_any()). */
int sus_path_add_self_hidden(const char *path)
{
	return sus_path_add_hidden_ex(path, true);
}

int sus_path_add_hidden(const char *path)
{
	return sus_path_add_hidden_ex(path, false);
}

/* Whether the path-based layer actually installed: both hooks must be patched, or stat and open
 * would disagree with each other. */
bool sus_path_lsm_active(void)
{
	return sus_path_getattr_hook.entry && sus_path_perm_hook.entry;
}

int sus_path_init(void)
{
    int rc;

    WRITE_ONCE(sus_path_stopping, false);
    WRITE_ONCE(sus_path_lifetime_watch, false);
    rc = susfs_sb_watch_init(sus_path_sb_release);
    if (rc == -EOPNOTSUPP || rc == -ENOSYS) {
        pr_warn("sus_path: fsnotify lifetime cleanup unavailable (%d); using legacy inode holds\n",
                rc);
    } else if (rc) {
        pr_err("sus_path: filesystem cleanup initialization failed %d - refusing inode holds\n", rc);
        return rc;
    } else {
        WRITE_ONCE(sus_path_lifetime_watch, true);
    }

    /* kvmalloc, not kmalloc: 64 KB of physically contiguous order-4 memory is not available on a
     * phone that has been up for a while (measured: MemFree 394 MB, and kmalloc_order failed with a
     * WARN in its call trace), while vmalloc memory is just as usable here - the buffer is only
     * touched from the getdents64 filter, which never faults on it.  A failure is not fatal: listings
     * then go unfiltered and the other layers still come up. */
    dirent_tmp = kvmalloc(DIRENT_BUF_SIZE, GFP_KERNEL);
    if (!dirent_tmp)
        pr_warn("sus_path: dirent scratch buffer unavailable, listings will not be filtered\n");

    /* The dirent rewrite rides the stat layer's table entries (registered with the stat family),
     * not a probe of its own, and nothing has to be armed for it on the first rule: what keeps that
     * callback cheap while there is nothing to filter is the rule-count check inside
     * sus_path_filter() (and the no_extra gate in sus_path_dirent_filter()).  The scratch buffer
     * above is what the rewrite itself needs, so it is allocated here rather than at first use. */
    SUSFS_LOGI("sus_path: dirent rewrite rides the stat layer's table entries (no probe of its own)\n");

    if (no_extra) {
        mutex_lock(&sus_path_param_lock);
        if (hide_list_given || sus_path_probe_given)
            pr_warn("sus_path: no_extra=1 - the hide_list/sus_path_probe value given at insmod is dropped (the LSM layer is off)\n");
        sus_path_ready = true;	/* later writes behave exactly as before this change */
        mutex_unlock(&sus_path_param_lock);
        SUSFS_LOGI("sus_path: no_extra=1 - the LSM layer and the dirent filter are OFF (isolation test)\n");
        return 0;
    }

    /* LSM hooks: reject path-based access to registered inodes outright.  These two ARE the
     * mechanism - a registered path is hidden only because this layer answers ENOENT.  A failure
     * here used to be a warning with a zero return, so add_sus_path() reported success while
     * nothing was hidden; loading now fails instead. */
    rc = ksu_register_lsm_hook(&sus_path_getattr_hook);
    if (rc) {
        pr_err("sus_path: getattr hook failed %d - nothing would be hidden, refusing to load\n", rc);
        /* The scratch buffer is already allocated; the caller is about to fail the load, and a
         * vmalloc'd buffer is not part of the module's own memory, so nobody else would free it. */
        kvfree(dirent_tmp);
        dirent_tmp = NULL;
        return rc;
    }
    SUSFS_LOGI("sus_path: getattr hook inserted ahead of the chain (node %px)\n",
            (void *)sus_path_getattr_hook.entry);

    rc = ksu_register_lsm_hook(&sus_path_perm_hook);
    if (rc) {
        pr_err("sus_path: perm hook failed %d - nothing would be hidden, refusing to load\n", rc);
        ksu_unregister_lsm_hook(&sus_path_getattr_hook);
        kvfree(dirent_tmp);
        dirent_tmp = NULL;
        return rc;
    }
    SUSFS_LOGI("sus_path: perm hook inserted ahead of the chain (node %px)\n",
            (void *)sus_path_perm_hook.entry);

    /* Name-based and metadata operations: without these an app that can write the parent directory
     * can delete or rename a hidden file, and a hidden file can still be probed or modified through
     * statfs/xattr/inotify - or answered with EPERM/EACCES, which says it exists. */
    {
        struct ksu_lsm_hook *extra[] = {
            &sus_path_unlink_hook, &sus_path_rmdir_hook,
            &sus_path_rename_hook, &sus_path_link_hook,
            &sus_path_statfs_hook, &sus_path_setattr_hook,
            &sus_path_getxattr_hook, &sus_path_listxattr_hook,
            &sus_path_setxattr_hook, &sus_path_removexattr_hook,
            &sus_path_notify_hook,
            &sus_path_readlink_hook,
        };
        int i;

        for (i = 0; i < (int)ARRAY_SIZE(extra); i++) {
            rc = ksu_register_lsm_hook(extra[i]);
            if (rc) {
                /* Not fatal - the core two hooks are up, so a hidden path is still hidden -
                 * but one class of operation is NOT covered, so it is counted and named in
                 * hide_list rather than only logged. */
                if (!n_lsm_ext_fail)
                    first_lsm_ext_fail = extra[i]->head_name;
                n_lsm_ext_fail++;
                pr_warn("sus_path: %s hook failed %d - that operation will not be covered\n",
                        extra[i]->head_name, rc);
            } else {
                SUSFS_LOGI("sus_path: %s hook inserted ahead of the chain (node %px)\n",
                        extra[i]->head_name, (void *)extra[i]->entry);
            }
        }
        if (n_lsm_ext_fail)
            pr_warn("sus_path: %d/%d secondary hooks failed (first: %s)\n",
                    n_lsm_ext_fail, (int)ARRAY_SIZE(extra), first_lsm_ext_fail);
    }

    /* The DAC probes, the syscall-entry kprobes and the path-string probes are DELETED, not
     * merely disabled: the mode relax in sus_path_relax_mode() covers the case where DAC
     * answers EACCES before any LSM hook runs, and a hook that cannot fire reads as coverage.
     * One of them also caused an FPAC panic, which is why they are not restored in
     * their former shape. */

    /* The control node goes up LAST, and only when the layer that hides it is really installed
     * (susfs_control_node_allowed()): a 0777 world-writable node without the thing that answers
     * ENOENT for it must not exist.  It has to be created before susfs_self_hide_nodes() runs,
     * which is why it is here and not in susfs_main.c - that list is registered after every
     * layer's own init, so paths resolve. */
    if (susfs_control_node_allowed()) {
        sus_path_node_entry = proc_create("susfs_path", 0777, NULL, &sus_path_proc_ops);
        if (!sus_path_node_entry)
            pr_warn("sus_path: proc_create(susfs_path) failed - the listing stays reachable through the hide_list parameter\n");
    } else {
        SUSFS_LOGI("sus_path: /proc/susfs_path not created (expose_proc=%d lsm=%d)\n",
                (int)susfs_expose_proc, (int)sus_path_lsm_active());
    }

    /* Apply what the insmod parameters handed over (see hide_list_pending): the import guard has passed
     * and this layer is up, so this is the first moment they can run for real.  From here on both
     * setters apply live.  The lock is what makes "cached first, later writes after" hold: a write
     * landing between ready = true and this apply would otherwise be overwritten by the older value
     * (hide_list is append-only, but hide_modules' commit is not - same pattern in both layers). */
    mutex_lock(&sus_path_param_lock);
    sus_path_ready = true;
    if (hide_list_given) {
        rc = sus_path_command(hide_list_pending, NULL);
        if (rc)
            pr_warn("sus_path: applying hide_list=\"%s\" from insmod failed %d - that rule is not in effect\n",
                    hide_list_pending, rc);
    }
    if (sus_path_probe_given) {
        rc = sus_path_probe_apply(sus_path_probe_pending);
        if (rc)
            pr_warn("sus_path: sus_path_probe=\"%s\" from insmod failed %d\n", sus_path_probe_pending, rc);
    }
    mutex_unlock(&sus_path_param_lock);
    return 0;
}

void sus_path_exit(void)
{
    mutex_lock(&sus_path_lifecycle_lock);
    spin_lock(&sus_path_lock);
    WRITE_ONCE(sus_path_stopping, true);
    spin_unlock(&sus_path_lock);
    mutex_unlock(&sus_path_lifecycle_lock);

    /* Our own node goes down first, while the LSM layer that hides it is still armed: the
     * reverse order would expose a 0777 control surface for the duration of the unload. */
    if (sus_path_node_entry) {
        proc_remove(sus_path_node_entry);
        sus_path_node_entry = NULL;
    }

    /* Unregister the hooks FIRST: after this nothing can match, so the entries (and their inode
     * references) can be torn down safely.  The dirent side needs no teardown of its own: it has
     * no flag to clear and no entry of its own - the entries it rides belong to the stat family,
     * so they are restored there, not here.
     *
     * ORDER DEPENDENCY, and it is now the only thing that makes that safe: susfs_layers_down() tears
     * the layer table down in reverse, and the stat layer sits AFTER this one in susfs_layers[], so
     * its susfs_fp_remove() calls + susfs_fp_drain() run before this function.  Reordering the table
     * would leave the wrappers reachable while this layer is being taken apart - the rule table they
     * read is emptied further down, and there is nothing in between any more (the old shape of this
     * file cleared a dirent_armed flag here for exactly that window). */

    /* The retry timer must be off and no resolution pass may be in flight while the table is
     * emptied below: a pass re-finds its entry under the lock and never frees anything, but it
     * may not run past the teardown either.  The pass uses trylock, so this cannot deadlock. */
    cancel_delayed_work_sync(&sus_path_pending_wq);
    mutex_lock(&sus_path_pending_lock);
    mutex_unlock(&sus_path_pending_lock);
    /* No walk can be in flight now, so the borrowed creds are ours to release. */
    sus_path_drop_caller_cred();
    if (sus_path_perm_hook.entry)
        ksu_unregister_lsm_hook(&sus_path_perm_hook);
    {
        struct ksu_lsm_hook *extra[] = {
            &sus_path_unlink_hook, &sus_path_rmdir_hook,
            &sus_path_rename_hook, &sus_path_link_hook,
            &sus_path_statfs_hook, &sus_path_setattr_hook,
            &sus_path_getxattr_hook, &sus_path_listxattr_hook,
            &sus_path_setxattr_hook, &sus_path_removexattr_hook,
            &sus_path_notify_hook,
            &sus_path_readlink_hook,
        };
        int i;

        for (i = 0; i < (int)ARRAY_SIZE(extra); i++) {
            if (extra[i]->entry)
                ksu_unregister_lsm_hook(extra[i]);
        }
    }
    if (sus_path_getattr_hook.entry)
        ksu_unregister_lsm_hook(&sus_path_getattr_hook);

    kvfree(dirent_tmp);
    dirent_tmp = NULL;

    while (sus_path_release_one(NULL, NULL, false))
        ;
    wait_event(sus_path_release_wait, !sus_path_releases_pending(NULL));
    /* Every entry restored its inode's i_op before dropping the reference (see
     * sus_path_iop_restore()), so nothing points at these copies any more; the copies
     * themselves are ours and go with the module. */
    sus_path_iops_free();
    susfs_sb_watch_exit();
    WRITE_ONCE(sus_path_lifetime_watch, false);
}

/* supercall: CMD_SUSFS_ADD_SUS_PATH (0x55550) / CMD_SUSFS_ADD_SUS_PATH_LOOP (0x55553)
 * Upstream keeps the two apart: susfs_add_sus_path() needs the path to exist and the lookup
 * error IS the command's answer (susfs.c:58-62), while susfs_add_sus_path_loop() checks for an
 * empty string only, stores the path on LH_SUS_PATH_LOOP and resolves it later (susfs.c:99-132,
 * susfs_run_sus_path_loop() susfs.c:134-172).  The dispatcher therefore says which command
 * arrived: treating both as pending made the plain command answer 0 for a path that does not
 * exist, so a caller other than the stock tool (which realpath()s first) believed a rule was
 * installed that upstream would have rejected.
 * A pending rule is not dead weight: it holds its place in the table, so the path is hidden from
 * the moment the background walk resolves its inode; what the pending state delays is every
 * layer - the LSM hooks (by inode) and the dirent filter ((ino, name)) - because none of them can
 * match an inode that does not exist yet. */
void sus_path_supercall(unsigned int cmd, void __user **arg)
{
    struct st_susfs_sus_path info = {0};
    struct sus_path_entry *e;
    struct path path;
    struct inode *inode = NULL;
    bool have_path = false;
    bool pending_ok = (cmd == CMD_SUSFS_ADD_SUS_PATH_LOOP);
    int rc;

    if (copy_from_user(&info, (void __user *)*arg, sizeof(info))) {
        info.err = -EFAULT;
        goto out;
    }
    if (!info.target_pathname[0]) {
        info.err = -EINVAL;
        goto out;
    }
    if (!susfs_abi_path_ok(info.target_pathname, sizeof(info.target_pathname))) {
        info.err = -ENAMETOOLONG;
        goto out;
    }
    if (READ_ONCE(sus_path_stopping)) {
        info.err = -ESHUTDOWN;
        goto out;
    }
    rc = kern_path(info.target_pathname, LOOKUP_FOLLOW, &path);
    if (!rc) {
        have_path = true;
        inode = d_inode(path.dentry);
        if (!inode)
            rc = -ENOENT;
    }
    if (rc && (rc != -ENOENT || !pending_ok)) {
        info.err = rc;
        goto out;
    }
    e = kzalloc(sizeof(*e), GFP_KERNEL);
    if (!e) {
        info.err = -ENOMEM;
        goto out;
    }
    if (inode) {
        e->sb = inode->i_sb;
        e->dev = (u64)inode->i_sb->s_dev;
        e->ino = (u64)inode->i_ino;
        e->inode = inode;
        ihold(inode);
        if (sus_path_iop_arm(e))
            atomic_inc(&n_iop_arm_fail);
        strscpy(e->name, path.dentry->d_name.name, sizeof(e->name));
    }
    sus_path_entry_set_path(e, info.target_pathname);
    if (!inode)
        sus_path_basename(e->path, e->name, sizeof(e->name));
    INIT_LIST_HEAD(&e->list);
    rc = sus_path_publish_entry(e, true);
    if (rc) {
        if (e->inode) {
            sus_path_iop_restore(e);
            iput(e->inode);
        }
        kfree(e);
    }
    if (rc < 0) {
        info.err = rc;
        goto out;
    }
    if (inode && !inode->i_ino)
        pr_warn("sus_path: '%s' reports ino 0 - hidden by inode, but a directory listing cannot be filtered for it\n",
                info.target_pathname);
    if (have_path) {
        path_put(&path);
        have_path = false;
    }
    if (!dirent_tmp) {
        dirent_tmp = kvmalloc(DIRENT_BUF_SIZE, GFP_KERNEL);
        if (!dirent_tmp)
            pr_warn("sus_path: dirent scratch buffer unavailable, listings remain unfiltered\n");
    }
    sus_path_hooks_arm();
    if (!inode) {
        sus_path_save_caller_cred();
        sus_path_pending_arm();
    }
    info.err = 0;
out:
    if (have_path)
        path_put(&path);
    sus_path_maybe_drop_caller_cred();
    if (copy_to_user(&((struct st_susfs_sus_path __user *)*arg)->err,
                     &info.err, sizeof(info.err)))
        pr_warn("sus_path supercall copy_to_user failed\n");
}
