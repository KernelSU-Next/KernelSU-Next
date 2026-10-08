// SPDX-License-Identifier: GPL-2.0
/*
 * sus_mount.c - hide KSU mounts from /proc/mounts and /proc/mountinfo.
 *
 * Upstream SUSFS skips a mount line when r->mnt_id >= DEFAULT_KSU_MNT_ID, and
 * (patch:1561-1585) installs those show functions only for NON-ksu domains
 * (!susfs_is_current_ksu_domain() in mounts_open()/mountinfo_open()/mountstats_open()),
 * so the su/ksu domain keeps seeing its own mounts (zygisk in post-fs-data and ksud
 * break otherwise).  The stock show functions are static but sit behind proc_ops function
 * pointers, so they are NOT LTO-inlined and stay kprobe-able (verified in kallsyms); the
 * LKM hooks all three with a kprobe pre_handler and returns early (regs->pc = x30) for a
 * KSU-range id, under the same domain gate.  struct mount / real_mount() come from the
 * private fs/mount.h (its includes are all public headers, so -I$(srctree)/fs is enough).
 *
 * The LKM hooks all three show functions with a kprobe pre_handler and returns
 * early (regs->pc = x30) when the mount id is in the KSU range - but only for
 * non-su/ksu processes, mirroring upstream's domain gate: the su domain must
 * keep seeing its own mounts or su tooling (zygisk in post-fs-data, ksud)
 * breaks.  struct mount / real_mount() come from the private fs/mount.h (its
 * includes are all public headers, so -I$(srctree)/fs is enough).
 *
 * HOW THE ID RANGE IS PRODUCED (this is the part that used to be missing):
 * upstream gets KSU mounts into that range by ADDING an allocator
 * (susfs_alloc_non_unshare_ksu_vfsmnt(), which calls
 * ida_alloc_min(&mnt_id_ida, DEFAULT_KSU_MNT_ID, GFP_KERNEL), patch:676-693) and
 * SWAPPING THE CALL SITES in vfs_create_mount()/clone_mnt() (patch:764, 800);
 * mnt_alloc_id() itself is never patched.  The LKM does not patch kernel text
 * at all, so its stock allocator (plain ida_alloc, smallest free id) keeps
 * handing out small ids - measured on device: 91..39693, so the old fixed 2e9
 * threshold could never match and the feature was 100% OFF.
 *
 * A mount is one of ours when mnt_devname contains "/data/adb/" (KSU/module bind mounts)
 * or the mount POINT - d_path() of {mnt, mnt_root}, the path show_mountinfo() prints - is
 * under /data/adb (catches meta-overlayfs, whose source is /dev/block/loopNN).  Marked ids
 * are never restored on disable, matching upstream, where an id is assigned once at mount
 * time and stays for the mount's lifetime.
 *
 * The new id is a REAL id taken from the kernel's own mnt_id_ida:
 *   ida_alloc_range(&mnt_id_ida, DEFAULT_KSU_MNT_ID, INT_MAX - 1, GFP_KERNEL)
 * NOT an invented number.  That is not a style choice.  mnt_free_id()
 * (fs/namespace.c:136-139) unconditionally calls ida_free(&mnt_id_ida,
 * mnt->mnt_id) when the mount finally goes away, so a hand-made id makes
 * lib/idr.c:523-525 fire WARN(1, "ida_free called for id=%d which is not
 * allocated.") on umount - and a WARN plus stack dump in dmesg is exactly the
 * kind of trace a root detector greps for, i.e. worse than the feature doing
 * nothing.  Upstream is under the same constraint and short-circuits the free
 * for its fake ids (patch:582-588); mnt_free_id() is static + LTO-inlined on
 * this kernel, so we cannot patch it - owning the id for real is the fix.
 *
 * Degradation: if mnt_id_ida / ida_alloc_range / ida_free cannot ALL be
 * resolved, nothing is marked at all.  min_mnt_id then stays false, the feature
 * does nothing, and every missing symbol is named in a pr_warn.  Falling back to
 * a self-made id is deliberately NOT an option (see the WARN above).
 *
 * A mount is recognised by either of:
 *   - mnt_devname contains "/data/adb/" (KSU/module bind mounts, whose source is
 *     the file under /data/adb/modules/...);
 *   - the mount POINT - d_path() of {mnt, mnt_root}, i.e. the same path
 *     show_mountinfo() prints - is under /data/adb (catches meta-overlayfs, whose
 *     source is a loop device: "/dev/block/loop48
 *     /data/adb/modules/meta-overlayfs/mnt ext4 rw,...").
 * Marked ids are never restored on disable, matching upstream, where an id is
 * assigned once at mount time and stays for the mount's lifetime.
 *
 * ---- the namespace that matters, and why identity is remembered as well
 *
 * KernelSU mounts module content into the ZYGOTE's mount namespace, because that
 * is the namespace every app is forked into.  The same filesystem is therefore a
 * different mount OBJECT there, with an id of its own - measured on this device:
 * /data/adb/modules/meta-overlayfs/mnt is id 2000000000 in the init namespace and
 * id 1111 in the zygote's.  Marking is what creates the big id, so it could only
 * cover the namespaces that existed when it ran; the id test alone then hid the
 * line in one view and left it in the app's own mount table - the one a checker
 * actually reads.
 *
 * The fix is identity, not a wider scan: sweeping every namespace from the task
 * list would mean walking mount tables that other tasks are mounting into and
 * unmounting from, under the protocol fs/mount.h documents for that
 * (namespace_sem for read plus ns_lock).  Below 6.12 only the ns_lock half of it
 * is reachable from a module, because namespace_sem is static in fs/namespace.c -
 * a foreign namespace mounts and unmounts while we walk it, which is exactly the
 * class of thing that takes a device down.  From 6.12 the tree is guarded by
 * namespace_sem instead and its ADDRESS is resolved at load time, so a walk is
 * possible - but only of the one namespace nobody can be in yet, the copy
 * copy_mnt_ns() has just built for the calling task (see the clone section
 * below).  Everything else stays identity based: the mount's IDENTITY is
 * remembered by the scan that already runs safely in the caller's namespace:
 *
 *   - superblock device + root dentry (both belong to the filesystem, so the same
 *     filesystem mounted in the zygote is matched), plus a path-shaped source
 *     string for equality;
 *   - the hide hooks accept identity as well as a KSU-range id, and hiding an
 *     identity-matched mount learns its id into the same id -> shown-id mapping, so
 *     fdinfo/statx keep naming a mount line the caller can still see.
 *
 * Known blind spots of the scan:
 *   - overlayfs mounts that KernelSU places on /system have d_path "/system"
 *     and mnt_devname "overlay", so they are NOT recognised; only intercepting
 *     vfs_create_mount()/clone_mnt() by su domain (upstream's group (1)) would
 *     catch those;
 *   - only the mount namespace of the process that enables the feature is
 *     scanned; a mount namespace cloned later (copy_mnt_ns()/unshare()) gets
 *     fresh small ids from the stock allocator (upstream handles that in
 *     clone_mnt() with CL_COPY_MNT_NS, patch:789-810).  It is not left
 *     uncovered: the copy_mnt_ns() probe walks the namespace it has just built
 *     and learns those ids (sus_mount_learn_ns() below 6.12,
 *     sus_mount_learn_ns_walk() from 6.12 on), while the mount LINES in it are
 *     hidden by identity - which is what the note above this list is about.
 */
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/stat.h>     /* struct statx (stx_mnt_id) */
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/nsproxy.h>
#include <linux/percpu.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/mm.h>		/* kvmalloc()/kvfree() for the keep-list staging buffers */
#include <linux/string.h>
#include <linux/list.h>
#include <linux/idr.h>      /* struct ida + ida_alloc_range()/ida_free() prototypes */
#include <linux/err.h>
#include <linux/errno.h>    /* -ENOSYS/-ENOENT/-ENOMEM used by the scan result */
#include <linux/dcache.h>   /* d_path() - called through a resolved symbol */
#include <linux/limits.h>   /* PATH_MAX, INT_MAX (via vdso/limits.h) */
#include <linux/security.h> /* security_secctx_to_secid() */
#include <linux/proc_fs.h>  /* proc_create() for /proc/susfs_hide_mounts */
#include <linux/version.h>  /* LINUX_VERSION_CODE / KERNEL_VERSION: nothing else here
                             * pulled version.h in, and -Werror=undef turned the
                             * version gates below into hard errors when it was missing */
#include <linux/rbtree.h>   /* >= 6.12: ns->mounts is an rb-tree, not a list */
#include <linux/rwsem.h>    /* >= 6.12: namespace_sem is a struct rw_semaphore */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#include <linux/workqueue.h> /* >= 6.12: the deferred walk of a cloned namespace */
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
#include <linux/xarray.h>   /* >= 6.18: struct xarray + XA_LIMIT()/xa_lock() for mnt_id_xa */
#endif
#include "mount.h"      /* fs/mount.h: struct mount + struct mnt_namespace + real_mount() */
#include "symbol_resolver.h"
#include "susfs_abi.h"
#include "susfs_log.h"
#include "susfs.h"	/* module-wide declarations */
#include "ksu_allow_gate.h"	/* the control-node gate is su's own */
#include "susfs_kprobe.h"

/* ---- the "which mounts are ours" prefix list: state ----
 * Up here, not next to the control surface further down, because mount_stat() between
 * the two reports the list length and the rescan count - and declaring it below its
 * first use would be a compile error, not a style issue. */
/* 64 prefixes (the list used to stop at 8, which a container setup outgrows quickly).  One write to
 * the control node may carry a whole list - it is bounded by SUS_MOUNT_KEEP_CMDLINE, i.e. 8352 bytes,
 * which is 64 entries of a 128-byte prefix - and the static cost of the two buffers below is roughly
 * 16 KB. */
#define SUS_MOUNT_KEEP_MAX 64
#define SUS_MOUNT_KEEP_LEN 128
#define SUS_MOUNT_KEEP_CMDLINE (SUS_MOUNT_KEEP_MAX * (SUS_MOUNT_KEEP_LEN + 2) + 32)

/* Room for one more full prefix line PLUS the "there is more" notice, in the sysfs read path.
 * Short of that, the notice is what gets cut: 64 prefixes of 127 characters stopped the loop 96 bytes
 * from the end, so the "N more; cat /proc/susfs_hide_mounts for all" text was itself truncated
 * mid-path and the final newline went with it - the read that needs the notice cannot print it.  One
 * more entry than strictly needed is deliberate: the notice's own length is not fixed. */
#define SUS_MOUNT_KEEP_TRUNC_RESERVE (SUS_MOUNT_KEEP_LEN + 64)

static char mount_keep[SUS_MOUNT_KEEP_MAX][SUS_MOUNT_KEEP_LEN];
static int n_mount_keep;
static DEFINE_SPINLOCK(mount_keep_lock);
static const char *const mount_keep_default = "/data/adb/";
static atomic_t n_keep_rescans = ATOMIC_INIT(0);

/* How many of the three mount-table hooks (show_vfsstat / show_mountinfo /
 * show_vfsmnt) are armed; 3 is healthy.  1 or 2 means the feature hides fewer files
 * than it says, which must be visible rather than inferred (see
 * sus_mount_register()).  Declared up here because mount_stat() reports it. */
static int n_show_probes;

#define DEFAULT_KSU_MNT_ID 2000000000ULL

/* min_mnt_id is a raw ulong tunable: 0/1 would make EVERY mount line match the
 * threshold, hiding all of /proc/mounts, /proc/<pid>/mountinfo and mountstats from
 * every process, su included.  Below this it is clamped back to DEFAULT_KSU_MNT_ID. */
#define SUS_MOUNT_MIN_SANE_MNT_ID 1000

/* Hard bound on the mount-namespace walk.  A concurrent umount_tree() does
 * list_del_init() on the entry it removes, which makes that node point at
 * itself, so an unbounded list_for_each() can spin forever if it lands on it. */
#define SUS_MOUNT_MAX_SCAN 65536

/* At/above this is "already a KSU-range id" (upstream's own test, patch:807).  The
 * marking side uses this constant and not the min_mnt_id tunable: a raised tunable
 * would re-mark an already-marked mount and allocate a SECOND id, leaking the first
 * for the mount's lifetime.  With the default tunable the two tests are identical. */
#define SUS_MOUNT_KSU_ID_MIN ((unsigned int)DEFAULT_KSU_MNT_ID)

static unsigned long param_min_mnt_id = DEFAULT_KSU_MNT_ID;
module_param_named(min_mnt_id, param_min_mnt_id, ulong, 0644);

/* SELinux context of the su/ksu domain, resolved to a sid at init.  Keep in sync
 * with susfs_avc_spoof.c's avc_su_ctx ("u:r:ksu:s0", the SukiSU variant; stock KernelSU
 * is "u:r:su:s0", override with susfs_guard_lkm.su_ctx=u:r:su:s0). */
static char param_su_ctx[128] = "u:r:ksu:s0";
module_param_string(su_ctx, param_su_ctx, sizeof(param_su_ctx), 0644);

static u32 su_sid;

/* The kernel's own mount-id allocator, resolved at init.  Below 6.18 that is the
 * ida:
 *   - mnt_id_ida: `static DEFINE_IDA(mnt_id_ida)` in fs/namespace.c:68 (data
 *     symbol; exactly one definition in the tree);
 *   - ida_alloc_range(): the out-of-line allocator (lib/idr.c:380;
 *     ida_alloc_min() is only a header inline over it, which is why ida_alloc_min
 *     has no kallsyms entry - do not try to resolve that one);
 *   - ida_free(): resolved as a corroborating check only.  We never call it: the
 *     paired free is done by the kernel itself in mnt_free_id()
 *     (fs/namespace.c:136-139) when a marked mount is finally freed, which is
 *     exactly what we want (and what keeps the umount path WARN-free).
 * 6.18 replaced the ida with the `mnt_id_xa` xarray and that free with
 * `__xa_erase`, so there the three names are mnt_id_xa / __xa_alloc / __xa_erase.
 * None of them is exported to modules: each is looked up in kallsyms and, when
 * that fails, taken from the loader's own externs below (the same names the
 * build-time System.map gate checks), with the two sources cross-checked at init.
 * Unlike ida_free(), the erase IS called from this file - by
 * sus_mount_ida_release(): an id that was allocated and not used, or the id a
 * mount used to own, has to go back to the allocator it came from.
 * Every name of the allocator in use must be present or we refuse to mark
 * anything (fail closed). */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
static struct xarray *sus_mount_mnt_id_xa;
static int (*pfn_xa_alloc)(struct xarray *xa, u32 *id, void *entry,
                           struct xa_limit limit, gfp_t gfp);
static void *(*pfn_xa_erase)(struct xarray *xa, unsigned long index);
static const char *sus_mount_xa_obj_src = "none";
static const char *sus_mount_xa_alloc_src = "none";
static const char *sus_mount_xa_erase_src = "none";
extern struct xarray mnt_id_xa;
extern int __xa_alloc(struct xarray *xa, u32 *id, void *entry,
                      struct xa_limit limit, gfp_t gfp);
extern void *__xa_erase(struct xarray *xa, unsigned long index);
#else
static struct ida *sus_mount_mnt_id_ida;
static int (*pfn_ida_alloc_range)(struct ida *ida, unsigned int min,
                                  unsigned int max, gfp_t gfp);
static void (*pfn_ida_free)(struct ida *ida, unsigned int id);
#endif

static bool sus_mount_ida_ready(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
    return sus_mount_mnt_id_xa && pfn_xa_alloc && pfn_xa_erase;
#else
    return sus_mount_mnt_id_ida && pfn_ida_alloc_range && pfn_ida_free;
#endif
}

/* security_cred_getsecid() is EXPORT_SYMBOL in security/security.c, but GKI's symbol
 * list is not guaranteed to carry it for modules, so it is resolved from kallsyms like
 * the other optional symbols this LKM uses. */
static void (*pfn_security_cred_getsecid)(const struct cred *cred, u32 *secid);
static char *(*pfn_d_path)(const struct path *path, char *buf, int buflen);

