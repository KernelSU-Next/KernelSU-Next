// SPDX-License-Identifier: GPL-2.0-only
/*
 * susfs_mount_mnt.c - SUS_MOUNT's path-resolution face (the __lookup_mnt
 * equivalent), independent of sus_path.
 *
 * Two landing points, preferred first:
 *
 *   lookup inline  the centre's ret hooks on path_lookupat / filename_lookup /
 *                  path_openat: the kernel already resolved the path, so the
 *                  decision runs on the real struct path and costs what the
 *                  lookup cost.  This is the exact __lookup_mnt position.
 *
 *   FP fallback    when the inline faces could not be installed (5.15 inlines
 *                  filename_lookup, so this is the live path there), each
 *                  path-shape syscall entry resolves the caller's path itself
 *                  through kern_path() and applies the same decision.  The
 *                  extra walk is the equal-cost answer: the syscall pays a real
 *                  lookup regardless of the outcome, and nothing user-writable
 *                  decides where it goes.
 *
 * Both land on sus_mount_path_hidden(), which owns the mount decision
 * (identity / mnt_id range / d_path learning) - this file only provides the
 * landing points.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/slab.h>	/* kmalloc()/kfree() */
#include <linux/namei.h>
#include <linux/path.h>

#include <asm/unistd.h>

#include "susfs_fp_hook.h"
#include "susfs_hooks.h"
#include "susfs_log.h"
#include "sus_mount.h"
#include "susfs_mount_mnt.h"

/* ---- lookup inline face ---- */

static int mount_mnt_lookup_pre(void *p)
{
	struct susfs_inode_ctx *ctx = p;

	if (ctx->path && sus_mount_path_hidden(ctx->path)) {
		ctx->ret = -ENOENT;
		return 1;
	}
	return 0;
}

static struct susfs_hook mount_mnt_lookup_file = {
	.owner = "mount_mnt",
	.name = "mnt:filename_lookup",
	.target = SUSFS_HOOK_TARGET_FILENAME_LOOKUP,
	.pre = mount_mnt_lookup_pre,
};

static struct susfs_hook mount_mnt_lookup_vfs = {
	.owner = "mount_mnt",
	.name = "mnt:path_lookupat",
	.target = SUSFS_HOOK_TARGET_PATH_LOOKUPAT,
	.pre = mount_mnt_lookup_pre,
};

static struct susfs_hook mount_mnt_openat = {
	.owner = "mount_mnt",
	.name = "mnt:path_openat",
	.target = SUSFS_HOOK_TARGET_PATH_OPENAT,
	.pre = mount_mnt_lookup_pre,
};

/* ---- FP fallback (syscall entries that reach a path lookup) ---- */

enum mount_mnt_syscall {
	MM_OPENAT,
	MM_OPENAT2,
	MM_NEWFSTATAT,
	MM_STATX,
	MM_READLINKAT,
	MM_FACCESSAT,
	MM_EXECVEAT,
	MM_COUNT,
};

static susfs_syscall_fn_t mm_slots[MM_COUNT];

/* Resolve the caller's own path (its creds, its namespaces) and run the mount
 * decision on the result.  Failed walks fall through to the original: the
 * caller could not reach the path anyway. */
static __nocfi noinline long mm_check_or_orig(const struct pt_regs *regs,
					      int slot)
{
	susfs_syscall_fn_t orig = READ_ONCE(mm_slots[slot]);
	const char __user *upath;
	char *buf;
	struct path path;
	long rc;

	if (!orig)
		return -ENOSYS;
	upath = (const char __user *)regs->regs[1];
	if (!upath)
		return orig(regs);
	buf = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!buf)
		return orig(regs);
	if (strncpy_from_user(buf, upath, PATH_MAX) <= 0) {
		kfree(buf);
		return orig(regs);
	}
	buf[PATH_MAX - 1] = '\0';
	rc = 0;
	{
		int kp = kern_path(buf, 0, &path);
		static atomic_t mm_dbg = ATOMIC_INIT(0);

		if (atomic_inc_return(&mm_dbg) <= 8u)
			pr_info("mount_mnt: fallback path=%s kern_path=%d\n",
				buf, kp);
		if (!kp) {
		bool hidden = sus_mount_path_hidden(&path);

		path_put(&path);
		if (hidden)
			rc = -ENOENT;
	}
	}
	kfree(buf);
	if (rc)
		return rc;
	return orig(regs);
}

static __nocfi noinline long mm_wrap_openat(const struct pt_regs *regs)
{
	return mm_check_or_orig(regs, MM_OPENAT);
}

static __nocfi noinline long mm_wrap_openat2(const struct pt_regs *regs)
{
	return mm_check_or_orig(regs, MM_OPENAT2);
}

