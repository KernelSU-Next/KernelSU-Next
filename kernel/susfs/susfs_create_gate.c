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
#include <linux/mount.h>
#include <linux/security.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
#include <linux/mnt_idmapping.h>
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
#include <linux/user_namespace.h>
#endif
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

/* Full mkdirat takeover.  These are the create-side symbols after the parent
 * lookup; the last lookup is done here so the positive dentry survives for the
 * sus_path identity check. */
static int (*create_filename_parentat_fn)(int dfd, struct filename *name,
					  unsigned int flags, struct path *parent,
					  struct qstr *last, int *type,
					  const struct path *root);
static struct dentry *(*create_lookup_one_qstr_excl_fn)(const struct qstr *name,
							struct dentry *base,
							unsigned int flags);
static int (*create_security_path_mkdir_fn)(const struct path *path,
					    struct dentry *dentry, umode_t mode);
static void (*create_done_path_create_fn)(struct path *path, struct dentry *dentry);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
static int (*create_vfs_mkdir_fn)(struct mnt_idmap *idmap, struct inode *dir,
				  struct dentry *dentry, umode_t mode);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
static int (*create_vfs_mkdir_fn)(struct user_namespace *mnt_userns,
				  struct inode *dir, struct dentry *dentry,
				  umode_t mode);
#else
static int (*create_vfs_mkdir_fn)(struct inode *dir, struct dentry *dentry,
				  umode_t mode);
#endif
static bool create_takeover_ready;

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

	if (!sus_path_user_rules_present())
		return false;
	if (!create_getname_fn || !create_filename_lookup_fn || !create_putname_fn)
		return false;

	name = create_getname_fn(path, 0, NULL);
	if (IS_ERR(name))
		return false;

	rc = create_filename_lookup_fn(dfd, name, 0, &p, NULL);
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
	/* filename_lookup() consumed `name` through 5.14; 5.15 removed that putname() and
	 * made the caller own it.  Do not put it twice on 5.10/5.14: the second putname()
	 * is the double free that panics init (create_name_hidden -> putname). */
#else
	create_putname_fn(name);
#endif
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

static umode_t create_strip_umask(struct inode *dir, umode_t mode)
{
	if (!IS_POSIXACL(dir))
		mode &= ~current_umask();
	return mode;
}

static __nocfi noinline int create_call_vfs_mkdir(struct path *path,
						 struct dentry *dentry, umode_t mode)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
	return create_vfs_mkdir_fn(mnt_idmap(path->mnt), d_inode(path->dentry),
				   dentry, mode);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
	return create_vfs_mkdir_fn(mnt_user_ns(path->mnt), d_inode(path->dentry),
				   dentry, mode);
#else
	return create_vfs_mkdir_fn(d_inode(path->dentry), dentry, mode);
#endif
}

/* A local filename_create() that keeps the positive dentry on EEXIST, so the
 * sus_path identity gate can answer EACCES instead of dropping straight to EEXIST.
 * There is exactly one parent+last lookup here, the same one the kernel would do. */
static __nocfi noinline long create_takeover_mkdirat(int dfd,
						     const char __user *pathname,
						     umode_t mode)
{
	struct filename *name;
	struct path path;
	struct qstr last;
	int type = 0, err, err2;
	struct dentry *dentry;
	bool hidden;

	if (!create_takeover_ready)
		return -ENOSYS;

	name = create_getname_fn(pathname, 0, NULL);
	if (IS_ERR(name))
		return PTR_ERR(name);

	err = create_filename_parentat_fn(dfd, name, 0, &path, &last, &type, NULL);
	if (err) {
		create_putname_fn(name);
		return err;
	}
	/* "/", "foo/." and "foo/..": filename_create() answers EEXIST for these, and
	 * mkdir -p relies on it when it walks the components of an existing path. */
	if (type != LAST_NORM) {
		path_put(&path);
		create_putname_fn(name);
		return -EEXIST;
	}

	err2 = mnt_want_write(path.mnt);
	inode_lock_nested(d_inode(path.dentry), I_MUTEX_PARENT);
	dentry = create_lookup_one_qstr_excl_fn(&last, path.dentry,
						LOOKUP_CREATE | LOOKUP_EXCL);
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		inode_unlock(d_inode(path.dentry));
		if (!err2)
			mnt_drop_write(path.mnt);
		path_put(&path);
		create_putname_fn(name);
		return err;
	}

	if (d_is_positive(dentry)) {
		hidden = sus_path_inode_hidden_public(d_inode(dentry));
		dput(dentry);
		inode_unlock(d_inode(path.dentry));
		if (!err2)
			mnt_drop_write(path.mnt);
		path_put(&path);
		create_putname_fn(name);
		return hidden ? -EACCES : -EEXIST;
	}

	if (err2) {
		dput(dentry);
		inode_unlock(d_inode(path.dentry));
		path_put(&path);
		create_putname_fn(name);
		return err2;
	}

	err = create_security_path_mkdir_fn ?
		create_security_path_mkdir_fn(&path, dentry,
					      create_strip_umask(d_inode(path.dentry), mode)) : 0;
	if (!err)
		err = create_call_vfs_mkdir(&path, dentry, mode);

	create_done_path_create_fn(&path, dentry);
	create_putname_fn(name);
	return err;
}