/* __nocfi on every function that reaches a resolved kernel symbol through a function
 * pointer: kCFI validates the type hash at such a call site and panics with
 * "CFI failure (target: ...)" otherwise - measured on this device, so these wrappers
 * keep the attribute. */
static __nocfi bool sus_mount_is_su_domain(void)
{
    u32 sid = 0;

    /* Unresolved symbol or an unresolvable su context: we cannot tell su apart,
     * so nothing is exempted (hide from every process) - loud in the init log. */
    if (!pfn_security_cred_getsecid || !su_sid)
        return false;
    /* kprobe pre_handler runs on the probed task with preemption disabled:
     * reading current->cred and the LSM hook list never sleeps. */
    pfn_security_cred_getsecid(current_cred(), &sid);
    return sid == su_sid;
}

/* Allocate a genuine id in the KSU range out of the kernel's own allocator -
 * mnt_id_ida below 6.18, the mnt_id_xa that replaced it from 6.18 on (see the
 * resolution note above).
 * Process context (GFP_KERNEL, may sleep).  Returns the id, or a negative errno
 * (-ENOMEM / -ENOSPC) - never a bogus id. */
static __nocfi int sus_mount_ida_alloc(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
    struct xa_limit limit = XA_LIMIT((u32)DEFAULT_KSU_MNT_ID, (u32)(INT_MAX - 1));
    u32 id = 0;
    int ret;

    xa_lock(sus_mount_mnt_id_xa);
    ret = pfn_xa_alloc(sus_mount_mnt_id_xa, &id, NULL, limit, GFP_KERNEL);
    xa_unlock(sus_mount_mnt_id_xa);
    if (ret)
        return ret;
    return (int)id;
#else
    return pfn_ida_alloc_range(sus_mount_mnt_id_ida,
                               (unsigned int)DEFAULT_KSU_MNT_ID,
                               (unsigned int)(INT_MAX - 1), GFP_KERNEL);
#endif
}

/* Hand an unused id back to the same ida it came from.  MUST go through a
 * __nocfi function, like every other call this module makes through a resolved
 * kernel symbol: an indirect call from a normally-instrumented function is
 * type-checked by clang CFI, and an out-of-tree module's type hash for a kernel
 * prototype does not match the kernel's own - measured, and expensive to learn:
 *
 *   Kernel panic - not syncing: CFI failure (target: ida_free+0x0/0x480)
 *   Call trace: sus_mount_mark_ksu_mounts+0x9b0 [susfs_guard_lkm]
 *
 * From 6.18 the same wrapper erases from mnt_id_xa (`__xa_erase`) instead; the
 * reasoning does not change with the symbol. */
static __nocfi void sus_mount_ida_release(int id)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
    xa_lock(sus_mount_mnt_id_xa);
    pfn_xa_erase(sus_mount_mnt_id_xa, (unsigned long)(unsigned int)id);
    xa_unlock(sus_mount_mnt_id_xa);
#else
    pfn_ida_free(sus_mount_mnt_id_ida, (unsigned int)id);
#endif
}

static __nocfi char *sus_mount_d_path(const struct path *path, char *buf, int buflen)
{
    if (!pfn_d_path)
        return ERR_PTR(-ENOSYS);
    return pfn_d_path(path, buf, buflen);
}

/* Effective threshold: clamped on every read too, because the sysfs knob can be
 * written at any time (init/enable also write the clamped value back). */
static unsigned long sus_mount_min_mnt_id(void)
{
    if (param_min_mnt_id < SUS_MOUNT_MIN_SANE_MNT_ID)
        return DEFAULT_KSU_MNT_ID;
    return param_min_mnt_id;
}

/* Declared up here because the new hooks' stat node reports it (it is otherwise
 * set by sus_mount_register() further down). */
static bool mount_registered;

/* Serialises the enable/disable control surface.  The supercall handler runs from the caller's
 * task_work, i.e. in process context, so two tasks (or a task and a `/proc/susfs_hide_mounts`
 * write) can enter sus_mount_register()/sus_mount_unregister() at the same time.  Both do
 * check-then-act on `mount_registered` and then call register_kprobe()/register_kretprobe(),
 * which sleep - and registering the same probe object twice is not harmless: an
 * address-registered kretprobe goes through warn_kprobe_rereg() (a WARN_ON_ONCE with a stack
 * trace naming this module, exactly the trace the file's header says must not appear), while a
 * name-registered one fails and the caller reports -EINVAL to userspace even though the other
 * task armed it.  sus_path.c and susfs_open_redirect.c already serialise their arm paths this
 * way. */
static DEFINE_MUTEX(sus_mount_ctl_lock);

/* ---- the same id in the two other places upstream rewrites ----
 *
 * Not enough to skip the mount line: /proc/<pid>/fdinfo/N prints "mnt_id:\t<i>" and statx(2)
 * returns stx_mnt_id, both taken straight from the mount the file lives on, so an app can
 * collect the mnt_ids of the fds it holds and look for ones /proc/self/mountinfo never
 * mentions - a positive indicator that something is hidden, and one that needs no root.
 * Upstream rewrites both to the id of the first mount up the chain that is not a KSU mount
 * (susfs_get_non_sus_mnt_id_from_mnt(), patch:849-858); that value is computed here once per
 * marked mount, at marking time, and kept in a small id table, because at rewrite time all we
 * have is the id (kprobe context: no sleeping, no lookups).
 *
 * The key is the one piece of state the kernel can hand to somebody else: mnt ids go back to
 * mnt_id_ida in mnt_free_id(), and the reuse is immediate, not theoretical - measured on this
 * device: mount tmpfs, note the id, umount, mount again hands out the SAME id.  Each entry
 * therefore carries the s_dev it was learned on and is dropped when (a) its superblock is torn
 * down (kprobe on generic_shutdown_super(), the earliest point where nothing can have taken the
 * id yet), (b) a mount that is NOT ours is seen carrying that id - proof of a recycle, so the
 * hide hook drops the entry right there (sus_mount_idmap_drop()), which is what keeps the table
 * fixed-size by reusing freed slots - or (c) it is refreshed by seeing one of OUR mounts again.
 * Left over is an id recycled while nobody reads a mount table at all: no reader then holds a
 * mount list to compare the rewritten number against.  A shown_id needs no expiry of its own:
 * an ancestor of its mount cannot be unmounted while the child is alive. */
#define SUS_MOUNT_IDMAP_MAX 64

/* sus_id == 0 marks a free slot; mnt ids are never 0. */
struct sus_mount_idmap_entry {
    int sus_id;
    int shown_id;
    dev_t s_dev;	/* the superblock the mount lived on (see the drop note) */
};

static struct sus_mount_idmap_entry mount_idmap[SUS_MOUNT_IDMAP_MAX];
static int n_idmap;
static DEFINE_SPINLOCK(idmap_lock);
static atomic_t n_idmap_recycled = ATOMIC_INIT(0);	/* stale entries dropped */
static atomic_t n_idmap_dropped_dev = ATOMIC_INIT(0);	/* dropped at sb teardown */

static atomic_t n_fdinfo_hits = ATOMIC_INIT(0);
static atomic_t n_fdinfo_rewrites = ATOMIC_INIT(0);
static atomic_t n_statx_hits = ATOMIC_INIT(0);
static atomic_t n_statx_rewrites = ATOMIC_INIT(0);

static void sus_mount_idmap_add(int sus_id, int shown_id, dev_t s_dev)
{
    unsigned long flags;
    int i, slot = -1;

    if (sus_id <= 0 || shown_id <= 0)
        return;
    spin_lock_irqsave(&idmap_lock, flags);
    /* Seen again on every enable and by both paths in, so an existing entry must not be
     * appended twice: the table is fixed size. */
    for (i = 0; i < n_idmap; i++) {
        if (mount_idmap[i].sus_id == sus_id) {
            mount_idmap[i].shown_id = shown_id;
            mount_idmap[i].s_dev = s_dev;
            goto out;
        }
        if (!mount_idmap[i].sus_id && slot < 0)
            slot = i;
    }
    if (slot < 0) {
        if (n_idmap >= SUS_MOUNT_IDMAP_MAX)
            goto out;
        slot = n_idmap++;
    }
    mount_idmap[slot].sus_id = sus_id;
    mount_idmap[slot].shown_id = shown_id;
    mount_idmap[slot].s_dev = s_dev;
out:
    spin_unlock_irqrestore(&idmap_lock, flags);
}

/* Every entry whose mount lived on @s_dev is worthless now: that superblock is
 * being shut down (see the note on sus_mount_ident_drop_dev()). */
static void sus_mount_idmap_drop_dev(dev_t s_dev)
{
    unsigned long flags;
    int i;

    spin_lock_irqsave(&idmap_lock, flags);
    for (i = 0; i < n_idmap; i++) {
        if (mount_idmap[i].sus_id && mount_idmap[i].s_dev == s_dev) {
            mount_idmap[i].sus_id = 0;
            mount_idmap[i].shown_id = 0;
            atomic_inc(&n_idmap_dropped_dev);
        }
    }
    spin_unlock_irqrestore(&idmap_lock, flags);
}

/* The id in @sus_id now belongs to a mount that is NOT ours, so what was learned for it
 * described a mount that no longer exists - a recycled id, observable in the hide hooks. */
static void sus_mount_idmap_drop(int sus_id)
{
    unsigned long flags;
    int i;

    if (sus_id <= 0)
        return;
    spin_lock_irqsave(&idmap_lock, flags);
    for (i = 0; i < n_idmap; i++) {
        if (mount_idmap[i].sus_id == sus_id) {
            mount_idmap[i].sus_id = 0;
            mount_idmap[i].shown_id = 0;
            atomic_inc(&n_idmap_recycled);
            break;
        }
    }
    spin_unlock_irqrestore(&idmap_lock, flags);
}

/* 0 means "not one of ours" - mnt_id 0 is never handed out. */
static int sus_mount_shown_for(int sus_id)
{
    unsigned long flags;
    int i, shown = 0;

    if (sus_id <= 0)
        return 0;
    spin_lock_irqsave(&idmap_lock, flags);
    for (i = 0; i < n_idmap; i++) {
        if (mount_idmap[i].sus_id == sus_id) {
            shown = mount_idmap[i].shown_id;
            break;
        }
    }
    spin_unlock_irqrestore(&idmap_lock, flags);
    return shown;
}

/* Upstream's susfs_get_non_sus_mnt_id_from_mnt(): climb past every marked mount
 * and report the id of the first one that is not ours.  Must be called AFTER
 * r->mnt_id has been replaced - upstream relies on the same thing, i.e. on the
 * starting mount already carrying a KSU-range id, or the loop would stop at the
 * mount itself and report its own (hidden) number. */
static int sus_mount_shown_id(struct mount *mnt)
{
    while (mnt && mnt->mnt_parent && mnt != mnt->mnt_parent &&
           (unsigned int)mnt->mnt_id >= SUS_MOUNT_KSU_ID_MIN)
        mnt = mnt->mnt_parent;
    return mnt ? mnt->mnt_id : 0;
}

/* ---- "is this mount one of ours", by IDENTITY and not only by id ----
 *
 * The id test is a property of ONE mount object in ONE namespace, and the id only
 * exists because the marking scan put it there: a KSU mount inside the app/zygote
 * namespace carries whatever id the stock allocator gave it (measured: the
 * meta-overlayfs mount is 2000000000 in the init namespace and 1111 in the
 * zygote's), so the id test hid the line in one view and left it in the other - the
 * app's own mount table, the one a checker reads.
 *
 * So the scan remembers what survives a namespace boundary (the mount OBJECT does not, its
 * filesystem does): s_dev + root ino, exact and cheap, because the root dentry is shared by
 * every mount of a superblock and a second tmpfs has its own root inode.  The NUMBER is
 * stored, not the dentry pointer: a held pointer would need dget() - pinning dentry and inode
 * past the superblock's shutdown, measured: rmmod of that build dput()'d it and panicked in
 * shmem_evict_inode, "Oops: Fatal exception" - or be used unlocked after the mount is gone.
 * mnt_devname is compared only when it is a path (starts with '/'), so a recorded
 * "tmpfs"/"overlay" cannot hide every mount of that kind.
 *
 * Only mounts the scan ACCEPTS are recorded, so this never widens into "hide anything that
 * looks similar".  A record is valid only while its filesystem is mounted, and its keys are
 * reusable: measured, a tmpfs mounted/recorded/unmounted by this test left s_dev 0:304 and
 * root inode 1 behind and the very next tmpfs got both, i.e. a stale record hid an unrelated
 * filesystem - hence the kprobe on generic_shutdown_super() that drops every record whose
 * s_dev is going away. */
#define SUS_MOUNT_DEVNAME_MAX 64
#define SUS_MOUNT_IDENT_MAX 32
/* Defined with the scan helpers further down; the identity test needs it here. */
static bool sus_mount_path_is_ours(const char *s);

struct sus_mount_ident {
    dev_t s_dev;
    unsigned long root_ino;	/* 0 = free slot */
    bool devname_is_path;
    /* strscpy() truncates a longer devname; without this flag the record could never match
     * its own live mount again (a truncated copy is always unequal), silently disabling
     * this fallback. */
    bool devname_truncated;
    char devname[SUS_MOUNT_DEVNAME_MAX];
};

static struct sus_mount_ident mount_ident[SUS_MOUNT_IDENT_MAX];
static int n_ident;
static DEFINE_SPINLOCK(ident_lock);
static atomic_t n_ident_hits = ATOMIC_INIT(0);		/* hidden by identity, not by id */
static atomic_t n_ident_learned = ATOMIC_INIT(0);	/* ids learned while hiding */
static atomic_t n_ident_full = ATOMIC_INIT(0);		/* records that found no slot */
static atomic_t n_ident_dropped_dev = ATOMIC_INIT(0);	/* records dropped at sb teardown */
static atomic_t n_sb_down = ATOMIC_INIT(0);		/* superblocks seen shut down */

static int mount_dbg;
module_param_named(mount_dbg, mount_dbg, int, 0644);
/* Diagnostic: name the fields the identity test compares, for the first few mounts the
 * hook sees, so "why did identity not match" is answerable from dmesg instead of guessed. */
static atomic_t n_dbg_logged = ATOMIC_INIT(0);

/* Process context.  Takes no reference on anything (see the note above): the
 * record is three numbers and a string, so a record whose mount is long gone costs
 * nothing and cannot keep a filesystem alive. */
static void sus_mount_ident_add(struct mount *r)
{
    const char *devname = r->mnt_devname;
    struct dentry *root = r->mnt.mnt_root;
    unsigned long ino;
    unsigned long flags;
    int i, slot = -1;

    if (!root || !root->d_inode)
        return;
    ino = root->d_inode->i_ino;
    if (!ino)
        return;

    spin_lock_irqsave(&ident_lock, flags);
    /* Re-check under the lock so two scans cannot both append; a re-enable finds them here. */
    for (i = 0; i < n_ident; i++) {
        if (mount_ident[i].s_dev == r->mnt.mnt_sb->s_dev &&
            mount_ident[i].root_ino == ino)
            goto out;
        if (!mount_ident[i].root_ino && slot < 0)
            slot = i;
    }
    if (slot < 0) {
        if (n_ident >= SUS_MOUNT_IDENT_MAX) {
            /* Silent truncation is a "registered but not effective" failure; say it once. */
            if (atomic_inc_return(&n_ident_full) == 1)
                pr_warn("sus_mount: identity table full (%d), %s is NOT recognised in other namespaces\n",
                        SUS_MOUNT_IDENT_MAX,
                        devname ? devname : "(no devname)");
            goto out;
        }
        slot = n_ident;
        smp_store_release(&n_ident, n_ident + 1);
    }
    {
        struct sus_mount_ident *e = &mount_ident[slot];

        e->s_dev = r->mnt.mnt_sb->s_dev;
        e->devname_is_path = devname && devname[0] == '/';
        if (e->devname_is_path) {
            e->devname_truncated = strlen(devname) >= sizeof(e->devname);
            strscpy(e->devname, devname, sizeof(e->devname));
        } else {
            e->devname[0] = '\0';
        }
        if (mount_dbg)
            SUSFS_LOGI("sus_mount: ident[%d] s_dev=%u root_ino=%lu devname=%s\n",
                    slot, (unsigned int)e->s_dev, ino,
                    e->devname_is_path ? e->devname : "(not path-shaped)");
        /* Set LAST: root_ino != 0 is what makes the record live for the readers. */
        smp_store_release(&e->root_ino, ino);
    }
out:
    spin_unlock_irqrestore(&ident_lock, flags);
}

