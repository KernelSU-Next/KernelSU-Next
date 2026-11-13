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

/* Negative results: the caller can use the usual `if (ret) fail` shape.
 * FALLBACK is not an installation failure: the hook is installed, but on the
 * non-preferred backend (for example map bit cleared -> kprobe instead of inline). */
#define SUSFS_HOOK_OK			0
#define SUSFS_HOOK_FALLBACK		(-1)
#define SUSFS_HOOK_INSTALL_FAILED	(-225)

enum susfs_hook_backend {
	SUSFS_HOOK_BACKEND_AUTO = 0,	/* only register(): prefer FP/INLINE, fall back */
	SUSFS_HOOK_BACKEND_FP,
	SUSFS_HOOK_BACKEND_KPROBE,
	SUSFS_HOOK_BACKEND_INLINE,
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
	SUSFS_HOOK_TARGET_COUNT
};

struct susfs_hook {
	const char *owner;		/* layer name, diagnostics and teardown */
	const char *name;		/* consumer name, diagnostics */
	enum susfs_hook_target target;
	enum susfs_hook_backend assign;	/* ignored by register() */
	/* Target-specific context; the centre supplies the typed adapter. */
	int  (*pre)(void *ctx);		/* 0 = run original, 1 = skip it */
	void (*post)(void *ctx);
	void *priv;
};

/*
 * susfs_hook_register():
 *   - chooses the normal backend for the target (FP for syscall targets,
 *     INLINE when the map bit is set, KPROBE otherwise);
 *   - a target is installed once, later consumers only join its dispatch list;
 *   - returns SUSFS_HOOK_OK (0)       installed with the preferred backend;
 *   - returns SUSFS_HOOK_FALLBACK (-1) installed, but a fallback backend was used;
 *   - returns SUSFS_HOOK_INSTALL_FAILED (-225) nothing installed.
 *
 * susfs_hook_register_assign():
 *   - uses exactly the requested backend, never falls back;
 *   - returns SUSFS_HOOK_OK (0) or SUSFS_HOOK_INSTALL_FAILED (-225).
 *
 * Both are idempotent for an already-installed target with the same consumer:
 * the duplicate registration returns the status of the existing install.
 */
int susfs_hook_register(struct susfs_hook *hook);
int susfs_hook_register_assign(struct susfs_hook *hook,
			       enum susfs_hook_backend backend);

/* Remove one consumer; the last one tears the target down. */
void susfs_hook_unregister(struct susfs_hook *hook);

#endif /* _SUSFS_HOOKS_H */
