// SPDX-License-Identifier: GPL-2.0-only
/*
 * susfs_mount_fp.c - SUS_MOUNT's syscall face (mount-shape entries only).
 *
 * The path half of upstream's __lookup_mnt spoof lives in the sus_path lookup
 * consumer (sus_mount_path_hidden()), so its cost is the original lookup's and
 * there is no user memory in the decision.  What remains here is the mount
 * state face: mount/umount2/move_mount/unshare are recorded per task before
 * passing through - the module-side equivalent of the TIF_PROC_UMOUNTED bit
 * upstream gets from the KernelSU patch.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/cred.h>

#include <asm/unistd.h>

#include "susfs_fp_hook.h"
#include "susfs_hooks.h"
#include "susfs_log.h"
#include "susfs_mount_fp.h"

enum mount_fp_syscall {
	MOUNT_FP_MOUNT,
	MOUNT_FP_UMOUNT2,
	MOUNT_FP_MOVE_MOUNT,
	MOUNT_FP_UNSHARE,
	MOUNT_FP_COUNT,
};

static susfs_syscall_fn_t mount_fp_slots[MOUNT_FP_COUNT];

/* Per-task "this task has mounted/unmounted" is not tracked yet; the counter
 * keeps the events observable until the mount layer consumes them. */
static atomic_t mount_fp_events = ATOMIC_INIT(0);

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
