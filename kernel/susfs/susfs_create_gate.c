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
#include <linux/namei.h>
#include <linux/dcache.h>
#include <linux/path.h>
#include <linux/err.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <asm/unistd.h>

#include "susfs.h"
#include "susfs_fp_hook.h"
#include "symbol_resolver.h"
#include "susfs_log.h"

/* The AArch32 table numbers (arch/arm/tools/syscall.tbl); the native ones come
 * from __NR_*.  Kept spelled out for the same reason susfs_fp_hook.h spells the
 * compat indexes out: the two tables do not share a numbering. */
#define COMPAT_MKNODAT_NR	33
#define COMPAT_MKDIRAT_NR	34
#define COMPAT_SYMLINKAT_NR	36
#define COMPAT_LINKAT_NR	37

/* inode_permission() grew a mount-namespace argument in 5.12 and an idmap in 6.3.
 * The call below exists only to match the work an absent name does (may_create's
 * permission check), so the argument that changed is the one the caller cannot
 * observe here: the check is discarded either way. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
#include <linux/mnt_idmapping.h>
#define susfs_create_inode_permission(inode, mask) \
	inode_permission(&nop_mnt_idmap, (inode), (mask))
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
#include <linux/user_namespace.h>
#define susfs_create_inode_permission(inode, mask) \
	inode_permission(&init_user_ns, (inode), (mask))
#else
#define susfs_create_inode_permission(inode, mask) \
	inode_permission((inode), (mask))
#endif

static struct filename *(*create_getname_fn)(const char __user *name, int flags, int *empty);
static int (*create_filename_lookup_fn)(int dfd, struct filename *name, unsigned flags,
					struct path *path, struct path *root);
static void (*create_putname_fn)(struct filename *name);

static susfs_syscall_fn_t create_orig_mknodat;
static susfs_syscall_fn_t create_orig_mkdirat;
static susfs_syscall_fn_t create_orig_symlinkat;
static susfs_syscall_fn_t create_orig_linkat;
static susfs_syscall_fn_t create_orig_compat_mknodat;
static susfs_syscall_fn_t create_orig_compat_mkdirat;
static susfs_syscall_fn_t create_orig_compat_symlinkat;
static susfs_syscall_fn_t create_orig_compat_linkat;

/* Is this (dfd, path) a name sus_path hides from the current caller?  No
 * LOOKUP_FOLLOW: the last component is what the create would make, so a hidden
 * symlink must answer for the link itself, not for its target.
 *
 * __nocfi noinline: the calls through the resolved pointers are indirect calls
 * into the kernel, and the CFI call-site gate the build enforces
 * (tools/cfi_sites.py) refuses an instrumented call site. */
static __nocfi noinline bool create_name_hidden(int dfd, const char __user *path)
{
	struct filename *name;
	struct path p;
	bool hidden;
	int rc;

	if (!sus_path_rules_present())
		return false;
	if (!create_getname_fn || !create_filename_lookup_fn || !create_putname_fn)
		return false;

	name = create_getname_fn(path, 0, NULL);
	if (IS_ERR(name))
		return false;

	rc = create_filename_lookup_fn(dfd, name, 0, &p, NULL);
	create_putname_fn(name);
	if (rc)
		return false;		/* absent: the kernel's own answer */

	hidden = sus_path_inode_hidden_public(d_inode(p.dentry));
	if (hidden) {
		/* Match the work an absent name would have done: the kernel runs
		 * may_create() against the parent before answering, so run the same
		 * permission check and throw the answer away.  This is the time/cache
		 * side of the differential, not the errno. */
		struct inode *dir = d_inode(p.dentry->d_parent);

		if (dir)
			(void)susfs_create_inode_permission(dir, MAY_WRITE | MAY_EXEC);
	}
	path_put(&p);
	return hidden;
}

static __nocfi noinline long create_wrap_mkdirat(const struct pt_regs *regs)
{
	susfs_syscall_fn_t orig = READ_ONCE(create_orig_mkdirat);

	if (!orig)
		return -ENOSYS;
	if (create_name_hidden((int)regs->regs[0], (const char __user *)regs->regs[1]))
		return -EACCES;
	return orig(regs);
}

static __nocfi noinline long create_wrap_mknodat(const struct pt_regs *regs)
{
	susfs_syscall_fn_t orig = READ_ONCE(create_orig_mknodat);

	if (!orig)
		return -ENOSYS;
	if (create_name_hidden((int)regs->regs[0], (const char __user *)regs->regs[1]))
		return -EACCES;
	return orig(regs);
}

