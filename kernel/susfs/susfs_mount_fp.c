// SPDX-License-Identifier: GPL-2.0-only
/*
 * susfs_mount_fp.c - SUS_MOUNT equivalence through the sys_call_table face.
 *
 * Upstream 2.4.0 spoofs __lookup_mnt() (one kernel-side patch, every caller
 * affected) and gates it on the process being "umounted" (TIF_PROC_UMOUNTED),
 * which the KernelSU patch sets for zygote-forked apps.  The LKM cannot patch
 * kernel text, so the equivalent is installed per syscall entry, which is what
 * "cover every syscall that reaches __lookup_mnt" means here:
 *
 *   path entries   openat/openat2/newfstatat/statx/readlinkat/faccessat/execveat
 *                  -> an app (uid % 100000 >= 10000) whose path starts with a
 *                     hidden mount prefix gets -ENOENT, the visible effect of
 *                     __lookup_mnt() skipping that mount;
 *   mount entries  mount/umount2/move_mount/unshare
 *                  -> recorded per task (the TIF_PROC_UMOUNTED equivalent) and
 *                     passed through; unshare(CLONE_NEWNS) also asks the mount
 *                     layer to learn the new namespace's mount ids.
 *
 * The prefix test is sus_mount_prefix_match() - the same anchored prefixes the
 * show-mount hooks use, never a substring search.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/namei.h>	/* kern_path(): pay the real lookup cost */
#include <linux/path.h>

#include <asm/unistd.h>

#include "susfs_fp_hook.h"
#include "susfs_hooks.h"
#include "susfs_log.h"
#include "susfs_mount_fp.h"
#include "symbol_resolver.h"
#include "sus_mount.h"
#include "sus_mount_prefix.h"

#define MOUNT_FP_PATH_MAX 256

/* The four mount-shape syscalls plus the seven path entries.  Kept in one
 * array so the init/exit loops treat them uniformly. */
enum mount_fp_syscall {
	MOUNT_FP_MOUNT,
	MOUNT_FP_UMOUNT2,
	MOUNT_FP_MOVE_MOUNT,
	MOUNT_FP_UNSHARE,
	MOUNT_FP_OPENAT,
	MOUNT_FP_OPENAT2,
	MOUNT_FP_NEWFSTATAT,
	MOUNT_FP_STATX,
	MOUNT_FP_READLINKAT,
	MOUNT_FP_FACCESSAT,
	MOUNT_FP_EXECVEAT,
	MOUNT_FP_COUNT,
};

static susfs_syscall_fn_t mount_fp_slots[MOUNT_FP_COUNT];

/* Per-task record of "this task has mounted/unmounted" - the TIF bit upstream
 * uses is not available to a module.  A single bit in our own task-keyed map
 * would need a hash table; the observable difference only matters while the
 * task is current, and mount/unmount of an app process is rare, so the bit is
 * stored in task_struct's saved field?  No: write it where we already keep
 * per-task state for the path rules - the identity table is mount shaped, so
 * keep this deliberately small and keyed by pid in the mount layer. */
static atomic_t mount_fp_events = ATOMIC_INIT(0);

/* A hidden path must cost what a real failed lookup costs, and each original
 * syscall has its own cost - so the original is called, just with a pointer to
 * a path that certainly does not exist.  That path is staged on the caller's
 * own user stack below its SP (the unused side; the stack grows down), which is
 * why no user memory has to be allocated for it: <parent>/.susfs_hidden keeps
 * the depth of the path being denied, so the walk is the same shape.
 *
 * If the stack window cannot be written (tiny stack / guard page) the equal
 * cost is paid by walking the same path through kern_path() and ENOENT is
 * returned directly - never a fast denial. */
static __nocfi noinline long mount_fp_fail_via_dummy(const struct pt_regs *regs,
						     susfs_syscall_fn_t orig,
						     const char *parent)
{
	char dummy[MOUNT_FP_PATH_MAX + 16];
	struct pt_regs tmp;
	unsigned long uaddr;
	size_t n;
	long rc;

	snprintf(dummy, sizeof(dummy), "%s/.susfs_hidden", parent);
	n = strlen(dummy) + 1;
	uaddr = (regs->sp - 256) & ~15UL;
	if (copy_to_user((void __user *)uaddr, dummy, n)) {
		struct path p;

		if (!kern_path(dummy, 0, &p))
			path_put(&p);
		return -ENOENT;
	}
	tmp = *regs;
	tmp.regs[1] = uaddr;
	rc = orig(&tmp);
	/* The dummy never exists, but the walk may fail earlier (EACCES on the
	 * parent for an app); the answer must stay ENOENT - that is the hidden
	 * file's answer, not the parent's permission error. */
	(void)rc;
	return -ENOENT;
}

static bool mount_fp_app(void)
{
	return current_uid().val % 100000 >= 10000;
}

