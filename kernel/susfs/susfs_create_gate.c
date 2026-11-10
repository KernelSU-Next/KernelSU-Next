/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_create_gate.c - answer the create family the way an absent name would
 *
 * Issue #115: sus_path hides a path on every read path, but the create family
 * (mkdirat/symlinkat/mknodat/linkat) used to answer EEXIST, because
 * filename_create() returns EEXIST as soon as the lookup finds a positive
 * dentry - before may_create() checks the parent's write permission.  Nothing
 * registered in the LSM chain can run first.
 *
 * These four entries are therefore replaced at the syscall table (the same
 * fp-hook layer the stat family uses).  When the name resolves to an inode
 * sus_path hides FROM THIS CALLER, the entry answers -EACCES, which is what an
 * absent name produces when the parent is not writable (the case the issue
 * measured).  Everything else - absent names, visible callers, missing
 * arguments - is handed to the kernel's own entry unchanged.
 *
 * It deliberately does not create anything: the dcache whiteout work (which
 * would make the kernel itself take the absent path) is not ready to carry
 * this, and a rule here must not be turned into a file on disk.
 *
 * Side channels considered:
 *  - errno: EACCES for a non-writable parent is what an absent name answers.
 *    A writable parent would make an absent name succeed; that difference is
 *    still there and is documented, not hidden.
 *  - time/cache: the hit path used to skip may_create()'s permission check, so
 *    it ran the same lookup parent permission check the absent path runs and
 *    throws the answer away.
 *  - linkat: the OLD path is checked too (an absent old name answers ENOENT);
 *    a hidden old path answering the new-path EACCES would let a caller link a
 *    hidden inode out of hiding.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <asm/unistd.h>

#include "susfs.h"
#include "susfs_fp_hook.h"
#include "susfs_create_data.h"
#include "susfs_create_policy.h"
#include "susfs_create_vfs.h"
#include "susfs_log.h"

/* The AArch32 table numbers (arch/arm/tools/syscall.tbl); the native ones come
 * from __NR_*.  Kept spelled out for the same reason susfs_fp_hook.h spells the
 * compat indexes out: the two tables do not share a numbering. */
#define COMPAT_MKNODAT_NR	324
#define COMPAT_MKDIRAT_NR	323
#define COMPAT_SYMLINKAT_NR	331
#define COMPAT_LINKAT_NR	330

static susfs_syscall_fn_t create_orig(enum susfs_create_syscall id)
{
	return READ_ONCE(*susfs_create_orig_slot(id));
}

static susfs_syscall_fn_t create_orig_compat(enum susfs_create_syscall id)
{
	return READ_ONCE(*susfs_create_orig_compat_slot(id));
}

static __nocfi noinline long create_wrap_mkdirat(const struct pt_regs *regs)
{
	susfs_syscall_fn_t orig = create_orig(SUSFS_CREATE_MKDIRAT);
	long ret;
	int handled;

	if (!orig)
		return -ENOSYS;
	if (susfs_create_name_hidden((int)regs->regs[0],
				     (const char __user *)regs->regs[1],
				     SUSFS_CREATE_VIEW_MKDIRAT, false))
		return -EACCES;
	if (susfs_create_commit_enabled()) {
		ret = susfs_create_mkdirat_commit((int)regs->regs[0],
						  (const char __user *)regs->regs[1],
						  (umode_t)regs->regs[2], &handled);
		if (handled)
			return ret;
	}
	return orig(regs);
}

static __nocfi noinline long create_wrap_mknodat(const struct pt_regs *regs)
{
	susfs_syscall_fn_t orig = create_orig(SUSFS_CREATE_MKNODAT);

	if (!orig)
		return -ENOSYS;
	if (susfs_create_name_hidden((int)regs->regs[0], (const char __user *)regs->regs[1], SUSFS_CREATE_VIEW_MKNODAT, false))
		return -EACCES;
	return orig(regs);
}

static __nocfi noinline long create_wrap_symlinkat(const struct pt_regs *regs)
{
	/* symlinkat(oldname, newdfd, newname): the path created is regs[2]. */
	susfs_syscall_fn_t orig = create_orig(SUSFS_CREATE_SYMLINKAT);

	if (!orig)
		return -ENOSYS;
	if (susfs_create_name_hidden((int)regs->regs[1], (const char __user *)regs->regs[2], SUSFS_CREATE_VIEW_SYMLINKAT, false))
		return -EACCES;
	return orig(regs);
}

static __nocfi noinline long create_wrap_linkat(const struct pt_regs *regs)
{
	/* linkat(olddfd, oldname, newdfd, newname, flags): both ends matter.  An
	 * absent old name answers ENOENT; a hidden old name must not let a caller
	 * link the hidden inode out under a visible name. */
	susfs_syscall_fn_t orig = create_orig(SUSFS_CREATE_LINKAT);

	if (!orig)
		return -ENOSYS;
	if (susfs_create_name_hidden((int)regs->regs[0], (const char __user *)regs->regs[1], SUSFS_CREATE_VIEW_LINKAT, true))
		return -ENOENT;
	if (susfs_create_name_hidden((int)regs->regs[2], (const char __user *)regs->regs[3], SUSFS_CREATE_VIEW_LINKAT, false))
		return -EACCES;
	return orig(regs);
}

