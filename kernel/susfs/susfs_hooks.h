/* SPDX-License-Identifier: GPL-2.0 */
/*
 * susfs_hooks.h - the single hook registration surface for feature layers.
 *
 * A feature layer never calls hk_inline_hook(), register_kprobe() or
 * susfs_fp_install().  It registers a target plus pre/post consumers here; the
 * susfs_hooks layer owns the target table, backend selection and teardown.
 */
#ifndef _SUSFS_HOOKS_H
#define _SUSFS_HOOKS_H

#include <linux/types.h>
#include <linux/version.h>

/* Installation failures are negative, so `if (ret < 0) fail` is the shape.
 * FALLBACK is a POSITIVE result and NOT an installation failure: the hook is
 * installed, but on the non-preferred backend (map bit cleared -> kprobe
 * instead of inline).  It must be reported to the caller so it can log the
 * degraded mode, but the hook is live. */
#define SUSFS_HOOK_OK			0
#define SUSFS_HOOK_FALLBACK		1
#define SUSFS_HOOK_INSTALL_FAILED	(-225)

/* Backends.  NONE is the "not installed" state a hook starts in; AUTO is only
 * valid for susfs_hook_register(): it picks the best backend for the target and
 * reports FALLBACK when it had to use another one. */
enum susfs_hook_backend {
	SUSFS_HOOK_BACKEND_NONE = 0,
	SUSFS_HOOK_BACKEND_AUTO,
	SUSFS_HOOK_BACKEND_FP,
	SUSFS_HOOK_BACKEND_KPROBE,
	SUSFS_HOOK_BACKEND_INLINE,
	SUSFS_HOOK_BACKEND_LSM,		/* security_hook_list insertion */
	SUSFS_HOOK_BACKEND_TABLE,	/* seq_operations-style table replacement */
};

enum susfs_hook_target {
	SUSFS_HOOK_TARGET_AVC_AUDIT,
	SUSFS_HOOK_TARGET_SHOW_MAP_VMA,
	SUSFS_HOOK_TARGET_SHOW_SMAP,
	SUSFS_HOOK_TARGET_SHOW_MAP,
	SUSFS_HOOK_TARGET_WALK_PAGE_RANGE,
	SUSFS_HOOK_TARGET_WALK_PAGE_VMA,
	SUSFS_HOOK_TARGET_GET_USER_PAGES_REMOTE,
	SUSFS_HOOK_TARGET_GET_USER_PAGES_REMOTE_INNER,
	SUSFS_HOOK_TARGET_MAP_FILES_GET_LINK,
	SUSFS_HOOK_TARGET_D_PATH,
	SUSFS_HOOK_TARGET_VFS_STATFS,
	SUSFS_HOOK_TARGET_SHOW_VFSSTAT,
	SUSFS_HOOK_TARGET_SHOW_MOUNTINFO,
	SUSFS_HOOK_TARGET_SHOW_VFSMNT,
	SUSFS_HOOK_TARGET_ARM64_SYS_STATX,
	SUSFS_HOOK_TARGET_DO_STATX,
	SUSFS_HOOK_TARGET_GENERIC_SHUTDOWN_SUPER,
	SUSFS_HOOK_TARGET_COPY_MNT_NS,
	SUSFS_HOOK_TARGET_ATTACH_RECURSIVE_MNT,
	SUSFS_HOOK_TARGET_ARM64_SYS_NEWUNAME,
	SUSFS_HOOK_TARGET_VFS_OPEN,
	SUSFS_HOOK_TARGET_PATH_OPENAT,
	SUSFS_HOOK_TARGET_ARM64_SYS_REBOOT,
	SUSFS_HOOK_TARGET_SEQ_SHOW,
	SUSFS_HOOK_TARGET_LSM,		/* security_hook_list, one per hook */
	SUSFS_HOOK_TARGET_COUNT
};

/* Target-specific contexts.  The centre builds these from the original call
 * arguments (inline) or from pt_regs (kprobe), so feature layers only see the
 * kernel objects they care about. */
struct seq_file;
struct vm_area_struct;

struct susfs_avc_ctx {
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 0)
	void *state;
#endif
	u32 ssid;
	u32 tsid;
	u16 tclass;
	u32 requested;
	u32 audited;
	u32 denied;
	int result;
	void *a;
};

struct susfs_show_map_ctx {
	struct seq_file *m;
	struct vm_area_struct *vma;
};

struct susfs_fp_hook;
struct kprobe;
struct kretprobe;
struct ksu_lsm_hook;

/* One probe owned by a feature layer.  The centre installs and removes it; the
 * handler stays in the feature.  Exactly one of kp/kr is set.  A probe that is
 * not `required` only degrades the target: its own failure is reported by the
 * feature's handler (the arm called the centre once for the whole set) and the
 * remaining probes keep working. */
struct susfs_hook_probe {
	struct kprobe *kp;
	struct kretprobe *kr;
	bool required;
};

struct susfs_hook {
	const char *owner;		/* layer name, diagnostics and teardown */
	const char *name;		/* consumer name, diagnostics */
	enum susfs_hook_target target;
	enum susfs_hook_backend assign;	/* ignored by register() */
	/* Structured consumers: the centre owns the wrapper and builds the typed
	 * context, so these receive a target-specific struct. */
	int  (*pre)(void *ctx);		/* 0 = run original, 1 = skip it */
	void (*post)(void *ctx);
	void *priv;
	/* Raw consumers: the feature owns the handler and, for a kretprobe, the
	 * instance data layout; the centre owns registration. */
	struct susfs_fp_hook *fp;
	const struct susfs_hook_probe *probes;
	unsigned int nprobes;
	struct ksu_lsm_hook *lsm;
	/* Filled by the centre. */
	enum susfs_hook_backend backend;
	bool installed;
	u64 probe_mask;
};

/*
 * susfs_hook_register():
 *   - chooses the normal backend for the target (FP for syscall targets,
 *     INLINE when the map bit is set, KPROBE otherwise);
 *   - a target is installed once, later consumers only join its dispatch list;
 *   - returns SUSFS_HOOK_OK (0)       installed with the preferred backend;
 *   - returns SUSFS_HOOK_FALLBACK (1) installed, but a fallback backend was used;
 *   - returns SUSFS_HOOK_INSTALL_FAILED (-225) nothing installed.
 *
 * susfs_hook_register_assign():
 *   - uses exactly the requested backend, never falls back;
 *   - returns SUSFS_HOOK_OK (0) or SUSFS_HOOK_INSTALL_FAILED (-225).
 *
 * Both are idempotent for an already-installed target with the same consumer:
 * the duplicate registration returns the status of the existing install.
 */
int susfs_hooks_init(void);
void susfs_hooks_exit(void);

int susfs_hook_register(struct susfs_hook *hook);
int susfs_hook_register_assign(struct susfs_hook *hook,
			       enum susfs_hook_backend backend);

/* Remove one consumer; the last one tears the target down. */
void susfs_hook_unregister(struct susfs_hook *hook);

#endif /* _SUSFS_HOOKS_H */