/* Interrupt-context safe (kprobe pre_handler): read-only, no sleeping. */
static bool sus_mount_ident_match(struct mount *r)
{
    int n = smp_load_acquire(&n_ident);
    const char *devname = r->mnt_devname;
    struct dentry *root = r->mnt.mnt_root;
    unsigned long ino = (root && root->d_inode) ? root->d_inode->i_ino : 0;
    int i;

    for (i = 0; i < n; i++) {
        const struct sus_mount_ident *e = &mount_ident[i];
        unsigned long e_ino = smp_load_acquire(&e->root_ino);

        if (!e_ino)
            continue;	/* free or invalidated slot: neither of its keys is valid */
        if (e_ino == ino && e->s_dev == r->mnt.mnt_sb->s_dev)
            return true;
        if (e->devname_is_path && devname &&
            (e->devname_truncated
                 ? !strncmp(e->devname, devname, sizeof(e->devname) - 1)
                 : !strcmp(e->devname, devname)))
            return true;
    }
    return false;
}

/**
 * sus_mount_ident_drop_dev() - forget every record that lived on @s_dev
 *
 * From a kprobe on generic_shutdown_super(): without it a record outlives its filesystem,
 * and the numbers it is keyed by are recyclable (measured above), so a stale record hides
 * an unrelated filesystem.  Process context, takes only our own spinlock, never sleeps.
 */
static void sus_mount_ident_drop_dev(dev_t s_dev)
{
    unsigned long flags;
    int i;

    spin_lock_irqsave(&ident_lock, flags);
    for (i = 0; i < n_ident; i++) {
        if (smp_load_acquire(&mount_ident[i].root_ino) && mount_ident[i].s_dev == s_dev) {
            /* The devname arm has to die first: it is tested independently of root_ino, so
             * clearing only the inode number left the record hiding any mount that reused
             * the source string - the stale-record bug this whole hook exists to prevent. */
            mount_ident[i].devname_is_path = false;
            mount_ident[i].devname[0] = '\0';
            smp_store_release(&mount_ident[i].root_ino, 0);
            atomic_inc(&n_ident_dropped_dev);
        }
    }
    spin_unlock_irqrestore(&ident_lock, flags);
}

/* generic_shutdown_super(struct super_block *sb) - fs/super.c, EXPORT_SYMBOL, so
 * it is a real function in kallsyms and not an inlined call site.  Runs before the
 * device number is handed back (free_anon_bdev()/blkdev_put() happen in
 * kill_anon_super()/kill_block_super() after this returns), so a record cannot
 * survive into the window where a new filesystem holds the same s_dev. */
static int sus_mount_sb_down_pre(struct kprobe *kp, struct pt_regs *regs)
{
    struct super_block *sb = (struct super_block *)regs->regs[0];

    atomic_inc(&n_sb_down);
    if (!sb)
        return 0;
    sus_mount_ident_drop_dev(sb->s_dev);
    sus_mount_idmap_drop_dev(sb->s_dev);
    return 0;
}

static struct kprobe kp_sb_down = {
    .symbol_name = "generic_shutdown_super",
    .pre_handler = sus_mount_sb_down_pre,
};
static bool kp_sb_down_ok;

/* Is this mount one of KernelSU's?  Two tests, cheapest first:
 *   - the id range, which is upstream's rule and what the marking scan produces;
 *   - the identity recorded by that scan, which is what catches the same mount in
 *     a namespace the scan could not reach (see the note above). */
static bool sus_mount_is_ours(struct mount *r)
{
    if ((unsigned int)r->mnt_id >= SUS_MOUNT_KSU_ID_MIN)
        return true;
    if (sus_mount_path_is_ours(r->mnt_devname))
        return true;
    return sus_mount_ident_match(r);
}

/* The id the caller is allowed to see for a mount we hide: the first ancestor
 * that is not ours.  Starts at the PARENT when the mount itself is not in the id
 * range, because an identity-recognised mount keeps a normal id and would
 * otherwise report its own (hidden) number. */
static int sus_mount_shown_id_from(struct mount *mnt)
{
    if (mnt && (unsigned int)mnt->mnt_id < SUS_MOUNT_KSU_ID_MIN)
        mnt = mnt->mnt_parent;
    while (mnt && mnt->mnt_parent && mnt != mnt->mnt_parent &&
           sus_mount_is_ours(mnt))
        mnt = mnt->mnt_parent;
    return mnt ? (int)mnt->mnt_id : 0;
}

/* Remember the id -> shown-id pair for a mount we are about to hide, so the
 * fdinfo/statx faces rewrite the very same id the app would otherwise see.  The
 * hide hooks are the only place that has a mount pointer in an app's namespace,
 * so learning here is what keeps "the line is gone" and "the number in fdinfo
 * names a line that exists" consistent for namespaces the scan never saw. */
static void sus_mount_note_id(struct mount *r)
{
    int shown;

    if (sus_mount_shown_for((int)r->mnt_id))
        return;
    shown = sus_mount_shown_id_from(r);
    if (shown > 0 && shown != (int)r->mnt_id) {
        sus_mount_idmap_add((int)r->mnt_id, shown, r->mnt.mnt_sb->s_dev);
        atomic_inc(&n_ident_learned);
    }
}

/* ---- namespaces created AFTER the enable ----
 *
 * Marking is a property of one mount object in one namespace and it does NOT
 * travel: fs/namespace.c:1054 clone_mnt() builds the copy with alloc_vfsmnt()
 * (:196), which calls mnt_alloc_id() (:126) - every mount in a namespace copied by
 * fork/unshare gets a BRAND NEW id, as measured on this device (the copy of the
 * marked mount has no 2e9 id at all).  So after an app unshares, or after zygote
 * restarts, nothing in that namespace carries a marked id.
 *
 * WHEN and WHERE that walk happens follows the tree the kernel keeps, because that is
 * what changed in 6.12 (this trap once panicked the device):
 *   - below 6.12 it is done in the return probe itself, on the namespace copy_mnt_ns()
 *     has just built for THIS task and no nsproxy carries yet (switch_task_namespaces()
 *     runs later), so no other task can mount into or umount from it, namespace_sem was
 *     released inside copy_mnt_ns() (fs/namespace.c:3986) and ns_lock is the whole lock
 *     (sus_mount_learn_ns());
 *   - from 6.12 the mounts hang off an rb-tree under namespace_sem, and taking that lock
 *     may sleep while a kretprobe handler must not - so the probe only records the
 *     namespace and a workqueue walks it in process context with namespace_sem held for
 *     read (sus_mount_defer_learn_ns()/sus_mount_learn_ns_walk()).  A namespace
 *     reference is taken for the deferral, and the walk is refused with -ENOSYS when
 *     namespace_sem could not be resolved at load time rather than iterating a tree the
 *     kernel can rebalance under us.
 * Either way the flags are checked first - without CLONE_NEWNS copy_mnt_ns() returns the
 * CURRENT namespace, the plain fork path, and walking a live namespace there is exactly
 * that bug - and nothing is allocated: the id table is fixed size. */
static atomic_t n_clone_walks = ATOMIC_INIT(0);
static atomic_t n_clone_learned = ATOMIC_INIT(0);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
static atomic_t n_clone_queued = ATOMIC_INIT(0);
static atomic_t n_clone_dropped = ATOMIC_INIT(0);
static atomic_t n_clone_refused_logged = ATOMIC_INIT(0);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
static struct rw_semaphore *sus_mount_namespace_sem;

static bool sus_mount_ns_walk_begin(void)
{
    if (!sus_mount_namespace_sem)
        return false;
    down_read(sus_mount_namespace_sem);
    return true;
}

static void sus_mount_ns_walk_end(void)
{
    up_read(sus_mount_namespace_sem);
}

#define SUS_MOUNT_ITER_TYPE		struct rb_node *
#define SUS_MOUNT_ITER_FOR(ns, it)	for ((it) = rb_first(&(ns)->mounts); (it); (it) = rb_next(it))
#define SUS_MOUNT_ITER_MOUNT(it)	rb_entry((it), struct mount, mnt_node)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
#define SUS_MOUNT_MNT_NOT_IN_NS(r, ns)	((r)->mnt_ns != (ns) || !mnt_ns_attached(r))
#else

#define SUS_MOUNT_MNT_NOT_IN_NS(r, ns)	((r)->mnt_ns != (ns) || RB_EMPTY_NODE(&(r)->mnt_node))
#endif
#else
static bool sus_mount_ns_walk_begin(void)
{
    return true;	/* the caller takes ns_lock instead */
}

static void sus_mount_ns_walk_end(void)
{
}

#define SUS_MOUNT_ITER_TYPE		struct list_head *
#define SUS_MOUNT_ITER_FOR(ns, it)	for ((it) = (ns)->list.next; (it) != &(ns)->list; (it) = (it)->next)
#define SUS_MOUNT_ITER_MOUNT(it)	list_entry((it), struct mount, mnt_list)
#define SUS_MOUNT_MNT_NOT_IN_NS(r, ns)	((r)->mnt_ns != (ns) || ((r)->mnt.mnt_flags & MNT_CURSOR))
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)

static void sus_mount_learn_ns(struct mnt_namespace *ns)
{
    SUS_MOUNT_ITER_TYPE pos;
    struct mount *r;
    int learned = 0;

    spin_lock(&ns->ns_lock);
    SUS_MOUNT_ITER_FOR(ns, pos) {
        int shown;

        r = SUS_MOUNT_ITER_MOUNT(pos);
        if (SUS_MOUNT_MNT_NOT_IN_NS(r, ns))
            continue;
        if (!sus_mount_is_ours(r))
            continue;
        if (sus_mount_shown_for((int)r->mnt_id))
            continue;
        shown = sus_mount_shown_id_from(r);
        if (shown > 0 && shown != (int)r->mnt_id) {
            sus_mount_idmap_add((int)r->mnt_id, shown, r->mnt.mnt_sb->s_dev);
            learned++;
        }
    }
    spin_unlock(&ns->ns_lock);
    atomic_inc(&n_clone_walks);
    if (learned)
        atomic_add(learned, &n_clone_learned);
}
#else
#define SUS_MOUNT_LEARN_RING 16
static struct mnt_namespace *sus_mount_learn_ring[SUS_MOUNT_LEARN_RING];
static unsigned int sus_mount_learn_head, sus_mount_learn_tail;
static DEFINE_SPINLOCK(sus_mount_learn_lock);
static void (*pfn_put_mnt_ns)(struct mnt_namespace *ns);
static void sus_mount_learn_work(struct work_struct *w);
static DECLARE_WORK(sus_mount_learn_wq, sus_mount_learn_work);

static __nocfi void sus_mount_put_mnt_ns(struct mnt_namespace *ns)
{
    pfn_put_mnt_ns(ns);
}

/* One recorded namespace, or NULL when the ring is empty. */
static struct mnt_namespace *sus_mount_learn_pop(void)
{
    unsigned long flags;
    struct mnt_namespace *ns;

    spin_lock_irqsave(&sus_mount_learn_lock, flags);
    if (sus_mount_learn_head == sus_mount_learn_tail) {
        spin_unlock_irqrestore(&sus_mount_learn_lock, flags);
        return NULL;
    }
    ns = sus_mount_learn_ring[sus_mount_learn_tail];
    sus_mount_learn_ring[sus_mount_learn_tail] = NULL;
    sus_mount_learn_tail = (sus_mount_learn_tail + 1) % SUS_MOUNT_LEARN_RING;
    spin_unlock_irqrestore(&sus_mount_learn_lock, flags);
    return ns;
}

static int sus_mount_learn_ns_walk(struct mnt_namespace *ns)
{
    SUS_MOUNT_ITER_TYPE pos;
    unsigned int seen = 0;
    int learned = 0;

    if (!sus_mount_ns_walk_begin())
        return -ENOSYS;

    rcu_read_lock();
    SUS_MOUNT_ITER_FOR(ns, pos) {
        struct mount *r;
        int shown;

        if (seen++ >= SUS_MOUNT_MAX_SCAN)
            break;
        r = SUS_MOUNT_ITER_MOUNT(pos);
        if (SUS_MOUNT_MNT_NOT_IN_NS(r, ns))
            continue;
        if (!sus_mount_is_ours(r))
            continue;
        if (sus_mount_shown_for((int)r->mnt_id))
            continue;
        shown = sus_mount_shown_id_from(r);
        if (shown > 0 && shown != (int)r->mnt_id) {
            sus_mount_idmap_add((int)r->mnt_id, shown, r->mnt.mnt_sb->s_dev);
            learned++;
        }
    }
    rcu_read_unlock();
    sus_mount_ns_walk_end();
    return learned;
}

static void sus_mount_learn_work(struct work_struct *w)
{
    struct mnt_namespace *ns;

    while ((ns = sus_mount_learn_pop()) != NULL) {
        int learned = sus_mount_learn_ns_walk(ns);

        if (learned == -ENOSYS && !atomic_xchg(&n_clone_refused_logged, 1))
            pr_warn("sus_mount: namespace_sem could not be resolved at load time - refusing to walk a cloned namespace without the lock the kernel's own mount-table iterator holds (that namespace's ids stay unlearned)\n");
        atomic_inc(&n_clone_walks);
        if (learned > 0)
            atomic_add(learned, &n_clone_learned);
        /* After sus_mount_ns_walk_end(): see the note above about 6.18. */
        sus_mount_put_mnt_ns(ns);
    }
}

/* Called from the copy_mnt_ns return probe: no allocation, no sleeping. */
static void sus_mount_defer_learn_ns(struct mnt_namespace *ns)
{
    unsigned int next;
    unsigned long flags;

    if (!pfn_put_mnt_ns) {
        atomic_inc(&n_clone_dropped);
        return;
    }
    get_mnt_ns(ns);

    spin_lock_irqsave(&sus_mount_learn_lock, flags);
    next = (sus_mount_learn_head + 1) % SUS_MOUNT_LEARN_RING;
    if (next == sus_mount_learn_tail) {
        spin_unlock_irqrestore(&sus_mount_learn_lock, flags);
        sus_mount_put_mnt_ns(ns);
        atomic_inc(&n_clone_dropped);
        return;
    }
    sus_mount_learn_ring[sus_mount_learn_head] = ns;
    sus_mount_learn_head = next;
    spin_unlock_irqrestore(&sus_mount_learn_lock, flags);

    atomic_inc(&n_clone_queued);
    schedule_work(&sus_mount_learn_wq);
}

static void sus_mount_learn_stop(void)
{
    struct mnt_namespace *ns;

    cancel_work_sync(&sus_mount_learn_wq);
    while ((ns = sus_mount_learn_pop()) != NULL)
        sus_mount_put_mnt_ns(ns);
}
#endif

struct sus_mount_clone_state {
    unsigned long flags;
};

static int sus_mount_clone_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct sus_mount_clone_state *st = (struct sus_mount_clone_state *)ri->data;

    st->flags = regs->regs[0];
    return 0;
}

static int sus_mount_clone_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct sus_mount_clone_state *st = (struct sus_mount_clone_state *)ri->data;
    struct mnt_namespace *ns = (struct mnt_namespace *)regs_return_value(regs);

    if (!(st->flags & CLONE_NEWNS))
        return 0;			/* plain fork: the current namespace, not a copy */
    if (IS_ERR_OR_NULL(ns))
        return 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
    sus_mount_learn_ns(ns);
#else
    sus_mount_defer_learn_ns(ns);
#endif
    return 0;
}

/* copy_mnt_ns() is not static (fs/namespace.c:3424) and is on every namespace
 * creation path (fork with CLONE_NEWNS, unshare, clone3). */