static __nocfi noinline long create_wrap_compat_mkdirat(const struct pt_regs *regs)
{
	susfs_syscall_fn_t orig = create_orig_compat(SUSFS_CREATE_MKDIRAT);
	long ret;
	int handled;

	if (!orig)
		return -ENOSYS;
	if (susfs_create_name_hidden((int)regs->regs[0],
				     (const char __user *)regs->regs[1],
				     SUSFS_CREATE_VIEW_MKDIRAT, false))
		return -EACCES;
	if (susfs_create_commit_enabled()) {
		ret = susfs_create_mkdirat_commit((int)regs->regs[0],
						  (const char __user *)regs->regs[1],
						  (umode_t)regs->regs[2], &handled);
		if (handled)
			return ret;
	}
	return orig(regs);
}

static __nocfi noinline long create_wrap_compat_mknodat(const struct pt_regs *regs)
{
	susfs_syscall_fn_t orig = create_orig_compat(SUSFS_CREATE_MKNODAT);

	if (!orig)
		return -ENOSYS;
	if (susfs_create_name_hidden((int)regs->regs[0], (const char __user *)regs->regs[1], SUSFS_CREATE_VIEW_MKNODAT, false))
		return -EACCES;
	return orig(regs);
}

static __nocfi noinline long create_wrap_compat_symlinkat(const struct pt_regs *regs)
{
	susfs_syscall_fn_t orig = create_orig_compat(SUSFS_CREATE_SYMLINKAT);

	if (!orig)
		return -ENOSYS;
	if (susfs_create_name_hidden((int)regs->regs[1], (const char __user *)regs->regs[2], SUSFS_CREATE_VIEW_SYMLINKAT, false))
		return -EACCES;
	return orig(regs);
}

static __nocfi noinline long create_wrap_compat_linkat(const struct pt_regs *regs)
{
	susfs_syscall_fn_t orig = create_orig_compat(SUSFS_CREATE_LINKAT);

	if (!orig)
		return -ENOSYS;
	if (susfs_create_name_hidden((int)regs->regs[0], (const char __user *)regs->regs[1], SUSFS_CREATE_VIEW_LINKAT, true))
		return -ENOENT;
	if (susfs_create_name_hidden((int)regs->regs[2], (const char __user *)regs->regs[3], SUSFS_CREATE_VIEW_LINKAT, false))
		return -EACCES;
	return orig(regs);
}

static struct susfs_fp_hook create_hooks[] = {
	{ .nr = __NR_mkdirat, .nr_compat = COMPAT_MKDIRAT_NR,
	  .name = "create/mkdirat", .sym = "__arm64_sys_mkdirat",
	  .wrapper = create_wrap_mkdirat, .wrapper_compat = create_wrap_compat_mkdirat },
	{ .nr = __NR_mknodat, .nr_compat = COMPAT_MKNODAT_NR,
	  .name = "create/mknodat", .sym = "__arm64_sys_mknodat",
	  .wrapper = create_wrap_mknodat, .wrapper_compat = create_wrap_compat_mknodat },
	{ .nr = __NR_symlinkat, .nr_compat = COMPAT_SYMLINKAT_NR,
	  .name = "create/symlinkat", .sym = "__arm64_sys_symlinkat",
	  .wrapper = create_wrap_symlinkat, .wrapper_compat = create_wrap_compat_symlinkat },
	{ .nr = __NR_linkat, .nr_compat = COMPAT_LINKAT_NR,
	  .name = "create/linkat", .sym = "__arm64_sys_linkat",
	  .wrapper = create_wrap_linkat, .wrapper_compat = create_wrap_compat_linkat },
};

int susfs_create_gate_init(void)
{
	int installed = 0;
	int rc;
	int i;

	rc = susfs_create_vfs_init();
	if (rc)
		return rc;

	BUILD_BUG_ON(ARRAY_SIZE(create_hooks) != SUSFS_CREATE_COUNT);
	for (i = 0; i < (int)ARRAY_SIZE(create_hooks); i++) {
		create_hooks[i].orig_slot =
			susfs_create_orig_slot((enum susfs_create_syscall)i);
		create_hooks[i].orig_slot_compat =
			susfs_create_orig_compat_slot((enum susfs_create_syscall)i);
	}

	/* Unconditional: a supercall can add a rule before/after this layer, so the
	 * entries must be there whenever the module is.  An entry that cannot be
	 * installed leaves the others working and the kernel's own answer in place. */
	for (i = 0; i < (int)ARRAY_SIZE(create_hooks); i++) {
		struct susfs_fp_hook *h = &create_hooks[i];

		rc = susfs_fp_install(h);
		if (rc) {
			pr_warn("create_gate: %s not installed (%d) - that syscall answers the kernel's own way\n",
				h->name, rc);
			continue;
		}
		installed++;
	}
	if (!installed) {
		pr_warn("create_gate: none of the %u create entries could be installed\n",
			(unsigned int)ARRAY_SIZE(create_hooks));
		return -ENOENT;
	}
	if (installed < (int)ARRAY_SIZE(create_hooks))
		pr_warn("create_gate: %d of %u create entries installed (the log above names the rest)\n",
			installed, (unsigned int)ARRAY_SIZE(create_hooks));

	SUSFS_LOGI("create_gate armed: %d of %u create entries answer EACCES for hidden names\n",
		   installed, (unsigned int)ARRAY_SIZE(create_hooks));
	return 0;
}

void susfs_create_gate_exit(void)
{
	int armed = 0;
	int i;

	for (i = 0; i < (int)ARRAY_SIZE(create_hooks); i++) {
		struct susfs_fp_hook *h = &create_hooks[i];

		if (!h->installed)
			continue;
		susfs_fp_remove(h);
		armed++;
	}
	if (armed)
		susfs_fp_drain();
}