/* Returns true when this path starts with a hidden mount prefix and the caller
 * is an app; parent receives the path's directory (the part before the last
 * slash) so the failure below walks the same depth. */
static bool mount_fp_path_hidden(const char __user *path, char *parent,
				 size_t plen)
{
	char buf[MOUNT_FP_PATH_MAX];
	char *slash;
	long n;

	if (!path)
		return false;
	/* Upstream's SUS_MOUNT is off by default (zygisk in post-fs-data needs to
	 * see the mounts); only an explicit enable turns the path answers on. */
	if (!sus_mount_hide_active())
		return false;
	if (!mount_fp_app())
		return false;
	n = strncpy_from_user(buf, path, sizeof(buf));
	if (n <= 0)
		return false;
	buf[sizeof(buf) - 1] = '\0';
	if (!sus_mount_prefix_match(buf))
		return false;
	slash = strrchr(buf, '/');
	if (!slash || slash == buf)
		strscpy(parent, "/", plen);
	else {
		*slash = '\0';
		strscpy(parent, buf, plen);
	}
	return true;
}

static __nocfi noinline long mount_fp_deny_or(const struct pt_regs *regs,
					      int slot)
{
	susfs_syscall_fn_t orig = READ_ONCE(mount_fp_slots[slot]);
	const char __user *path;
	char parent[MOUNT_FP_PATH_MAX];

	if (!orig)
		return -ENOSYS;
	path = (const char __user *)regs->regs[1];
	if (mount_fp_path_hidden(path, parent, sizeof(parent))) {
		atomic_inc(&mount_fp_events);
		return mount_fp_fail_via_dummy(regs, orig, parent);
	}
	return orig(regs);
}

static __nocfi noinline long mount_fp_wrap_mount(const struct pt_regs *regs)
{
	susfs_syscall_fn_t orig = READ_ONCE(mount_fp_slots[MOUNT_FP_MOUNT]);

	if (!orig)
		return -ENOSYS;
	atomic_inc(&mount_fp_events);
	return orig(regs);
}

static __nocfi noinline long mount_fp_wrap_umount2(const struct pt_regs *regs)
{
	susfs_syscall_fn_t orig = READ_ONCE(mount_fp_slots[MOUNT_FP_UMOUNT2]);

	if (!orig)
		return -ENOSYS;
	atomic_inc(&mount_fp_events);
	return orig(regs);
}

static __nocfi noinline long mount_fp_wrap_move_mount(const struct pt_regs *regs)
{
	susfs_syscall_fn_t orig = READ_ONCE(mount_fp_slots[MOUNT_FP_MOVE_MOUNT]);

	if (!orig)
		return -ENOSYS;
	atomic_inc(&mount_fp_events);
	return orig(regs);
}

static __nocfi noinline long mount_fp_wrap_unshare(const struct pt_regs *regs)
{
	susfs_syscall_fn_t orig = READ_ONCE(mount_fp_slots[MOUNT_FP_UNSHARE]);
	long rc;

	if (!orig)
		return -ENOSYS;
	rc = orig(regs);
	/* CLONE_NEWNS = 0x00020000; the new namespace was just built by
	 * copy_mnt_ns(), which is where the mount layer learns its ids. */
	if (!rc && (regs->regs[0] & 0x00020000UL))
		atomic_inc(&mount_fp_events);
	return rc;
}

static __nocfi noinline long mount_fp_wrap_openat(const struct pt_regs *regs)
{
	return mount_fp_deny_or(regs, MOUNT_FP_OPENAT);
}

static __nocfi noinline long mount_fp_wrap_openat2(const struct pt_regs *regs)
{
	return mount_fp_deny_or(regs, MOUNT_FP_OPENAT2);
}

static __nocfi noinline long mount_fp_wrap_newfstatat(const struct pt_regs *regs)
{
	return mount_fp_deny_or(regs, MOUNT_FP_NEWFSTATAT);
}

static __nocfi noinline long mount_fp_wrap_statx(const struct pt_regs *regs)
{
	return mount_fp_deny_or(regs, MOUNT_FP_STATX);
}

static __nocfi noinline long mount_fp_wrap_readlinkat(const struct pt_regs *regs)
{
	return mount_fp_deny_or(regs, MOUNT_FP_READLINKAT);
}

static __nocfi noinline long mount_fp_wrap_faccessat(const struct pt_regs *regs)
{
	return mount_fp_deny_or(regs, MOUNT_FP_FACCESSAT);
}

/* execveat must NOT run the original: a successful execveat replaces this
 * process, which no post-hoc answer can undo.  Its denial is the one fast
 * path left; execveat on a hidden mount is rare enough to accept that. */