static struct kretprobe kr_clone_ns = {
    .kp.symbol_name = "copy_mnt_ns",
    .entry_handler = sus_mount_clone_entry,
    .handler = sus_mount_clone_ret,
    .data_size = sizeof(struct sus_mount_clone_state),
    .maxactive = 16,
};
static bool kr_clone_ns_ok;

/* ---- mounts that appear AFTER the enable ----
 *
 * The scan runs once, on the current namespace: a filesystem mounted later - the ordinary
 * case for a module image installed while the phone is up - carried no marked id and no
 * recorded identity, so nothing hid it in ANY namespace (measured before this hook existed:
 * visible to a non-su reader in the init namespace and in a fresh clone).
 *
 * attach_recursive_mnt() is the one function every mount path goes through - exactly two
 * callers, graft_tree() (mount(2)/fsmount and do_loopback binds) and do_move_mount() (a
 * move) - so one kretprobe covers all of them.  Identity is recorded and not an id: it is
 * what the hide hooks compare, needs no allocation and is namespace independent, whereas a
 * KSU-range id needs ida_alloc_range(GFP_KERNEL) and a kprobe handler must not sleep.  The
 * shown id is learned at the RETURN (mnt_parent/mnt_mountpoint exist then), and the
 * acceptance rule is the scan's, so this cannot widen what is hidden - a bind of an already
 * recorded filesystem needs no record at all. */
#define SUS_MOUNT_NEWMNT_PATH_MAX 256

static atomic_t n_newmnt_seen = ATOMIC_INIT(0);
static atomic_t n_newmnt_recorded = ATOMIC_INIT(0);
static atomic_t n_newmnt_pathfail = ATOMIC_INIT(0);

struct sus_mount_newmnt_state {
    struct mount *m;
};

static int sus_mount_newmnt_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct sus_mount_newmnt_state *st = (struct sus_mount_newmnt_state *)ri->data;

    st->m = (struct mount *)regs->regs[0];	/* source_mnt */
    return 0;
}

/* The scan's rule, verbatim: a devname anchored at /data/adb, or a mountpoint under
 * /data/adb.  @buf is the caller's, small on purpose - this runs from a kretprobe
 * handler, where a PATH_MAX stack buffer would be a stack overflow waiting to happen,
 * and the mountpoints this rule accepts are short (/data/adb/modules/<name>/mnt). */
static bool sus_mount_should_record(struct mount *r, char *buf, int buflen, char **why)
{
    struct path p;
    char *dp;

    if (sus_mount_path_is_ours(r->mnt_devname)) {
        *why = "devname";
        return true;
    }
    if (!pfn_d_path)
        return false;
    p.mnt = &r->mnt;
    p.dentry = r->mnt.mnt_root;
    dp = sus_mount_d_path(&p, buf, buflen);
    if (IS_ERR_OR_NULL(dp)) {
        atomic_inc(&n_newmnt_pathfail);
        return false;
    }
    if (sus_mount_path_is_ours(dp)) {
        *why = dp;
        return true;
    }
    return false;
}

static int sus_mount_newmnt_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct sus_mount_newmnt_state *st = (struct sus_mount_newmnt_state *)ri->data;
    struct mount *r = st->m;
    char buf[SUS_MOUNT_NEWMNT_PATH_MAX];
    char *why = NULL;

    if (!susfs_ptr_plausible(r) || (long)regs_return_value(regs) != 0)
        return 0;			/* the attach failed: nothing was mounted */
    atomic_inc(&n_newmnt_seen);
    /* A successful attach leaves the mount with a parent; without one there is no
     * mountpoint path to test and no parent chain for the shown id. */
    if (!r->mnt_parent || r->mnt_parent == r)
        return 0;
    if (!sus_mount_should_record(r, buf, sizeof(buf), &why))
        return 0;

    sus_mount_ident_add(r);
    sus_mount_note_id(r);
    atomic_inc(&n_newmnt_recorded);
    SUSFS_LOGI("sus_mount: learned mount at %s (mnt_id %d, devname %s)\n",
            why, r->mnt_id, r->mnt_devname ? r->mnt_devname : "none");
    return 0;
}

static struct kretprobe kr_newmnt = {
    .kp.symbol_name = "attach_recursive_mnt",
    .entry_handler = sus_mount_newmnt_entry,
    .handler = sus_mount_newmnt_ret,
    .data_size = sizeof(struct sus_mount_newmnt_state),
    .maxactive = 16,
};
static bool kr_newmnt_ok;

/* ---- /proc/<pid>/fdinfo/N ----
 *
 * fs/proc/fd.c:seq_show() formats pos/flags/mnt_id/ino into the seq_file buffer and
 * returns; a kprobe cannot see the mnt_id (a local), but it can read the text already in
 * m->buf at return time: find the label, parse the decimal after it, replace it with the id
 * the app is supposed to see.  The replacement is never longer (a shown id is a normal,
 * small one), so the buffer is only shortened.  Works whether the kernel formats the line
 * with one seq_printf (AOSP 5.15) or with seq_put_decimal_ull() - both leave
 * "mnt_id:\t<digits>" in the buffer at return. */
#define SUS_MOUNT_MNTID_LABEL		"mnt_id:\t"
#define SUS_MOUNT_MNTID_LABEL_LEN	8

/* Per-instance state for the kretprobes below (fdinfo + the two statx landing points), in
 * ri->data and NOT in per-CPU storage: seq_show() formats through seq_printf(), whose
 * seq_buf_alloc() is GFP_KERNEL and can sleep, so with CONFIG_PREEMPT=y the task can resume
 * on another CPU where a per-CPU slot would hold NULL or a seq_file of an unrelated /proc
 * read that the return handler would then memmove into.  A kretprobe instance is per-task
 * (the getdents64 filter in sus_path.c does the same), and one struct serves all of them
 * because the fields are disjoint. */
struct sus_mount_kretprobe_state {
    struct seq_file *m;		/* fdinfo: the seq_file being filled */
    unsigned long ubuf;		/* statx: the caller's struct statx __user * */
    char where;			/* statx: 's' = __arm64_sys_statx, 'd' = do_statx */
};

static atomic_t n_fdinfo_entry = ATOMIC_INIT(0);
static atomic_t n_fdinfo_nolabel = ATOMIC_INIT(0);

static int sus_mount_fdinfo_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct sus_mount_kretprobe_state *st = (struct sus_mount_kretprobe_state *)ri->data;

    atomic_inc(&n_fdinfo_entry);
    /* Not dereferenced here - the return handler checks it - but an implausible
     * value means this kretprobe sits on a function that does not take a seq_file,
     * and the rewrite below would then read an unrelated object. */
    st->m = susfs_ptr_plausible((void *)regs->regs[0])
               ? (struct seq_file *)regs->regs[0] : NULL;
    return 0;
}

/* Replaces the decimal after the mnt_id label in the seq_file's already formatted buffer
 * when the id has a disguise in sus_mount's table (KSU-range id -> host id).  The
 * replacement never grows, so m->count stays consistent.  The other half of the fdinfo line
 * (the ino) belongs to open_redirect's own kretprobe, which must fire whether or not this
 * feature's hide switch is on. */
static bool sus_mount_fdinfo_replace_mntid(struct seq_file *m)
{
    char *buf = m->buf, digits[12];
    size_t count = m->count, i, pos = 0, len = 0, n = 0;
    unsigned long old = 0;
    int shown;
    unsigned int v;

    for (i = 0; i + SUS_MOUNT_MNTID_LABEL_LEN < count; i++) {
        if (!memcmp(buf + i, SUS_MOUNT_MNTID_LABEL, SUS_MOUNT_MNTID_LABEL_LEN)) {
            pos = i + SUS_MOUNT_MNTID_LABEL_LEN;
            break;
        }
    }
    if (!pos)
        return false;

    while (pos + len < count && len < 10 &&
           buf[pos + len] >= '0' && buf[pos + len] <= '9') {
        old = old * 10 + (unsigned long)(buf[pos + len] - '0');
        len++;
    }
    if (!len)
        return false;

    shown = sus_mount_shown_for((int)old);
    if (shown <= 0)
        return false;

    v = (unsigned int)shown;
    while (v) {
        digits[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    if (!n)
        return false;
    /* In practice a KSU-range id is replaced by a small host id, so this shrinks -
     * but the room check is here so a rule with an unexpectedly long host id
     * cannot overwrite past the seq_file buffer. */
    if (n > len && count + (n - len) >= m->size)
        return false;
    for (i = 0; i < n / 2; i++) {
        char t = digits[i];

        digits[i] = digits[n - 1 - i];
        digits[n - 1 - i] = t;
    }
    if (n != len)
        memmove(buf + pos + n, buf + pos + len, count - (pos + len));
    memcpy(buf + pos, digits, n);
    m->count = count - len + n;
    return true;
}

static int sus_mount_fdinfo_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct sus_mount_kretprobe_state *st = (struct sus_mount_kretprobe_state *)ri->data;
    struct seq_file *m = st->m;

    if (!m || (long)regs_return_value(regs) != 0)
        return 0;
    if (!m->buf || !m->count)
        return 0;
    /* Only fdinfo's own file is rewritten, and above 5.15 this probe sits on ALL FOUR
     * functions named seq_show (see the note at kr_fdinfo[]): the file being read is
     * m->file, and /proc/<pid>/fdinfo/<fd> (and .../task/<tid>/fdinfo/<fd>) has a dentry
     * whose parent is named "fdinfo".  The mnt_id label test below already makes the scan a
     * no-op for another file, so this only keeps it off other files' buffers. */
    if (!m->file || !m->file->f_path.dentry || !m->file->f_path.dentry->d_parent ||
        strcmp(m->file->f_path.dentry->d_parent->d_name.name, "fdinfo") != 0) {
        atomic_inc(&n_fdinfo_nolabel);
        return 0;
    }
    /* The su/ksu domain keeps seeing its own mounts' real ids, exactly like the
     * mount-line skip above. */
    if (sus_mount_is_su_domain())
        return 0;

    atomic_inc(&n_fdinfo_hits);

    if (sus_mount_fdinfo_replace_mntid(m))
        atomic_inc(&n_fdinfo_rewrites);
    else
        atomic_inc(&n_fdinfo_nolabel);
    return 0;
}

/* fs/proc/fd.c's fdinfo callback is registered through single_open(file, seq_show, inode), so
 * no table holds its address - and the name is NOT unique above 5.15: the DDK trees have four
 * symbols called exactly `seq_show` on android14-6.1 and android15-6.6, and one on 5.10/5.15.
 * register_kprobe(.symbol_name=...) attaches to whichever kallsyms lists first, which is how the
 * fdinfo rewrite silently stopped working on those kernels (mountinfo showed the disguised id
 * while /proc/<pid>/fdinfo/N printed the real one - a one-file oracle).  So: enumerate every
 * match and hook all of them; the return handler decides by the file it sees, which makes an
 * unrelated seq_show cost one comparison and change nothing. */
#define SUS_MOUNT_FDINFO_MAX 8

static struct kretprobe kr_fdinfo[SUS_MOUNT_FDINFO_MAX];
static int n_fdinfo_probes;
static bool kr_fdinfo_ok;

static int sus_mount_fdinfo_arm(void)
{
    unsigned long addrs[SUS_MOUNT_FDINFO_MAX];
    int i, n;

    n = ksu_find_symbol_all("seq_show", addrs, SUS_MOUNT_FDINFO_MAX);
    if (n <= 0) {
        pr_warn("sus_mount: seq_show not resolved - fdinfo keeps printing the real mnt_id\n");
        return -ENOENT;
    }
    for (i = 0; i < n; i++) {
        struct kretprobe *kr = &kr_fdinfo[i];
        int rc;

        kr->kp.addr = (void *)addrs[i];
        kr->entry_handler = sus_mount_fdinfo_entry;
        kr->handler = sus_mount_fdinfo_ret;
        kr->data_size = sizeof(struct sus_mount_kretprobe_state);
        kr->maxactive = 16;
        rc = register_kretprobe(kr);
        if (rc) {
            pr_warn("sus_mount: register_kretprobe(seq_show @%px) failed %d\n", (void *)addrs[i], rc);
            susfs_krp_forget_addr(kr);
            continue;
        }
        n_fdinfo_probes++;
    }
    if (!n_fdinfo_probes)
        return -EINVAL;
    SUSFS_LOGI("sus_mount: fdinfo hooked on %d/%d seq_show symbol(s)\n", n_fdinfo_probes, n);
    kr_fdinfo_ok = true;
    return 0;
}

static void sus_mount_fdinfo_disarm(void)
{
    int i;

    for (i = 0; i < SUS_MOUNT_FDINFO_MAX; i++) {
        if (!kr_fdinfo[i].kp.addr)
            continue;
        unregister_kretprobe(&kr_fdinfo[i]);
        susfs_krp_forget_addr(&kr_fdinfo[i]);
    }
    n_fdinfo_probes = 0;
    kr_fdinfo_ok = false;
}

/* ---- statx(2): stx_mnt_id ----
 *
 * vfs_statx() fills stat->mnt_id right after the getattr callback, so the only place a
 * kprobe can change it is the uapi struct the syscall is about to copy out: entry (take the
 * user pointer) plus return (rewrite if the call succeeded).  Two landing points, because
 * "the wrapper is in kallsyms" says nothing about who is really called: __arm64_sys_statx is
 * the syscall entry, do_statx what it delegates to.  The rewrite is idempotent (a rewritten
 * id is not in the table), so arming both is safe, and which one fires is reported
 * separately.
 *
 * They do NOT read the same register: __arm64_sys_statx is `asmlinkage long
 * __arm64_sys_statx(const struct pt_regs *)`
 * (arch/arm64/include/asm/syscall_wrapper.h), so its buffer is
 * ((struct pt_regs *)regs->regs[0])->regs[4] - reading regs->regs[4] directly works only
 * because the dispatcher leaves x1..x7 untouched, the reason the reboot handler in
 * susfs_supercall.c goes through PT_REAL_REGS.  do_statx(int dfd, const char __user
 * *filename, unsigned flags, unsigned int mask, struct statx __user *buffer) is an ordinary
 * function, so x4 IS the buffer. */
static atomic_t n_statx_entry = ATOMIC_INIT(0);
static atomic_t n_statx_ret = ATOMIC_INIT(0);
static atomic_t n_statx_nobuf = ATOMIC_INIT(0);
static atomic_t n_statx_err = ATOMIC_INIT(0);
static atomic_t n_statx_copyfail = ATOMIC_INIT(0);
static atomic_t n_statx_nomap = ATOMIC_INIT(0);

static int sus_mount_statx_entry_sys(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct sus_mount_kretprobe_state *st = (struct sus_mount_kretprobe_state *)ri->data;
    const struct pt_regs *uregs = (const struct pt_regs *)regs->regs[0];

    atomic_inc(&n_statx_entry);
    st->where = 's';
    st->ubuf = uregs ? (unsigned long)uregs->regs[4] : 0;
    return 0;
}

static int sus_mount_statx_entry_do(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct sus_mount_kretprobe_state *st = (struct sus_mount_kretprobe_state *)ri->data;

    atomic_inc(&n_statx_entry);
    st->where = 'd';
    st->ubuf = regs->regs[4];
    return 0;
}

static int sus_mount_statx_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct sus_mount_kretprobe_state *st = (struct sus_mount_kretprobe_state *)ri->data;
    unsigned long ubuf = st->ubuf;
    u64 id = 0, shown;
    int new_id;

    atomic_inc(&n_statx_ret);
    if ((long)regs_return_value(regs) != 0) {
        atomic_inc(&n_statx_err);
        return 0;
    }
    if (!ubuf) {
        /* Named, because "which landing point had no pointer" is the difference
         * between a wrong register and a wrong understanding of the call. */
        atomic_inc(&n_statx_nobuf);
        pr_info_ratelimited("sus_mount: statx landing point %c had no buffer\n",
                            st->where);
        return 0;
    }
    if (sus_mount_is_su_domain())
        return 0;

    atomic_inc(&n_statx_hits);
    if (copy_from_user(&id, (void __user *)(ubuf + offsetof(struct statx, stx_mnt_id)),
                       sizeof(id))) {
        atomic_inc(&n_statx_copyfail);
        return 0;
    }
    new_id = sus_mount_shown_for((int)id);
    if (new_id <= 0) {
        atomic_inc(&n_statx_nomap);
        return 0;
    }
    shown = (u64)new_id;
    if (copy_to_user((void __user *)(ubuf + offsetof(struct statx, stx_mnt_id)),
                     &shown, sizeof(shown))) {
        atomic_inc(&n_statx_copyfail);
        return 0;
    }
    atomic_inc(&n_statx_rewrites);
    return 0;
}