static __nocfi noinline long create_wrap_symlinkat(const struct pt_regs *regs)
{
	/* symlinkat(oldname, newdfd, newname): the path created is regs[2]. */
	susfs_syscall_fn_t orig = READ_ONCE(create_orig_symlinkat);

	if (!orig)
		return -ENOSYS;
	if (create_name_hidden((int)regs->regs[1], (const char __user *)regs->regs[2]))
		return -EACCES;
	return orig(regs);
}

static __nocfi noinline long create_wrap_linkat(const struct pt_regs *regs)
{
	/* linkat(olddfd, oldname, newdfd, newname, flags): both ends matter.  An
	 * absent old name answers ENOENT; a hidden old name must not let a caller
	 * link the hidden inode out under a visible name. */
	susfs_syscall_fn_t orig = READ_ONCE(create_orig_linkat);

	if (!orig)
		return -ENOSYS;
	if (create_name_hidden((int)regs->regs[0], (const char __user *)regs->regs[1]))
		return -ENOENT;
	if (create_name_hidden((int)regs->regs[2], (const char __user *)regs->regs[3]))
		return -EACCES;
	return orig(regs);
}

static __nocfi noinline long create_wrap_compat_mkdirat(const struct pt_regs *regs)
{
	susfs_syscall_fn_t orig = READ_ONCE(create_orig_compat_mkdirat);

	if (!orig)
		return -ENOSYS;
	if (create_name_hidden((int)regs->regs[0], (const char __user *)regs->regs[1]))
		return -EACCES;
	return orig(regs);
}

static __nocfi noinline long create_wrap_compat_mknodat(const struct pt_regs *regs)
{
	susfs_syscall_fn_t orig = READ_ONCE(create_orig_compat_mknodat);

	if (!orig)
		return -ENOSYS;
	if (create_name_hidden((int)regs->regs[0], (const char __user *)regs->regs[1]))
		return -EACCES;
	return orig(regs);
}

static __nocfi noinline long create_wrap_compat_symlinkat(const struct pt_regs *regs)
{
	susfs_syscall_fn_t orig = READ_ONCE(create_orig_compat_symlinkat);

	if (!orig)
		return -ENOSYS;
	if (create_name_hidden((int)regs->regs[1], (const char __user *)regs->regs[2]))
		return -EACCES;
	return orig(regs);
}

static __nocfi noinline long create_wrap_compat_linkat(const struct pt_regs *regs)
{
	susfs_syscall_fn_t orig = READ_ONCE(create_orig_compat_linkat);

	if (!orig)
		return -ENOSYS;
	if (create_name_hidden((int)regs->regs[0], (const char __user *)regs->regs[1]))
		return -ENOENT;
	if (create_name_hidden((int)regs->regs[2], (const char __user *)regs->regs[3]))
		return -EACCES;
	return orig(regs);
}

static struct susfs_fp_hook create_hooks[] = {
	{ .nr = __NR_mkdirat, .nr_compat = COMPAT_MKDIRAT_NR,
	  .name = "create/mkdirat", .sym = "__arm64_sys_mkdirat",
	  .wrapper = create_wrap_mkdirat, .orig_slot = &create_orig_mkdirat,
	  .orig_slot_compat = &create_orig_compat_mkdirat },
	{ .nr = __NR_mknodat, .nr_compat = COMPAT_MKNODAT_NR,
	  .name = "create/mknodat", .sym = "__arm64_sys_mknodat",
	  .wrapper = create_wrap_mknodat, .orig_slot = &create_orig_mknodat,
	  .orig_slot_compat = &create_orig_compat_mknodat },
	{ .nr = __NR_symlinkat, .nr_compat = COMPAT_SYMLINKAT_NR,
	  .name = "create/symlinkat", .sym = "__arm64_sys_symlinkat",
	  .wrapper = create_wrap_symlinkat, .orig_slot = &create_orig_symlinkat,
	  .orig_slot_compat = &create_orig_compat_symlinkat },
	{ .nr = __NR_linkat, .nr_compat = COMPAT_LINKAT_NR,
	  .name = "create/linkat", .sym = "__arm64_sys_linkat",
	  .wrapper = create_wrap_linkat, .orig_slot = &create_orig_linkat,
	  .orig_slot_compat = &create_orig_compat_linkat },
};

int susfs_create_gate_init(void)
{
	int installed = 0;
	int rc;
	int i;

	create_getname_fn = (void *)find_kernel_symbol_exact("getname_flags");
	create_filename_lookup_fn = (void *)find_kernel_symbol_exact("filename_lookup");
	create_putname_fn = (void *)find_kernel_symbol_exact("putname");
	if (!create_getname_fn || !create_filename_lookup_fn || !create_putname_fn) {
		pr_warn("create_gate: missing getname_flags/filename_lookup/putname - the create family keeps answering the kernel's own way\n");
		return -ENOENT;
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