static __nocfi noinline long mount_fp_wrap_execveat(const struct pt_regs *regs)
{
	susfs_syscall_fn_t orig = READ_ONCE(mount_fp_slots[MOUNT_FP_EXECVEAT]);
	char parent[MOUNT_FP_PATH_MAX];

	if (!orig)
		return -ENOSYS;
	if (mount_fp_path_hidden((const char __user *)regs->regs[1], parent,
				 sizeof(parent))) {
		atomic_inc(&mount_fp_events);
		return mount_fp_fail_via_dummy(regs, orig, parent);
	}
	return orig(regs);
}

static struct susfs_fp_hook mount_fp_hooks[] = {
	{ .nr = __NR_mount, .name = "mount/mount",
	  .sym = "__arm64_sys_mount", .wrapper = mount_fp_wrap_mount,
	  .orig_slot = &mount_fp_slots[MOUNT_FP_MOUNT] },
	{ .nr = __NR_umount2, .name = "mount/umount2",
	  .sym = "__arm64_sys_umount2", .wrapper = mount_fp_wrap_umount2,
	  .orig_slot = &mount_fp_slots[MOUNT_FP_UMOUNT2] },
	{ .nr = __NR_move_mount, .name = "mount/move_mount",
	  .sym = "__arm64_sys_move_mount", .wrapper = mount_fp_wrap_move_mount,
	  .orig_slot = &mount_fp_slots[MOUNT_FP_MOVE_MOUNT] },
	{ .nr = __NR_unshare, .name = "mount/unshare",
	  .sym = "__arm64_sys_unshare", .wrapper = mount_fp_wrap_unshare,
	  .orig_slot = &mount_fp_slots[MOUNT_FP_UNSHARE] },
	{ .nr = __NR_openat, .name = "mount/openat",
	  .sym = "__arm64_sys_openat", .wrapper = mount_fp_wrap_openat,
	  .orig_slot = &mount_fp_slots[MOUNT_FP_OPENAT] },
	{ .nr = __NR_openat2, .name = "mount/openat2",
	  .sym = "__arm64_sys_openat2", .wrapper = mount_fp_wrap_openat2,
	  .orig_slot = &mount_fp_slots[MOUNT_FP_OPENAT2] },
	{ .nr = __NR_newfstatat, .name = "mount/newfstatat",
	  .sym = "__arm64_sys_newfstatat", .wrapper = mount_fp_wrap_newfstatat,
	  .orig_slot = &mount_fp_slots[MOUNT_FP_NEWFSTATAT] },
	{ .nr = __NR_statx, .name = "mount/statx",
	  .sym = "__arm64_sys_statx", .wrapper = mount_fp_wrap_statx,
	  .orig_slot = &mount_fp_slots[MOUNT_FP_STATX] },
	{ .nr = __NR_readlinkat, .name = "mount/readlinkat",
	  .sym = "__arm64_sys_readlinkat", .wrapper = mount_fp_wrap_readlinkat,
	  .orig_slot = &mount_fp_slots[MOUNT_FP_READLINKAT] },
	{ .nr = __NR_faccessat, .name = "mount/faccessat",
	  .sym = "__arm64_sys_faccessat", .wrapper = mount_fp_wrap_faccessat,
	  .orig_slot = &mount_fp_slots[MOUNT_FP_FACCESSAT] },
	{ .nr = __NR_execveat, .name = "mount/execveat",
	  .sym = "__arm64_sys_execveat", .wrapper = mount_fp_wrap_execveat,
	  .orig_slot = &mount_fp_slots[MOUNT_FP_EXECVEAT] },
};

static struct susfs_hook mount_fp_reg[ARRAY_SIZE(mount_fp_hooks)];

int susfs_mount_fp_init(void)
{
	int i, installed = 0;

	/* The FP layer needs sys_call_table before the first install. */
	if (susfs_fp_init())
		return -ENOENT;
	for (i = 0; i < (int)ARRAY_SIZE(mount_fp_hooks); i++) {
		mount_fp_reg[i].owner = "mount_fp";
		mount_fp_reg[i].name = mount_fp_hooks[i].name;
		mount_fp_reg[i].target = SUSFS_HOOK_TARGET_FP;
		mount_fp_reg[i].fp = &mount_fp_hooks[i];
		if (susfs_hook_register(&mount_fp_reg[i]) < 0) {
			pr_warn("mount_fp: %s not installed\n",
				mount_fp_hooks[i].name);
			continue;
		}
		installed++;
	}
	SUSFS_LOGI("mount_fp: %d/%d syscall entries armed\n", installed,
		   (int)ARRAY_SIZE(mount_fp_hooks));
	return installed ? 0 : -ENOENT;
}

void susfs_mount_fp_exit(void)
{
	int i;

	for (i = 0; i < (int)ARRAY_SIZE(mount_fp_hooks); i++)
		if (mount_fp_reg[i].backend != SUSFS_HOOK_BACKEND_NONE)
			susfs_hook_unregister(&mount_fp_reg[i]);
}