static struct kretprobe kr_statx = {
    .kp.symbol_name = "__arm64_sys_statx",
    .entry_handler = sus_mount_statx_entry_sys,
    .handler = sus_mount_statx_ret,
    .data_size = sizeof(struct sus_mount_kretprobe_state),
    .maxactive = 16,
};
static bool kr_statx_ok;

static struct kretprobe kr_statx_do = {
    .kp.symbol_name = "do_statx",
    .entry_handler = sus_mount_statx_entry_do,
    .handler = sus_mount_statx_ret,
    .data_size = sizeof(struct sus_mount_kretprobe_state),
    .maxactive = 16,
};
static bool kr_statx_do_ok;

/* Live entries, not slots: after an invalidation the slot is free but the slot
 * count stays where it was, and a diagnostic that reads "idmap=6" while only two
 * entries are usable is the kind of number this project keeps catching. */
static int sus_mount_idmap_live(void)
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

static int sus_mount_ident_live(void)
{
    unsigned long flags;
    int i, live = 0;

    spin_lock_irqsave(&ident_lock, flags);
    for (i = 0; i < READ_ONCE(n_ident); i++)
        if (smp_load_acquire(&mount_ident[i].root_ino))
            live++;
    spin_unlock_irqrestore(&ident_lock, flags);
    return live;
}

/* Reachability/effect counters, one line per hook: "installed" says nothing about
 * whether the rewrite ever happened. */
static int sus_mount_stat_show(char *buf, const struct kernel_param *kp)
{
    return scnprintf(buf, PAGE_SIZE,
                     "idmap=%d/%d  ident=%d/%d  hide=%d su_domain=%d\n"
                     "show_probes=%d/3 (show_vfsstat/show_mountinfo/show_vfsmnt)\n"
                     "ident: hits=%d learned_ids=%d full=%d dropped_dev=%d\n"
                     "idmap: recycled_dropped=%d dropped_dev=%d\n"
                     "sb: down=%d (probe=%d)\n"
                     "clone: walks=%d learned=%d (probe=%d)\n"
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
                     "clone_defer: queued=%d dropped=%d\n"
#endif
                     "newmount: seen=%d recorded=%d pathfail=%d (probe=%d)\n"
                     "keep: prefixes=%d rescans=%d\n"
                     "fdinfo: probes=%d entry=%d hits=%d rewrites=%d nolabel=%d\n"
                     "statx: entry=%d ret=%d hits=%d rewrites=%d nobuf=%d err=%d copyfail=%d nomap=%d "
                     "(sys=%d do=%d)\n",
                     sus_mount_idmap_live(), n_idmap,
                     sus_mount_ident_live(), READ_ONCE(n_ident), mount_registered,
                     (int)sus_mount_is_su_domain(),
                     n_show_probes,
                     atomic_read(&n_ident_hits), atomic_read(&n_ident_learned),
                     atomic_read(&n_ident_full),
                     atomic_read(&n_ident_dropped_dev),
                     atomic_read(&n_idmap_recycled),
                     atomic_read(&n_idmap_dropped_dev),
                     atomic_read(&n_sb_down), (int)kp_sb_down_ok,
                     atomic_read(&n_clone_walks), atomic_read(&n_clone_learned),
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
                     atomic_read(&n_clone_queued), atomic_read(&n_clone_dropped),
#endif
                     (int)kr_clone_ns_ok,
                     atomic_read(&n_newmnt_seen), atomic_read(&n_newmnt_recorded),
                     atomic_read(&n_newmnt_pathfail), (int)kr_newmnt_ok,
                     n_mount_keep, atomic_read(&n_keep_rescans),
                     n_fdinfo_probes,
                     atomic_read(&n_fdinfo_entry), atomic_read(&n_fdinfo_hits),
                     atomic_read(&n_fdinfo_rewrites),
                     atomic_read(&n_fdinfo_nolabel),
                     atomic_read(&n_statx_entry), atomic_read(&n_statx_ret),
                     atomic_read(&n_statx_hits), atomic_read(&n_statx_rewrites),
                     atomic_read(&n_statx_nobuf), atomic_read(&n_statx_err),
                     atomic_read(&n_statx_copyfail), atomic_read(&n_statx_nomap),
                     (int)kr_statx_ok, (int)kr_statx_do_ok);
}
static const struct kernel_param_ops sus_mount_stat_ops = {
    .get = sus_mount_stat_show,
};
module_param_cb(mount_stat, &sus_mount_stat_ops, NULL, 0400);

static int sus_mount_show_pre(struct kprobe *kp, struct pt_regs *regs)
{
    struct vfsmount *mnt = (struct vfsmount *)regs->regs[1];
    struct mount *r;

    if (!susfs_ptr_plausible(mnt))
        return 0;
    r = real_mount(mnt);
    /* Cheap path first, and NOT only the id: a KSU mount in a namespace the
     * marking scan never reached (the zygote's, hence every app's) keeps a normal
     * id - see the identity note above sus_mount_is_ours(). */
    if (!sus_mount_is_ours(r)) {
        /* This mount is not ours but carries this id right now, which is proof the
         * id was recycled if we ever learned it (see the idmap note above). */
        sus_mount_idmap_drop((int)r->mnt_id);
        /* mount_dbg: say why - the id, s_dev, root dentry and source are exactly
         * what sus_mount_is_ours() compared. */
        if (mount_dbg && atomic_inc_return(&n_dbg_logged) <= 40)
            SUSFS_LOGI("sus_mount: hook: NOT ours id=%d s_dev=%u root=%px devname=%s\n",
                    r->mnt_id, (unsigned int)r->mnt.mnt_sb->s_dev,
                    r->mnt.mnt_root, r->mnt_devname ? r->mnt_devname : "none");
        return 0;
    }
    /* domain gate, upstream patch:1561-1585: the su/ksu domain is not
     * touched at all, it must be able to see its own mounts. */
    if (sus_mount_is_su_domain())
        return 0;
    sus_mount_note_id(r);
    atomic_inc(&n_ident_hits);
    regs->pc = regs->regs[30];   /* skip this mount line */
    /* These show_* callbacks return int and x0 still holds seq_file*.
     * seq_read() treats a negative return as a hard error, so a stray high
     * bit here would break the whole read; upstream's equivalent site
     * explicitly returns 0. */
    regs->regs[0] = 0;
    return 1;
}

static struct kprobe kp_vfsstat = {
    .symbol_name = "show_vfsstat",
    .pre_handler = sus_mount_show_pre,
};

static struct kprobe kp_mountinfo = {
    .symbol_name = "show_mountinfo",
    .pre_handler = sus_mount_show_pre,
};

/* /proc/mounts and /proc/<pid>/mounts go through show_vfsmnt - a DIFFERENT
 * function from show_vfsstat (which serves mountstats).  Missing this hook left
 * /proc/mounts completely unhidden while mountinfo was filtered.  Upstream hooks
 * all three (patch:1402 susfs_show_vfsmnt, :1439 susfs_show_mountinfo, :1504
 * susfs_show_vfsstat). */
static struct kprobe kp_vfsmnt = {
    .symbol_name = "show_vfsmnt",
    .pre_handler = sus_mount_show_pre,
};

/* The three of them as one list, plus what is armed.  Registration and teardown both
 * go through this: with independent registration (see sus_mount_register()) an
 * unregister of a probe that never armed is not a no-op, so teardown must ask. */
#define SUS_MOUNT_SHOW_N 3
static struct kprobe *const sus_mount_show_probes[SUS_MOUNT_SHOW_N] = {
    &kp_vfsstat, &kp_mountinfo, &kp_vfsmnt,
};
static const char *const sus_mount_show_names[SUS_MOUNT_SHOW_N] = {
    "show_vfsstat", "show_mountinfo", "show_vfsmnt",
};
static bool sus_mount_show_armed[SUS_MOUNT_SHOW_N];

/* ---- which mounts count as "ours" ----
 *
 * One test, used by both the enable-time scan and the mount-time hook so the two cannot
 * drift: ours when the source string (mnt_devname) or the mountpoint path starts with one of
 * the PREFIXES below.  Runtime configurable - it used to be a hardcoded "/data/adb/"
 * (KernelSU's module store), which never matched a container (proot/chroot) mounting
 * tmpfs/proc/sysfs/devpts at, say, /data/local/tmp/ubuntu2/dev (see the command surface
 * below).  Anchored (strncmp), never a substring search: a devname such as
 * "/mnt/media_rw/x/data/adb/y" is not a KernelSU mount, and matching those hid unrelated
 * mounts (measured: a mount whose source was "x/data/adb/y" disappeared for non-su readers).
 * Changing the list rescans the current namespace, so an already-mounted path is picked up
 * without re-enabling (later ones come from the attach_recursive_mnt hook); entries recorded
 * before a change stay recorded - the list decides what is accepted from now on, not what is
 * already known. */

/* Interrupt/kprobe safe: read-only, no allocation. */
static bool sus_mount_path_is_ours(const char *s)
{
    unsigned long flags;
    bool hit = false;
    int i;

    if (!s)
        return false;

    spin_lock_irqsave(&mount_keep_lock, flags);
    for (i = 0; i < n_mount_keep; i++) {
        size_t len = strlen(mount_keep[i]);

        if (len && !strncmp(s, mount_keep[i], len)) {
            hit = true;
            break;
        }
    }
    spin_unlock_irqrestore(&mount_keep_lock, flags);
    return hit;
}

/* Retro-fit upstream's "KSU mounts carry an id >= DEFAULT_KSU_MNT_ID" onto the
 * mounts that already exist.  Process context only (kmalloc + d_path +
 * ida_alloc_range with GFP_KERNEL); called from module load and from the
 * supercall enable path.
 *
 * Upstream never needs this: it allocates the big id while the mount is being
 * created (susfs_alloc_non_unshare_ksu_vfsmnt(), patch:676-693, whose
 * ida_alloc_min(&mnt_id_ida, DEFAULT_KSU_MNT_ID, GFP_KERNEL) is the same
 * allocation we do here, just at a different moment).  Two properties matter:
 *   - the id is genuinely allocated from mnt_id_ida, so the ida_free() the
 *     kernel runs in mnt_free_id() (fs/namespace.c:136-139) when the mount is
 *     finally freed is paired and does not WARN (lib/idr.c:523-525).  This is
 *     the whole reason we do not invent the number;
 *   - nothing else rewrites the field, so the assignment sticks until the mount
 *     is gone (which is what upstream assumes as well), and because the id now
 *     belongs to the ida it is reused after the mount is freed - normal
 *     allocator behaviour, not a leak.
 */

/* One namespace's mount list.  Split out of the driver below because the same work
 * now has to happen in every reachable namespace, and the traversal protocol is
 * per namespace.
 *
 * fs/mount.h documents that protocol as "namespace_sem for read AND ns_lock"
 * (fs/namespace.c:704-713, :4484-4540), and WHICH half a module can take is what
 * changed in 6.12 - the two are behind SUS_MOUNT_ITER_* and
 * sus_mount_ns_walk_begin()/end(), so this loop is written once:
 *   - below 6.12 the mounts hang off ns->list, down_read() is an inline over rwsem
 *     internals and namespace_sem is static in fs/namespace.c, so only its ns_lock
 *     half is reachable: it keeps us out of the kernel's own list readers and of
 *     every list mutation that takes it (list_add/list_del under ns_lock), and the
 *     iteration bound covers the only mutation that does not (umount_tree()'s
 *     list_del_init under namespace_sem);
 *   - from 6.12 they hang off an rb-tree (ns->mounts) that the kernel iterates under
 *     namespace_sem, and ns_lock no longer guards it - so the ADDRESS of the static
 *     namespace_sem is resolved at load time and taken for read around the walk.  If
 *     that symbol cannot be resolved, sus_mount_ns_walk_begin() says so and we refuse
 *     to walk (-ENOSYS) instead of iterating a tree the kernel may rebalance under us;
 *   - either way rcu_read_lock keeps a mount that is being torn down alive: mounts
 *     that were ever here are freed through call_rcu() in cleanup_mnt()
 *     (fs/namespace.c:1144-1145), so a stale pointer picked up here cannot be
 *     reused under us.
 *
 * ID ALLOCATION happens BEFORE the lock, in a small batch: sus_mount_ida_alloc() with
 * GFP_KERNEL may allocate a radix node and therefore sleep, and this loop runs with a
 * lock held.  A batch that is not fully used is handed back to the same allocator
 * afterwards (allocated and freed there, so the pairing the kernel expects stays
 * intact). */
#define SUS_MOUNT_ID_BATCH 8
/* Passes of SUS_MOUNT_ID_BATCH each, i.e. the most mounts one enable can mark. */
#define SUS_MOUNT_SCAN_PASSES 16

/* ---- control surface: which mounts are ours ---- */

static int sus_mount_mark_ksu_mounts(void);	/* changing the list rescans */

static int sus_mount_keep_parse(const char *val, char dst[][SUS_MOUNT_KEEP_LEN], int max)
{
	const char *p = val;
	int n = 0;

	/* Scanned in place: the list is only read, never rewritten here.  It used to be copied into a
	 * SUS_MOUNT_KEEP_CMDLINE-sized stack buffer, which was fine for 8 entries and is an 8 KB stack
	 * frame for 64 - past what the kernel allows a function to use (the module is built with
	 * -Wframe-larger-than=2048).  Copying bought nothing: the callers already hand over a
	 * NUL-terminated string whose length they bounded, and trailing whitespace is skipped by the
	 * tokenizer below.
	 *
	 * Contract, now that the copy is gone: @val must be NUL-terminated and shorter than
	 * SUS_MOUNT_KEEP_CMDLINE - the tokenizer walks it directly, so a string without a terminator
	 * would be read past its end (the old internal copy made that impossible by construction, at
	 * the price of the frame above).  Every caller today rejects such a value before calling:
	 * the proc/sysfs write path and the cached insmod parameter both strscpy() into a
	 * SUS_MOUNT_KEEP_CMDLINE buffer (or refuse the value outright), so the bound holds here. */
	while (*p) {
		const char *tok;
		int len;

		while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n' || *p == '\r')
			p++;
		if (!*p)
			break;
		tok = p;
		while (*p && *p != ' ' && *p != ',' && *p != '\t' && *p != '\n' && *p != '\r')
			p++;
		len = (int)(p - tok);
		if (len <= 0)
			continue;
		if (len >= SUS_MOUNT_KEEP_LEN)
			return -ENAMETOOLONG;
		if (n >= max)
			return -ENOSPC;
		memcpy(dst[n], tok, len);
		dst[n][len] = '\0';
		n++;
	}
	return n;
}

static void sus_mount_keep_commit(char dst[][SUS_MOUNT_KEEP_LEN], int n)
{
	unsigned long flags;

	spin_lock_irqsave(&mount_keep_lock, flags);
	memset(mount_keep, 0, sizeof(mount_keep));
	memcpy(mount_keep, dst, (size_t)n * SUS_MOUNT_KEEP_LEN);
	n_mount_keep = n;
	spin_unlock_irqrestore(&mount_keep_lock, flags);
}

static void sus_mount_keep_rescan(void)
{
	int rc;

	if (!mount_registered)
		return;		/* nothing is armed yet; the scan at enable will do it */
	rc = sus_mount_mark_ksu_mounts();
	atomic_inc(&n_keep_rescans);
	SUSFS_LOGI("sus_mount: rescanned after a list change: %d mount(s) marked (rc=%d)\n",
			rc, rc < 0 ? rc : 0);
}