static __nocfi noinline long mm_wrap_newfstatat(const struct pt_regs *regs)
{
	return mm_check_or_orig(regs, MM_NEWFSTATAT);
}

static __nocfi noinline long mm_wrap_statx(const struct pt_regs *regs)
{
	return mm_check_or_orig(regs, MM_STATX);
}

static __nocfi noinline long mm_wrap_readlinkat(const struct pt_regs *regs)
{
	return mm_check_or_orig(regs, MM_READLINKAT);
}

static __nocfi noinline long mm_wrap_faccessat(const struct pt_regs *regs)
{
	return mm_check_or_orig(regs, MM_FACCESSAT);
}

static __nocfi noinline long mm_wrap_execveat(const struct pt_regs *regs)
{
	return mm_check_or_orig(regs, MM_EXECVEAT);
}

static struct susfs_fp_hook mm_hooks[] = {
	{ .nr = __NR_openat, .name = "mount_mnt/openat",
	  .sym = "__arm64_sys_openat", .wrapper = mm_wrap_openat,
	  .orig_slot = &mm_slots[MM_OPENAT] },
	{ .nr = __NR_openat2, .name = "mount_mnt/openat2",
	  .sym = "__arm64_sys_openat2", .wrapper = mm_wrap_openat2,
	  .orig_slot = &mm_slots[MM_OPENAT2] },
	{ .nr = __NR_newfstatat, .name = "mount_mnt/newfstatat",
	  .sym = "__arm64_sys_newfstatat", .wrapper = mm_wrap_newfstatat,
	  .orig_slot = &mm_slots[MM_NEWFSTATAT] },
	{ .nr = __NR_statx, .name = "mount_mnt/statx",
	  .sym = "__arm64_sys_statx", .wrapper = mm_wrap_statx,
	  .orig_slot = &mm_slots[MM_STATX] },
	{ .nr = __NR_readlinkat, .name = "mount_mnt/readlinkat",
	  .sym = "__arm64_sys_readlinkat", .wrapper = mm_wrap_readlinkat,
	  .orig_slot = &mm_slots[MM_READLINKAT] },
	{ .nr = __NR_faccessat, .name = "mount_mnt/faccessat",
	  .sym = "__arm64_sys_faccessat", .wrapper = mm_wrap_faccessat,
	  .orig_slot = &mm_slots[MM_FACCESSAT] },
	{ .nr = __NR_execveat, .name = "mount_mnt/execveat",
	  .sym = "__arm64_sys_execveat", .wrapper = mm_wrap_execveat,
	  .orig_slot = &mm_slots[MM_EXECVEAT] },
};

static struct susfs_hook mm_reg[ARRAY_SIZE(mm_hooks)];

int susfs_mount_mnt_init(void)
{
	int i, inline_ok, fp_ok = 0;

	/* Preferred landing point: the centre's lookup ret hooks. */
	inline_ok = 0;
	inline_ok += susfs_hook_register(&mount_mnt_lookup_file) >= 0;
	inline_ok += susfs_hook_register(&mount_mnt_lookup_vfs) >= 0;
	inline_ok += susfs_hook_register(&mount_mnt_openat) >= 0;
	SUSFS_LOGI("mount_mnt: lookup inline %d/3\n", inline_ok);

	/* Fallback: syscall entries that resolve the path themselves. */
	if (susfs_fp_init())
		return inline_ok ? 0 : -ENOENT;
	for (i = 0; i < (int)ARRAY_SIZE(mm_hooks); i++) {
		mm_reg[i].owner = "mount_mnt";
		mm_reg[i].name = mm_hooks[i].name;
		mm_reg[i].target = SUSFS_HOOK_TARGET_FP;
		mm_reg[i].fp = &mm_hooks[i];
		if (susfs_hook_register(&mm_reg[i]) < 0)
			continue;
		fp_ok++;
	}
	SUSFS_LOGI("mount_mnt: fp fallback %d/%d\n", fp_ok,
		   (int)ARRAY_SIZE(mm_hooks));
	return (inline_ok || fp_ok) ? 0 : -ENOENT;
}

void susfs_mount_mnt_exit(void)
{
	int i;

	if (mount_mnt_lookup_file.backend != SUSFS_HOOK_BACKEND_NONE)
		susfs_hook_unregister(&mount_mnt_lookup_file);
	if (mount_mnt_lookup_vfs.backend != SUSFS_HOOK_BACKEND_NONE)
		susfs_hook_unregister(&mount_mnt_lookup_vfs);
	if (mount_mnt_openat.backend != SUSFS_HOOK_BACKEND_NONE)
		susfs_hook_unregister(&mount_mnt_openat);
	for (i = 0; i < (int)ARRAY_SIZE(mm_hooks); i++)
		if (mm_reg[i].backend != SUSFS_HOOK_BACKEND_NONE)
			susfs_hook_unregister(&mm_reg[i]);
}