static __nocfi noinline long create_wrap_mkdirat(const struct pt_regs *regs)
{
	return create_takeover_mkdirat((int)regs->regs[0],
				       (const char __user *)regs->regs[1],
				       (umode_t)regs->regs[2]);
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
	return create_takeover_mkdirat((int)regs->regs[0],
				       (const char __user *)regs->regs[1],
				       (umode_t)regs->regs[2]);
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
	  .wrapper = create_wrap_mkdirat, .wrapper_compat = create_wrap_compat_mkdirat,
	  .orig_slot = &create_orig_mkdirat,
	  .orig_slot_compat = &create_orig_compat_mkdirat },
	{ .nr = __NR_mknodat, .nr_compat = COMPAT_MKNODAT_NR,
	  .name = "create/mknodat", .sym = "__arm64_sys_mknodat",
	  .wrapper = create_wrap_mknodat, .wrapper_compat = create_wrap_compat_mknodat,
	  .orig_slot = &create_orig_mknodat,
	  .orig_slot_compat = &create_orig_compat_mknodat },
	{ .nr = __NR_symlinkat, .nr_compat = COMPAT_SYMLINKAT_NR,
	  .name = "create/symlinkat", .sym = "__arm64_sys_symlinkat",
	  .wrapper = create_wrap_symlinkat, .wrapper_compat = create_wrap_compat_symlinkat,
	  .orig_slot = &create_orig_symlinkat,
	  .orig_slot_compat = &create_orig_compat_symlinkat },
	{ .nr = __NR_linkat, .nr_compat = COMPAT_LINKAT_NR,
	  .name = "create/linkat", .sym = "__arm64_sys_linkat",
	  .wrapper = create_wrap_linkat, .wrapper_compat = create_wrap_compat_linkat,
	  .orig_slot = &create_orig_linkat,
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

	create_filename_parentat_fn = (void *)find_kernel_symbol_exact("filename_parentat");
	if (!create_filename_parentat_fn)
		create_filename_parentat_fn = (void *)find_kernel_symbol_exact("__filename_parentat");
	create_lookup_one_qstr_excl_fn = (void *)find_kernel_symbol_exact("lookup_one_qstr_excl");
	create_security_path_mkdir_fn = (void *)find_kernel_symbol_exact("security_path_mkdir");
	create_done_path_create_fn = (void *)find_kernel_symbol_exact("done_path_create");
	create_vfs_mkdir_fn = (void *)find_kernel_symbol_exact("vfs_mkdir");
	create_takeover_ready = create_filename_parentat_fn &&
				create_lookup_one_qstr_excl_fn &&
				create_done_path_create_fn &&
				create_vfs_mkdir_fn;
	if (!create_takeover_ready)
		pr_warn("create_gate: mkdirat takeover unavailable (parentat=%px lookup_one=%px done_path_create=%px vfs_mkdir=%px)\n",
			create_filename_parentat_fn, create_lookup_one_qstr_excl_fn,
			create_done_path_create_fn, create_vfs_mkdir_fn);
	else
		SUSFS_LOGI("create_gate: mkdirat takeover armed (path_mkdir=%s)\n",
			   create_security_path_mkdir_fn ? "yes" : "no");

	/* Unconditional: a supercall can add a rule before/after this layer, so the
	 * entries must be there whenever the module is.  An entry that cannot be
	 * installed leaves the others working and the kernel's own answer in place. */
	for (i = 0; i < (int)ARRAY_SIZE(create_hooks); i++) {
		struct susfs_fp_hook *h = &create_hooks[i];

		if (h->nr == __NR_mkdirat && !create_takeover_ready) {
			pr_warn("create_gate: mkdirat takeover not ready - leaving the native syscall alone\n");
			continue;
		}
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