/* Commands, same shape as the hide_modules node: add <prefix> | del <prefix> |
 * set <prefix>... | reset | clear.  `reset` restores the built-in default (/data/adb/);
 * `clear` leaves no prefix at all, which stops NEW mounts from being accepted while the KSU
 * mounts stay hidden by their ids; @bare_list is for the insmod form of the parameter only.
 *
 * The two staging buffers used to be locals, which made this frame 2192 bytes and 6.6
 * rejects that outright - "error: stack frame size (2192) exceeds limit (2048) in
 * 'sus_mount_keep_command' [-Werror,-Wframe-larger-than]", and -Wframe-larger-than is an
 * error in the 6.6 GKI build.  They are kvmalloc'd instead: every caller here is a proc/sysfs
 * write handler or module_param setter, i.e. process context that may sleep, so GFP_KERNEL is
 * safe.  The command semantics are unchanged - the early returns now go through @out. */
static int sus_mount_keep_command(const char *val, bool bare_list)
{
	char *cmd;
	char (*staged)[SUS_MOUNT_KEEP_LEN];
	const char *arg;
	int i, n;
	int rc = 0;

	cmd = kvmalloc(SUS_MOUNT_KEEP_CMDLINE, GFP_KERNEL);
	staged = kvmalloc_array(SUS_MOUNT_KEEP_MAX, SUS_MOUNT_KEEP_LEN, GFP_KERNEL);
	if (!cmd || !staged) {
		kvfree(cmd);
		kvfree(staged);
		return -ENOMEM;
	}

	strscpy(cmd, val, SUS_MOUNT_KEEP_CMDLINE);
	for (i = (int)strlen(cmd) - 1; i >= 0 && (cmd[i] == '\n' || cmd[i] == '\r' || cmd[i] == ' '); i--)
		cmd[i] = '\0';

	if (!strcmp(cmd, "reset")) {
		strscpy(staged[0], mount_keep_default, SUS_MOUNT_KEEP_LEN);
		sus_mount_keep_commit(staged, 1);
		sus_mount_keep_rescan();
		SUSFS_LOGI("sus_mount: prefix list reset to the default\n");
		goto out;
	}
	if (!strcmp(cmd, "clear")) {
		sus_mount_keep_commit(staged, 0);
		SUSFS_LOGI("sus_mount: prefix list cleared (no new mount is accepted by path)\n");
		goto out;
	}
	if (!strncmp(cmd, "set ", 4)) {
		n = sus_mount_keep_parse(cmd + 4, staged, SUS_MOUNT_KEEP_MAX);
		if (n < 0) {
			rc = n;
			goto out;
		}
		sus_mount_keep_commit(staged, n);
		sus_mount_keep_rescan();
		SUSFS_LOGI("sus_mount: prefix list set to %d entr(ies)\n", n);
		goto out;
	}
	if (!strncmp(cmd, "add ", 4) || !strncmp(cmd, "del ", 4)) {
		bool adding = (cmd[0] == 'a');

		arg = cmd + 4;
		while (*arg == ' ')
			arg++;
		if (!*arg || strlen(arg) >= SUS_MOUNT_KEEP_LEN) {
			rc = -EINVAL;
			goto out;
		}

		spin_lock(&mount_keep_lock);
		n = n_mount_keep;
		if (n > SUS_MOUNT_KEEP_MAX)
			n = SUS_MOUNT_KEEP_MAX;
		memcpy(staged, mount_keep, (size_t)n * SUS_MOUNT_KEEP_LEN);
		spin_unlock(&mount_keep_lock);

		{
			bool found = false;

			for (i = 0; i < n; i++) {
				if (strcmp(staged[i], arg))
					continue;
				found = true;
				if (adding)
					goto out;
				memmove(&staged[i], &staged[i + 1],
					(size_t)(n - i - 1) * SUS_MOUNT_KEEP_LEN);
				n--;
				break;
			}
			if (adding) {
				if (found)
					goto out;
				if (n >= SUS_MOUNT_KEEP_MAX) {
					rc = -ENOSPC;
					goto out;
				}
				strscpy(staged[n], arg, SUS_MOUNT_KEEP_LEN);
				n++;
			} else if (!found) {
				rc = -ENOENT;
				goto out;
			}
		}
		sus_mount_keep_commit(staged, n);
		sus_mount_keep_rescan();
		SUSFS_LOGI("sus_mount: %s %s -> %d prefix(es)\n", adding ? "add" : "del", arg, n);
		goto out;
	}

	if (!bare_list) {
		rc = -EINVAL;
		goto out;
	}
	n = sus_mount_keep_parse(cmd, staged, SUS_MOUNT_KEEP_MAX);
	if (n < 0) {
		rc = n;
		goto out;
	}
	sus_mount_keep_commit(staged, n);
	sus_mount_keep_rescan();
	SUSFS_LOGI("sus_mount: prefix list set to %d entr(ies)\n", n);

out:
	kvfree(staged);
	kvfree(cmd);
	return rc;
}

/* One consistent (length, entries) pair, taken under the lock, for BOTH read paths below.  They used
 * to print straight out of the live array while sus_mount_keep_commit() was memset()ing and
 * memcpy()ing over it, so a read racing a commit could report a count that no longer matched the
 * entries it printed, or print an entry that was half old prefix and half new one.
 *
 * The caller prints from the copy and NOT under the lock: seq_printf() grows the seq buffer with
 * kvmalloc(GFP_KERNEL) once the output passes the page, and that must not happen in an atomic
 * section - which is also why the copy, not the printing, is what the lock covers.  A NULL @dst
 * reports 0 entries rather than quietly reading the live array unlocked. */
static int sus_mount_keep_snapshot(char (*dst)[SUS_MOUNT_KEEP_LEN])
{
	unsigned long flags;
	int n;

	if (!dst)
		return 0;
	spin_lock_irqsave(&mount_keep_lock, flags);
	n = n_mount_keep;
	if (n > SUS_MOUNT_KEEP_MAX)
		n = SUS_MOUNT_KEEP_MAX;
	if (n > 0)
		memcpy(dst, mount_keep, (size_t)n * SUS_MOUNT_KEEP_LEN);
	spin_unlock_irqrestore(&mount_keep_lock, flags);
	return n;
}

/* The list as one string, for the sysfs parameter read (whose buffer the kernel caps at PAGE_SIZE).
 * With 64 entries the text can exceed that, so a cut is spelled out instead of looking like the end
 * of the list - /proc/susfs_hide_mounts is the read that always shows all of them. */
static int sus_mount_keep_format(char *buf, size_t size)
{
	char (*snap)[SUS_MOUNT_KEEP_LEN];
	int nents, i, n = 0;

	snap = kvmalloc((size_t)SUS_MOUNT_KEEP_MAX * SUS_MOUNT_KEEP_LEN, GFP_KERNEL);
	if (!snap)
		return scnprintf(buf, size, "mount prefixes: (list unavailable: out of memory)\n");
	nents = sus_mount_keep_snapshot(snap);

	n += scnprintf(buf + n, size - n,
		"mount prefixes: %d/%d, rescans=%d, recorded=%d, hidden_by_identity=%d, learned_ids=%d\n",
		nents, SUS_MOUNT_KEEP_MAX, atomic_read(&n_keep_rescans),
		atomic_read(&n_newmnt_recorded), atomic_read(&n_ident_hits),
		atomic_read(&n_ident_learned));
	n += scnprintf(buf + n, size - n, "prefixes:");
	for (i = 0; i < nents && n < (int)size - SUS_MOUNT_KEEP_TRUNC_RESERVE; i++)
		n += scnprintf(buf + n, size - n, " %s", snap[i]);
	if (!nents)
		n += scnprintf(buf + n, size - n, " (none - only KSU-range ids are hidden)");
	else if (i < nents)
		n += scnprintf(buf + n, size - n, " ... (%d more; cat /proc/susfs_hide_mounts for all)",
			       nents - i);
	n += scnprintf(buf + n, size - n, "\n");
	kvfree(snap);
	return n;
}

/* The same list straight into a seq_file, which is what /proc/susfs_hide_mounts reads: no fixed
 * buffer, so 64 prefixes come back in full. */
static void sus_mount_keep_show_seq(struct seq_file *m)
{
	char (*snap)[SUS_MOUNT_KEEP_LEN];
	int nents, i;

	snap = kvmalloc((size_t)SUS_MOUNT_KEEP_MAX * SUS_MOUNT_KEEP_LEN, GFP_KERNEL);
	if (!snap) {
		seq_puts(m, "mount prefixes: (list unavailable: out of memory)\n");
		return;
	}
	nents = sus_mount_keep_snapshot(snap);

	seq_printf(m, "mount prefixes: %d/%d, rescans=%d, recorded=%d, hidden_by_identity=%d, learned_ids=%d\n",
		   nents, SUS_MOUNT_KEEP_MAX, atomic_read(&n_keep_rescans),
		   atomic_read(&n_newmnt_recorded), atomic_read(&n_ident_hits),
		   atomic_read(&n_ident_learned));
	seq_puts(m, "prefixes:");
	if (!nents)
		seq_puts(m, " (none - only KSU-range ids are hidden)");
	else
		for (i = 0; i < nents; i++)
			seq_printf(m, " %s", snap[i]);
	seq_putc(m, '\n');
	kvfree(snap);
}

/* ---- insmod parameters must not touch the guard-protected side --------------------------------
 * module_param_cb()'s setter runs during load_module()'s parse_args(), i.e. BEFORE susfs_init() has
 * reached susfs_imports_guard() and before this layer is up.  sus_mount_keep_command() is not
 * import-free: committing the list takes spin_lock_irqsave(&mount_keep_lock) and the rescan walks the
 * mount namespace - and _raw_spin_lock_irqsave is one of the imports the guard's table covers
 * (imports_guard.c), so a loader that left it at zero jumped to address 0 before anything could refuse
 * the load.  The setter therefore validates the shape and caches the raw value, and
 * susfs_sus_mount_init() applies it through the same entry point once the guard has passed.
 * mount_keep_ready marks the point after which the setter applies live, and sus_mount_keep_param_lock
 * keeps the two orders from interleaving (the commit replaces the list, so a write landing between
 * "ready = true" and the cached apply must not be clobbered by the older value).  (That guard table is
 * not the whole import set: ordinary exports such as strscpy/kvmalloc are the loader's and the
 * build-time System.map gate's business, so the validation below may use them.)
 *
 * One entry of that table is still reached from here on purpose: sus_mount_keep_param_lock is taken in
 * parse_args(), and mutex_lock/mutex_unlock are in that table as well - so a loader that left those two
 * at zero would jump to address 0 before susfs_init() could refuse the load.  It is not removable by a
 * local change: the mutex is what orders "cache the value" against "apply the cached value", and the
 * other mutual-exclusion primitives available here (_raw_spin_lock*, __rcu_read_*) are in that table
 * too.  Closing it needs a lock-free publication of the cached value (store, then re-check the ready
 * flag and apply live), which is a change of its own. */
static char mount_keep_pending[SUS_MOUNT_KEEP_CMDLINE];
static bool mount_keep_pending_given;
static bool mount_keep_ready;
static DEFINE_MUTEX(sus_mount_keep_param_lock);

/* Validation only, through the same parser the command uses (limits and error codes cannot drift).
 * No lock, no commit, no rescan - safe to call from parse_args(). */
static int sus_mount_keep_list_check(const char *list)
{
	char (*dst)[SUS_MOUNT_KEEP_LEN];
	int rc;

	dst = kvmalloc_array(SUS_MOUNT_KEEP_MAX, SUS_MOUNT_KEEP_LEN, GFP_KERNEL);
	if (!dst)
		return -ENOMEM;
	rc = sus_mount_keep_parse(list, dst, SUS_MOUNT_KEEP_MAX);
	kvfree(dst);
	return rc < 0 ? rc : 0;
}

/* The grammar sus_mount_keep_command() accepts, checked without running it: `reset`, `clear`,
 * `set <list>`, `add <prefix>`, `del <prefix>`, or - the insmod form - a bare list.  @val is modified
 * in place (trailing whitespace trimmed), which is why the caller hands over its own copy. */
static int sus_mount_keep_check(char *val)
{
	const char *arg;
	size_t i;

	for (i = strlen(val); i > 0 && (val[i - 1] == '\n' || val[i - 1] == '\r' || val[i - 1] == ' '); i--)
		val[i - 1] = '\0';

	/* An empty value is not "set the list to nothing": that is what `clear` says, and silently emptying
	 * the prefix list (which the built-in default otherwise seeds) is not something a typo should do. */
	if (!val[0])
		return -EINVAL;
	if (!strcmp(val, "reset") || !strcmp(val, "clear"))
		return 0;
	if (!strncmp(val, "set ", 4))
		return sus_mount_keep_list_check(val + 4);
	if (!strncmp(val, "add ", 4) || !strncmp(val, "del ", 4)) {
		arg = val + 4;
		while (*arg == ' ')
			arg++;
		if (!*arg || strlen(arg) >= SUS_MOUNT_KEEP_LEN)
			return -EINVAL;
		/* parse_args runs before the list has any entry, so `del` could only fail later (-ENOENT). */
		if (val[0] == 'd')
			return -EINVAL;
		return 0;
	}
	return sus_mount_keep_list_check(val);
}

/* The same trailing-whitespace rule the command applies ("echo" leaves a "\n" behind), used to tell an
 * empty value from a real one on the live path as well. */
static bool sus_mount_keep_value_empty(const char *val)
{
	size_t i;

	for (i = strlen(val); i > 0; i--)
		if (val[i - 1] != '\n' && val[i - 1] != '\r' && val[i - 1] != ' ')
			return false;
	return true;
}

static int sus_mount_keep_param_set(const char *val, const struct kernel_param *kp)
{
	int rc;

	mutex_lock(&sus_mount_keep_param_lock);
	if (mount_keep_ready) {
		bool empty = sus_mount_keep_value_empty(val);
		bool too_long = strlen(val) >= SUS_MOUNT_KEEP_CMDLINE;

		mutex_unlock(&sus_mount_keep_param_lock);
		/* An empty value is not "set the list to nothing" - that is what `clear` says, and the
		 * pre-init path refuses it for the same reason (see sus_mount_keep_check()).  A value that
		 * does not fit the command buffer is refused too: its tail is cut off, and the cut can leave
		 * an empty command behind, i.e. the same silent clear. */
		return (empty || too_long) ? -EINVAL : sus_mount_keep_command(val, true);
	}

	/* parse_args(): validate and cache only - see mount_keep_pending.  Validate the value itself, not a
	 * copy that was already truncated to the cache size. */
	if (strlen(val) >= sizeof(mount_keep_pending)) {
		mutex_unlock(&sus_mount_keep_param_lock);
		return -EINVAL;
	}
	strscpy(mount_keep_pending, val, sizeof(mount_keep_pending));
	rc = sus_mount_keep_check(mount_keep_pending);
	if (!rc)
		mount_keep_pending_given = true;
	mutex_unlock(&sus_mount_keep_param_lock);
	return rc;
}

static int sus_mount_keep_param_get(char *buf, const struct kernel_param *kp)
{
	return sus_mount_keep_format(buf, PAGE_SIZE);
}

static const struct kernel_param_ops sus_mount_keep_ops = {
	.get = sus_mount_keep_param_get,
	.set = sus_mount_keep_param_set,
};
/* 0600: root only; the whole directory is inside the one hide_modules hides. */
module_param_cb(hide_mounts, &sus_mount_keep_ops, NULL, 0600);

static int sus_mount_keep_proc_show(struct seq_file *m, void *v)
{
	sus_mount_keep_show_seq(m);
	return 0;
}

static int sus_mount_keep_proc_open(struct inode *inode, struct file *file)
{
	/* 0777 node + this check, like every other control node: a restrictive mode would
	 * answer EACCES (advertising that the node exists) before sus_path could answer
	 * ENOENT. */
	if (!susfs_control_visible())
		return -ENOENT;
	return single_open(file, sus_mount_keep_proc_show, NULL);
}

static ssize_t sus_mount_keep_proc_write(struct file *file, const char __user *buf,
					 size_t len, loff_t *off)
{
	char *cmd;
	int rc;

	/* Same reason as the open check: an fd opened before the process dropped
	 * privileges must not become a way in. */
	if (!susfs_control_visible())
		return -ENOENT;
	if (len == 0)
		return 0;
	/* On the heap, not on the stack: one write may carry the whole list, so the buffer is sized for
	 * SUS_MOUNT_KEEP_MAX prefixes - 8 KB now that the list holds 64, and no function may use that
	 * much stack. */
	if (len >= SUS_MOUNT_KEEP_CMDLINE)
		return -EINVAL;
	cmd = kvmalloc(len + 1, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;
	if (copy_from_user(cmd, buf, len)) {
		kvfree(cmd);
		return -EFAULT;
	}
	cmd[len] = '\0';

	/* Commands only: a typo must not silently replace the list. */
	rc = sus_mount_keep_command(cmd, false);
	kvfree(cmd);
	if (rc)
		return rc;
	return len;
}

static const struct proc_ops sus_mount_keep_proc_ops = {
	.proc_open = sus_mount_keep_proc_open,
	.proc_read = seq_read,
	.proc_write = sus_mount_keep_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static struct proc_dir_entry *sus_mount_keep_entry;

static int sus_mount_scan_ns(struct mnt_namespace *ns, char *buf, unsigned long min,
                             bool *more)
{
    int batch[SUS_MOUNT_ID_BATCH];
    /* The ids the mounts already owned, so they can be handed back to the kernel's ida once
     * the locks are dropped.  Overwriting r->mnt_id without returning the old value leaked one
     * id per marked mount: mnt_free_id() at teardown frees whatever the field holds THEN (the
     * new one), so the original ida_alloc never had a matching ida_free.  Same size as the
     * batch, because at most one old id is produced per id consumed. */
    int replaced[SUS_MOUNT_ID_BATCH];
    int n_batch = 0, used = 0, n_replaced = 0;
    SUS_MOUNT_ITER_TYPE pos;
    unsigned int seen = 0;
    int scan_logged = 0;
    int marked = 0;
    unsigned int n_devname = 0, n_dpath_ok = 0, n_dpath_err = 0;
    *more = false;
    unsigned int n_skipped_ns = 0, n_skipped_marked = 0;
    bool hit_cap = false;
    int i;

    if (!sus_mount_ns_walk_begin()) {
        pr_warn("sus_mount: namespace_sem could not be resolved at load time - refusing to walk ns %p without the lock the kernel's own mount-table iterator holds (mounts stay unmarked)\n",
                ns);
        return -ENOSYS;
    }

    for (i = 0; i < SUS_MOUNT_ID_BATCH; i++) {
        int id = sus_mount_ida_alloc();

        if (id < 0)
            break;
        if (id < (int)DEFAULT_KSU_MNT_ID) {
            /* Below our floor: the resolved mnt_id_ida is not what we think it is.
             * Hand this id back and stop allocating. */
            sus_mount_ida_release(id);
            break;
        }
        batch[n_batch++] = id;
    }
    if (!n_batch) {
        pr_warn("sus_mount: could not allocate KSU-range ids for ns %p - that namespace is left unmarked\n",
                ns);
        sus_mount_ns_walk_end();
        return 0;
    }

    rcu_read_lock();
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
    spin_lock(&ns->ns_lock);
#endif
    SUS_MOUNT_ITER_FOR(ns, pos) {
        struct path mnt_path;
        struct mount *r;
        const char *shown;
        char *dp;
        int new_id;

        if (seen++ >= SUS_MOUNT_MAX_SCAN) {
            hit_cap = true;
            break;
        }
        r = SUS_MOUNT_ITER_MOUNT(pos);
        /* proc_mounts cursors are fake mounts anchored in this same list
         * (fs/namespace.c:678-681 mnt_is_cursor(), include/linux/mount.h:70): below
         * 6.12 MNT_CURSOR is what keeps them out of the walk, from 6.12 on they are
         * simply not in ns->mounts - both are what SUS_MOUNT_MNT_NOT_IN_NS() tests. */
        if (SUS_MOUNT_MNT_NOT_IN_NS(r, ns)) {
            n_skipped_ns++;
            continue;
        }
        /* Anything already carrying a KSU-range id: the idempotency guard for a re-enable (a
         * second id for the same mount would leak the first for its lifetime) AND the cover
         * for KernelSU's own mounts, which this kernel already hands such an id to - measured
         * on this device: exactly one, the meta-overlayfs loop mount at 2000000000.  Their
         * line is skipped by the id alone, so they need the same "id the app may see" mapping
         * or fdinfo/statx print a number mountinfo no longer lists.  Compares against the
         * constant, not the tunable (SUS_MOUNT_KSU_ID_MIN), and records the identity as well:
         * this namespace's mount is a different OBJECT from the same filesystem's mount in the
         * next namespace (measured: 2000000000 here, 1111 in the zygote's). */
        if ((unsigned int)r->mnt_id >= SUS_MOUNT_KSU_ID_MIN) {
            n_skipped_marked++;
            sus_mount_idmap_add((int)r->mnt_id, sus_mount_shown_id(r), r->mnt.mnt_sb->s_dev);
            sus_mount_ident_add(r);
            continue;
        }

        if (sus_mount_path_is_ours(r->mnt_devname)) {
            n_devname++;
            shown = r->mnt_devname;
        } else {
            /* meta-overlayfs style: the source is /dev/block/loopNN, so only the
             * mount point says /data/adb/... .  d_path() of {mnt, mnt_root} is the
             * mountpoint path show_mountinfo() prints. */
            mnt_path.mnt = &r->mnt;
            mnt_path.dentry = r->mnt.mnt_root;
            dp = sus_mount_d_path(&mnt_path, buf, PATH_MAX);
            /* Diagnostic while the matching rule is being validated: the first few
             * mounts show what d_path() actually renders for them. */
            if (scan_logged < 40) {
                scan_logged++;
                SUSFS_LOGI("sus_mount: scan %s -> %s\n", r->mnt_devname,
                        IS_ERR_OR_NULL(dp) ? "(d_path failed)" : dp);
            }
            if (IS_ERR_OR_NULL(dp)) {
                n_dpath_err++;
                continue;
            }
            n_dpath_ok++;
            if (!sus_mount_path_is_ours(dp))
                continue;
            shown = dp;
        }

        if (used >= n_batch) {
            /* Not an error: the batch is allocated before the lock (ida_alloc_range() may
             * sleep) and so bounds one pass, not the scan.  The caller runs another pass. */
            *more = true;
            break;
        }
        new_id = batch[used++];
        SUSFS_LOGI("sus_mount: marked mnt_id %d -> %d (%s, devname %s)\n",
                r->mnt_id, new_id, shown,
                r->mnt_devname ? r->mnt_devname : "none");
        if (n_replaced < (int)ARRAY_SIZE(replaced))
            replaced[n_replaced++] = r->mnt_id;
        r->mnt_id = new_id;
        /* After the id is replaced, exactly like upstream: the climb starts at a
         * mount that now carries a KSU-range id and stops at the first ancestor
         * that does not - i.e. the id mountinfo still prints for the host. */
        sus_mount_idmap_add(new_id, sus_mount_shown_id(r), r->mnt.mnt_sb->s_dev);
        /* And the identity, so the same filesystem mounted in another namespace
         * (where this scan cannot reach) is recognised by the hide hooks. */
        sus_mount_ident_add(r);
        marked++;
    }
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
    spin_unlock(&ns->ns_lock);
#endif
    rcu_read_unlock();
    sus_mount_ns_walk_end();

    /* Hand back whatever the batch did not use. */
    for (i = used; i < n_batch; i++)
        sus_mount_ida_release(batch[i]);

    /* ... and the ids the marked mounts used to own: the kernel frees r->mnt_id at teardown,
     * which is now the new id, so without this the original id is lost for good. */
    for (i = 0; i < n_replaced; i++)
        sus_mount_ida_release(replaced[i]);

    if (hit_cap)
        pr_warn("sus_mount: walk of ns %p stopped after %u entries (cap %d), result may be incomplete\n",
                ns, seen, SUS_MOUNT_MAX_SCAN);
    SUSFS_LOGI("sus_mount: ns %p: seen=%u devname_hits=%u dpath_ok=%u dpath_err=%u skipped(other ns/cursor)=%u skipped(already marked)=%u marked=%d ids_alloc=%d\n",
            ns, seen, n_devname, n_dpath_ok, n_dpath_err, n_skipped_ns,
            n_skipped_marked, marked, n_batch);
    return marked;
}

static int sus_mount_mark_ksu_mounts(void)
{
    char *buf;
    unsigned long min;
    int marked = 0;
    int pass;
    bool more = false;

    /* Fail closed: without all three symbols we cannot own a real id, and a
     * self-made id would leave an ida_free WARN behind on umount. */
    if (!sus_mount_ida_ready()) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
        pr_warn("sus_mount: NOT marking: mnt_id_xa=%s __xa_alloc=%s __xa_erase=%s must all be supplied; min_mnt_id stays false, so the feature does nothing\n",
                sus_mount_xa_obj_src, sus_mount_xa_alloc_src, sus_mount_xa_erase_src);
#else
        pr_warn("sus_mount: NOT marking: mnt_id_ida=%d ida_alloc_range=%d ida_free=%d must all resolve; min_mnt_id stays false, so the feature does nothing\n",
                !!sus_mount_mnt_id_ida, !!pfn_ida_alloc_range, !!pfn_ida_free);
#endif
        return -ENOSYS;
    }

    if (!current->nsproxy || !current->nsproxy->mnt_ns) {
        pr_warn("sus_mount: current has no mnt_ns, cannot scan for KSU mounts\n");
        return -ENOENT;
    }

    /* clamp the tunable (a value of 0/1 would match every mount line). */
    if (param_min_mnt_id < SUS_MOUNT_MIN_SANE_MNT_ID) {
        pr_warn("sus_mount: min_mnt_id=%lu is below %d, clamping to %llu\n",
                param_min_mnt_id, SUS_MOUNT_MIN_SANE_MNT_ID, DEFAULT_KSU_MNT_ID);
        param_min_mnt_id = DEFAULT_KSU_MNT_ID;
    }
    min = sus_mount_min_mnt_id();

    buf = kmalloc(PATH_MAX, GFP_KERNEL);
    if (!buf) {
        pr_warn("sus_mount: kmalloc(PATH_MAX) failed, no KSU mount marked\n");
        return -ENOMEM;
    }

    /* ONE namespace: the caller's.  A sweep over every namespace reachable from the
     * task list is NOT done here: it would walk mount tables that other tasks are
     * mounting into and umounting from, and from 6.12 that means holding namespace_sem
     * for read across an unbounded number of foreign namespaces (below 6.12 only the
     * ns_lock half is reachable at all - see the protocol note above sus_mount_scan_ns).
     * The one foreign namespace that IS walked is the copy copy_mnt_ns() has just built
     * for the caller, from its own probe - see the clone note above.
     *
     * The zygote's copy of a KSU mount (the one every app inherits) is reached
     * through the identity table instead: same superblock, same root dentry, and
     * the hide hooks accept that as well as a KSU-range id, with no cross-namespace
     * walk at all.  That is what closes the leak this file's header describes. */
    /* One pass is bounded by SUS_MOUNT_ID_BATCH, because the ids have to be allocated before
     * ns_lock is taken (ida_alloc_range() may sleep).  A module store with more bind mounts
     * than that left the rest unmarked and visible in /proc/mounts, so run passes until the
     * scan reports nothing more to mark - capped, so a namespace that grows under us cannot
     * spin here. */
    for (pass = 0; pass < SUS_MOUNT_SCAN_PASSES; pass++) {
        int n = sus_mount_scan_ns(current->nsproxy->mnt_ns, buf, min, &more);

        if (n > 0)
            marked += n;
        if (!more)
            break;
    }
    if (more)
        pr_warn("sus_mount: still more mounts to mark after %d passes (%d marked); raise SUS_MOUNT_SCAN_PASSES or SUS_MOUNT_ID_BATCH\n",
                SUS_MOUNT_SCAN_PASSES, marked);

    kfree(buf);

    if (marked > 0)
        SUSFS_LOGI("sus_mount: %d KSU mount(s) marked with real ids from the kernel's own mount-id allocator (>= %llu), %d identity record(s) cached\n",
                marked, DEFAULT_KSU_MNT_ID, READ_ONCE(n_ident));
    else if (marked < 0)
        SUSFS_LOGI("sus_mount: scan refused (%d), nothing marked - the reason is the pr_warn above\n",
                marked);
    else
        SUSFS_LOGI("sus_mount: 0 KSU mounts marked (nothing under /data/adb matched in this mnt ns, hide threshold %lu)\n",
                min);
    return marked;
}

int susfs_sus_mount_init(void)
{
    int err;

    pfn_security_cred_getsecid =
        (void *)find_kernel_symbol_exact("security_cred_getsecid");
    pfn_d_path = (void *)find_kernel_symbol_exact("d_path");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
    void *loader_xa = (void *)&mnt_id_xa;
    void *loader_alloc = (void *)&__xa_alloc;
    void *loader_erase = (void *)&__xa_erase;

    sus_mount_mnt_id_xa = (struct xarray *)find_kernel_symbol_exact("mnt_id_xa");
    if (sus_mount_mnt_id_xa) {
        sus_mount_xa_obj_src = "kallsyms";
        if (loader_xa && loader_xa != (void *)sus_mount_mnt_id_xa)
            pr_warn("sus_mount: mnt_id_xa: kallsyms %px != loader %px\n",
                    sus_mount_mnt_id_xa, loader_xa);
    } else if (loader_xa) {
        sus_mount_mnt_id_xa = loader_xa;
        sus_mount_xa_obj_src = "loader";
    }

    pfn_xa_alloc = (void *)find_kernel_symbol_exact("__xa_alloc");
    if (pfn_xa_alloc) {
        sus_mount_xa_alloc_src = "kallsyms";
        if (loader_alloc && loader_alloc != (void *)pfn_xa_alloc)
            pr_warn("sus_mount: __xa_alloc: kallsyms %px != loader %px\n",
                    (void *)pfn_xa_alloc, loader_alloc);
    } else if (loader_alloc) {
        pfn_xa_alloc = loader_alloc;
        sus_mount_xa_alloc_src = "loader";
    }

    pfn_xa_erase = (void *)find_kernel_symbol_exact("__xa_erase");
    if (pfn_xa_erase) {
        sus_mount_xa_erase_src = "kallsyms";
        if (loader_erase && loader_erase != (void *)pfn_xa_erase)
            pr_warn("sus_mount: __xa_erase: kallsyms %px != loader %px\n",
                    (void *)pfn_xa_erase, loader_erase);
    } else if (loader_erase) {
        pfn_xa_erase = loader_erase;
        sus_mount_xa_erase_src = "loader";
    }
#else
    sus_mount_mnt_id_ida = (struct ida *)find_kernel_symbol_exact("mnt_id_ida");
    pfn_ida_alloc_range = (void *)find_kernel_symbol_exact("ida_alloc_range");
    pfn_ida_free = (void *)find_kernel_symbol_exact("ida_free");
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
    sus_mount_namespace_sem = (struct rw_semaphore *)find_kernel_symbol_exact("namespace_sem");
    pfn_put_mnt_ns = (void *)find_kernel_symbol_exact("put_mnt_ns");
#endif

    err = security_secctx_to_secid(param_su_ctx, strlen(param_su_ctx), &su_sid);
    if (err) {
        pr_warn("sus_mount: secctx_to_secid(%s) failed %d\n", param_su_ctx, err);
        su_sid = 0;
    }

    if (param_min_mnt_id < SUS_MOUNT_MIN_SANE_MNT_ID) {
        pr_warn("sus_mount: min_mnt_id=%lu is below %d, clamping to %llu\n",
                param_min_mnt_id, SUS_MOUNT_MIN_SANE_MNT_ID, DEFAULT_KSU_MNT_ID);
        param_min_mnt_id = DEFAULT_KSU_MNT_ID;
    }

    SUSFS_LOGI("sus_mount: su ctx \"%s\" -> sid %u (stock KernelSU uses \"u:r:su:s0\", override with susfs_guard_lkm.su_ctx)\n",
            param_su_ctx, su_sid);
    if (!pfn_security_cred_getsecid)
        pr_warn("sus_mount: security_cred_getsecid not found - no su-domain gating, KSU mounts will be hidden from EVERY process including su\n");
    if (!pfn_d_path)
        pr_warn("sus_mount: d_path not found - only mnt_devname is checked, meta-overlayfs style mounts will NOT be marked\n");
    /* The id side must own real ids; each missing symbol is named explicitly and
     * only disables the marking (the hook itself can still be installed). */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
    SUSFS_LOGI("sus_mount: 6.18 mount-id allocator: mnt_id_xa via %s, __xa_alloc via %s, __xa_erase via %s\n",
            sus_mount_xa_obj_src, sus_mount_xa_alloc_src, sus_mount_xa_erase_src);
    if (!sus_mount_mnt_id_xa)
        pr_warn("sus_mount: mnt_id_xa not supplied (neither kallsyms nor the loader has the name) - KSU mounts will NOT be marked, threshold stays false (feature does nothing)\n");
    if (!pfn_xa_alloc)
        pr_warn("sus_mount: __xa_alloc not supplied - KSU mounts will NOT be marked, threshold stays false (feature does nothing)\n");
    if (!pfn_xa_erase)
        pr_warn("sus_mount: __xa_erase not supplied - KSU mounts will NOT be marked, threshold stays false (feature does nothing)\n");
#else
    if (!sus_mount_mnt_id_ida)
        pr_warn("sus_mount: mnt_id_ida not found - KSU mounts will NOT be marked, threshold stays false (feature does nothing)\n");
    if (!pfn_ida_alloc_range)
        pr_warn("sus_mount: ida_alloc_range not found - KSU mounts will NOT be marked, threshold stays false (feature does nothing)\n");
    if (!pfn_ida_free)
        pr_warn("sus_mount: ida_free not found - KSU mounts will NOT be marked, threshold stays false (feature does nothing)\n");
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
    if (!sus_mount_namespace_sem)
        pr_warn("sus_mount: namespace_sem not found - the namespace scan will refuse to walk, so KSU mounts will NOT be marked (feature does nothing)\n");
    if (!pfn_put_mnt_ns)
        pr_warn("sus_mount: put_mnt_ns not found - a namespace cloned after the enable is not recorded, so its mounts' ids are only learned by a later scan\n");
#endif

    /* upstream defaults this OFF (static key false) so zygisk can see sus
     * mounts during post-fs-data; the LKM mirrors that: no hook until
     * CMD_SUSFS_HIDE_SUS_MNTS_FOR_NON_SU_PROCS enables it.  The id assignment
     * itself is not gated on that flag - the hook compares ids, so the mounts
     * have to carry KSU ids before it is switched on (and the next enable
     * rescans anyway, which picks up mounts created since load). */
    SUSFS_LOGI("sus_mount: disabled by default (enable via CMD_SUSFS_HIDE_SUS_MNTS_FOR_NON_SU_PROCS)\n");

    /* Which mounts count as ours, before anything looks at them: the built-in default
     * is KernelSU's module store.  The list is runtime configurable (hide_mounts /
     * /proc/susfs_hide_mounts) - a container at /data/local/tmp/ubuntu2 needs its own
     * prefix to be hidden at all. */
    {
        /* One entry is all this needs: an array sized for the whole list would be 8 KB of stack here
         * (SUS_MOUNT_KEEP_MAX is 64 now), and commit() only copies the one row it is told about. */
        char staged[1][SUS_MOUNT_KEEP_LEN] = { { 0 } };

        strscpy(staged[0], mount_keep_default, SUS_MOUNT_KEEP_LEN);
        sus_mount_keep_commit(staged, 1);
    }

    (void)sus_mount_mark_ksu_mounts();

    if (susfs_control_node_allowed()) {
        sus_mount_keep_entry = proc_create("susfs_hide_mounts", 0777, NULL,
                           &sus_mount_keep_proc_ops);
        if (!sus_mount_keep_entry)
            pr_warn("sus_mount: proc_create(susfs_hide_mounts) failed - runtime prefix control unavailable, use the hide_mounts parameter\n");
    } else {
        SUSFS_LOGI("sus_mount: /proc/susfs_hide_mounts not created (expose_proc=%d lsm=%d)\n",
                (int)susfs_expose_proc, (int)sus_path_lsm_active());
    }

    /* Apply what the insmod parameter cached (see mount_keep_pending): the import guard has passed and
     * this layer is up.  Applied AFTER the default seed above on purpose - an explicit
     * hide_mounts=<list> is what the operator asked for, instead of being silently replaced by the
     * built-in default (the same shape as hide_modules in susfs_hide_syms.c).  From here on the setter
     * applies live. */
    mutex_lock(&sus_mount_keep_param_lock);
    mount_keep_ready = true;
    if (mount_keep_pending_given) {
        int rc = sus_mount_keep_command(mount_keep_pending, true);

        if (rc)
            pr_warn("sus_mount: applying hide_mounts=\"%s\" from insmod failed %d - that prefix list is not in effect\n",
                    mount_keep_pending, rc);
    }
    mutex_unlock(&sus_mount_keep_param_lock);
    return 0;
}

/* One unregister path for both callers (module exit and the disable supercall):
 * two copies drifted apart once already in this project, leaving a hook armed
 * after "disabled". */
static void sus_mount_unregister(void)
{
    int i;

    /* Hold the control mutex across the check and the unregistering: see its definition for
     * why two concurrent arm/disarm paths cannot be allowed to interleave here. */
    mutex_lock(&sus_mount_ctl_lock);
    if (!mount_registered) {
        mutex_unlock(&sus_mount_ctl_lock);
        return;
    }
    /* Per probe: with independent registration the list can be partial, and
     * unregister_kprobe() on a probe that never armed walks lists it is not on. */
    for (i = 0; i < SUS_MOUNT_SHOW_N; i++) {
        if (!sus_mount_show_armed[i])
            continue;
        unregister_kprobe(sus_mount_show_probes[i]);
        /* Hand the struct back before the flag says it is disarmed: it was registered by
         * .symbol_name, and the address the kernel resolved survives a successful
         * unregister, so the next enable would be refused with -EINVAL - see
         * susfs_kprobe.h.  This is the one path that made "hide, unhide, hide again" a
         * one-way switch: the second hide armed nothing and reported -EINVAL. */
        susfs_kp_forget_addr(sus_mount_show_probes[i]);
        sus_mount_show_armed[i] = false;
    }
    n_show_probes = 0;
    if (kr_fdinfo_ok)
        sus_mount_fdinfo_disarm();
    if (kr_statx_ok) {
        unregister_kretprobe(&kr_statx);
        susfs_krp_forget_addr(&kr_statx);
        kr_statx_ok = false;
    }
    if (kr_statx_do_ok) {
        unregister_kretprobe(&kr_statx_do);
        susfs_krp_forget_addr(&kr_statx_do);
        kr_statx_do_ok = false;
    }
    if (kp_sb_down_ok) {
        unregister_kprobe(&kp_sb_down);
        susfs_kp_forget_addr(&kp_sb_down);
        kp_sb_down_ok = false;
    }
    if (kr_clone_ns_ok) {
        unregister_kretprobe(&kr_clone_ns);
        susfs_krp_forget_addr(&kr_clone_ns);
        kr_clone_ns_ok = false;
    }
    if (kr_newmnt_ok) {
        unregister_kretprobe(&kr_newmnt);
        susfs_krp_forget_addr(&kr_newmnt);
        kr_newmnt_ok = false;
    }
    mount_registered = false;
    mutex_unlock(&sus_mount_ctl_lock);
}

void susfs_sus_mount_exit(void)
{
    unsigned long flags;

    if (sus_mount_keep_entry) {
        proc_remove(sus_mount_keep_entry);
        sus_mount_keep_entry = NULL;
    }
    sus_mount_unregister();
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
    sus_mount_learn_stop();
#endif
    /* Marked mnt_ids are deliberately NOT restored: upstream assigns an id once
     * per mount and never rewrites it, so a marked id stays for the mount's
     * lifetime (and a later enable only has to scan for new mounts).  The id
     * itself goes back to mnt_id_ida through the kernel's own mnt_free_id()
     * when the mount is finally freed - we never free it ourselves. */

    /* Nothing to release: an identity record is numbers and a string, it holds no
     * reference on the dentry or the superblock (see the note above), precisely so
     * an unmounted module image cannot be kept alive by this table.  The records
     * themselves stay; after exit nothing compares against them (the hide hooks are
     * unregistered above) and a later enable re-learns them. */
    WRITE_ONCE(n_ident, 0);

    /* The id map, on the other hand, IS dropped: it is keyed by an id the kernel
     * recycles, and for as long as the module is disabled nothing observes a
     * recycle.  A re-enable re-learns every pair from the mounts it scans. */
    spin_lock_irqsave(&idmap_lock, flags);
    memset(mount_idmap, 0, sizeof(mount_idmap));
    n_idmap = 0;
    spin_unlock_irqrestore(&idmap_lock, flags);
}

static int sus_mount_register(void)
{
    int rc, i;

    mutex_lock(&sus_mount_ctl_lock);
    if (mount_registered) {
        mutex_unlock(&sus_mount_ctl_lock);
        return 0;
    }

    /* The three mount-table hooks, registered INDEPENDENTLY.  They used to be fatal on the
     * first failure and silent about which one it was: -EINVAL from register_kprobe() (its
     * answer when the address is not probeable) came back to userspace as a bare -EINVAL with
     * nothing in the log - measured on a vendor 5.15 kernel, where enabling failed that way
     * while every mount table stayed unhidden, and only the source said which probe it was.
     *
     * Each hook covers a different file - show_vfsstat serves /proc/<pid>/mountstats,
     * show_mountinfo mountinfo, show_vfsmnt /proc/mounts and /proc/<pid>/mounts - so one that
     * cannot be armed must not take the other two down.  Every failure is named and the number
     * armed is on mount_stat as `show_probes=<n>/3`, which makes a partial hide visible instead
     * of silently smaller.  Only "none of the three" is fatal. */
    for (i = 0; i < SUS_MOUNT_SHOW_N; i++) {
        rc = register_kprobe(sus_mount_show_probes[i]);
        if (rc) {
            pr_warn("sus_mount: register_kprobe(%s) failed %d - that file keeps showing our mounts\n",
                    sus_mount_show_names[i], rc);
            /* A failure after address resolution leaves .addr set, so the retry a later enable
             * performs has to find the struct clear or it is refused with -EINVAL without
             * ever reaching the symbol again - see susfs_kprobe.h. */
            susfs_kp_forget_addr(sus_mount_show_probes[i]);
            continue;
        }
        sus_mount_show_armed[i] = true;
        n_show_probes++;
    }
    if (!n_show_probes) {
        pr_err("sus_mount: none of show_vfsstat/show_mountinfo/show_vfsmnt could be hooked - not reporting the feature as enabled\n");
        mutex_unlock(&sus_mount_ctl_lock);
        return -EINVAL;
    }
    /* The two id rewrites are optional on their own: without them the mount
     * lines are still hidden, so a missing symbol must not take the rest down -
     * but each failure is named, because it leaves the ids visible in exactly
     * the place upstream rewrites them. */
    rc = sus_mount_fdinfo_arm();
    if (rc)
        pr_warn("sus_mount: the fdinfo probe could not be armed %d - /proc/<pid>/fdinfo keeps printing the real mnt_id\n", rc);
    rc = register_kretprobe(&kr_statx);
    if (rc) {
        susfs_krp_forget_addr(&kr_statx);
        pr_warn("sus_mount: register_kretprobe(__arm64_sys_statx) failed %d - statx keeps returning the real stx_mnt_id\n", rc);
    } else {
        kr_statx_ok = true;
    }
    rc = register_kretprobe(&kr_statx_do);
    if (rc) {
        susfs_krp_forget_addr(&kr_statx_do);
        pr_warn("sus_mount: register_kretprobe(do_statx) failed %d - the second statx landing point is not armed\n", rc);
    } else {
        kr_statx_do_ok = true;
    }
    /* Optional too, but it is the ONLY thing that invalidates a record when its
     * filesystem is unmounted, and the numbers the records are keyed by do get
     * reused (measured).  A missing symbol means records can outlive their fs and
     * falsely match another one, so say so instead of quietly degrading. */
    rc = register_kprobe(&kp_sb_down);
    if (rc) {
        susfs_kp_forget_addr(&kp_sb_down);
        pr_warn("sus_mount: register_kprobe(generic_shutdown_super) failed %d - records are NOT dropped when their filesystem is unmounted, a reused s_dev can match an unrelated mount\n",
                rc);
    } else {
        kp_sb_down_ok = true;
    }
    /* Optional as well: without it a namespace copied after the enable keeps
     * working for the mount TABLE (identity hides the lines) but fdinfo/statx stay
     * inconsistent until somebody reads a mount table in that namespace. */
    rc = register_kretprobe(&kr_clone_ns);
    if (rc) {
        susfs_krp_forget_addr(&kr_clone_ns);
        pr_warn("sus_mount: register_kretprobe(copy_mnt_ns) failed %d - ids of mounts in a namespace copied later are only learned when its mount table is read\n",
                rc);
    } else {
        kr_clone_ns_ok = true;
    }
    /* And the mount-time registration, which is what covers a filesystem mounted while
     * the module is already up (see the note above kr_newmnt).  Also optional in the
     * sense that hiding keeps working for everything the scan saw - but without it a
     * mount that appears later is not hidden anywhere, so a failure is reported. */
    rc = register_kretprobe(&kr_newmnt);
    if (rc) {
        susfs_krp_forget_addr(&kr_newmnt);
        pr_warn("sus_mount: register_kretprobe(attach_recursive_mnt) failed %d - a mount created after the enable is NOT hidden (not marked, no identity recorded)\n",
                rc);
    } else {
        kr_newmnt_ok = true;
    }
    mount_registered = true;
    mutex_unlock(&sus_mount_ctl_lock);
    return 0;
}

/* supercall: CMD_SUSFS_HIDE_SUS_MNTS_FOR_NON_SU_PROCS */
void susfs_sus_mount_supercall(void __user **arg)
{
    struct st_susfs_hide_sus_mnts_for_non_su_procs info = {0};
    int rc;

    if (copy_from_user(&info, (void __user *)*arg, sizeof(info))) {
        info.err = -EFAULT;
        goto out;
    }

    if (info.enabled) {
        rc = sus_mount_register();
        if (rc) {
            info.err = rc;
            goto out;
        }
        /* Only now does the threshold matter, so mark the KSU mounts (this also catches
         * everything mounted since the module was loaded).  A failed scan is REPORTED, not just
         * logged: with no mount carrying a KSU-range id the threshold never matches, so nothing
         * is hidden while the hook stays live - "enabled, does nothing", which a caller cannot
         * see from err=0.  (A scan that found zero KSU mounts is not a failure: rc==0.) */
        rc = sus_mount_mark_ksu_mounts();
        if (rc < 0) {
            pr_warn("sus_mount: scan on enable failed %d - hook is live but no mount was marked, reporting the failure to userspace\n",
                    rc);
            info.err = rc;
            goto out;
        }
    } else if (mount_registered) {
        sus_mount_unregister();
    }
    info.err = 0;
    SUSFS_LOGI("sus_mount: %s (supercall)\n", info.enabled ? "hide" : "unhide");
out:
    /* upstream writes back only ->err for input-type commands */
    if (copy_to_user(&((struct st_susfs_hide_sus_mnts_for_non_su_procs __user *)*arg)->err,
                     &info.err, sizeof(info.err)))
        pr_warn("sus_mount supercall copy_to_user failed\n");
}
