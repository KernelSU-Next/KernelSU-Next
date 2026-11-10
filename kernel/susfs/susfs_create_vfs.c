/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_create_vfs.c - VFS primitives for the create gate
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

#include "susfs.h"
#include "symbol_resolver.h"
#include "susfs_log.h"
#include "susfs_create_vfs.h"
#include "susfs_create_op.h"
#include "susfs_create_view.h"

static struct filename *(*create_getname_fn)(const char __user *name, int flags, int *empty);
static void (*create_putname_fn)(struct filename *name);
static int (*create_filename_parentat_fn)(int dfd, struct filename *name,
                                          unsigned int flags, struct path *parent,
                                          struct qstr *last, int *type,
                                          const struct path *root);
static struct dentry *(*create_lookup_one_qstr_excl_fn)(const struct qstr *name,
                                                        struct dentry *base,
                                                        unsigned int flags);
static char *(*create_d_path_fn)(const struct path *path, char *buf, int buflen);
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

int susfs_create_vfs_init(void)
{
	create_getname_fn = (void *)find_kernel_symbol_exact("getname_flags");
	create_putname_fn = (void *)find_kernel_symbol_exact("putname");
	if (!create_getname_fn || !create_putname_fn) {
		pr_warn("create_gate: missing getname_flags/putname - the create family keeps answering the kernel's own way\n");
		return -ENOENT;
	}

	create_filename_parentat_fn = (void *)find_kernel_symbol_exact("filename_parentat");
	if (!create_filename_parentat_fn)
		create_filename_parentat_fn = (void *)find_kernel_symbol_exact("__filename_parentat");
	create_lookup_one_qstr_excl_fn = (void *)find_kernel_symbol_exact("lookup_one_qstr_excl");
	create_d_path_fn = (void *)find_kernel_symbol_exact("d_path");
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
	return 0;
}

static __nocfi noinline struct filename *create_getname(const char __user *name)
{
	return create_getname_fn(name, 0, NULL);
}

static __nocfi noinline void create_putname(struct filename *name)
{
	create_putname_fn(name);
}

void susfs_create_vfs_parent_permission(struct inode *dir)
{
	if (!dir)
		return;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
	(void)inode_permission(&nop_mnt_idmap, dir, MAY_WRITE | MAY_EXEC);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
	(void)inode_permission(&init_user_ns, dir, MAY_WRITE | MAY_EXEC);
#else
	(void)inode_permission(dir, MAY_WRITE | MAY_EXEC);
#endif
}

enum susfs_create_op_state {
	CREATE_OP_NEW,
	CREATE_OP_GETNAME,
	CREATE_OP_PARENTAT,
	CREATE_OP_NOT_NAME,
	CREATE_OP_LOCKED,
	CREATE_OP_COMMITTED,
	CREATE_OP_COMMITTED_ERROR,
	CREATE_OP_ERROR,
};

struct susfs_create_op {
	struct filename *name;
	struct path parent;
	struct qstr last;
	int type;
	struct dentry *dentry;
	enum susfs_create_op_state state;
	int err;
	bool parent_held;
	bool write_held;
	bool lock_held;
	bool dentry_held;
};

static void create_op_unlock(struct susfs_create_op *op)
{
	if (op->dentry_held) {
		dput(op->dentry);
		op->dentry = NULL;
		op->dentry_held = false;
	}
	if (op->lock_held) {
		inode_unlock(d_inode(op->parent.dentry));
		op->lock_held = false;
	}
	if (op->write_held) {
		mnt_drop_write(op->parent.mnt);
		op->write_held = false;
	}
}

static __nocfi noinline int create_parentat(int dfd, struct filename *name,
					     struct path *parent, struct qstr *last,
					     int *type)
{
	return create_filename_parentat_fn(dfd, name, 0, parent, last, type, NULL);
}

static __nocfi noinline struct dentry *create_lookup_excl(const struct qstr *last,
							  struct dentry *parent)
{
	return create_lookup_one_qstr_excl_fn(last, parent,
					      LOOKUP_CREATE | LOOKUP_EXCL);
}

struct susfs_create_op *susfs_create_vfs_begin(int dfd, const char __user *path,
					       int *err)
{
	struct susfs_create_op *op;

	if (err)
		*err = 0;
	op = kzalloc(sizeof(*op), GFP_KERNEL);
	if (!op) {
		if (err)
			*err = -ENOMEM;
		return NULL;
	}
	op->state = CREATE_OP_NEW;
	op->name = create_getname(path);
	if (IS_ERR(op->name)) {
		int rc = PTR_ERR(op->name);

		op->name = NULL;
		kfree(op);
		if (err)
			*err = rc;
		return NULL;
	}
	op->state = CREATE_OP_GETNAME;

	op->err = create_parentat(dfd, op->name, &op->parent, &op->last,
				  &op->type);
	if (op->err) {
		op->state = CREATE_OP_ERROR;
	} else {
		op->parent_held = true;
		op->state = (op->type == LAST_NORM) ? CREATE_OP_PARENTAT
						    : CREATE_OP_NOT_NAME;
	}
	if (err)
		*err = op->err;
	return op;
}

bool susfs_create_vfs_is_name(struct susfs_create_op *op)
{
	return op && op->state == CREATE_OP_PARENTAT;
}

int susfs_create_vfs_lock(struct susfs_create_op *op)
{
	int rc;

	if (!op)
		return -EINVAL;
	if (op->state != CREATE_OP_PARENTAT)
		return op->err ? op->err : -EINVAL;

	rc = mnt_want_write(op->parent.mnt);
	if (rc) {
		op->err = rc;
		op->state = CREATE_OP_ERROR;
		return rc;
	}
	op->write_held = true;

	inode_lock_nested(d_inode(op->parent.dentry), I_MUTEX_PARENT);
	op->lock_held = true;

	op->dentry = create_lookup_excl(&op->last, op->parent.dentry);
	if (IS_ERR(op->dentry)) {
		rc = PTR_ERR(op->dentry);
		op->dentry = NULL;
		op->err = rc;
		op->state = CREATE_OP_ERROR;
		create_op_unlock(op);
		return rc;
	}
	op->dentry_held = true;
	op->state = CREATE_OP_LOCKED;
	return 0;
}

int susfs_create_vfs_status(struct susfs_create_op *op,
			    struct susfs_create_view *view)
{
	struct inode *inode;

	if (!op || !view)
		return -EINVAL;
	if (op->state == CREATE_OP_NOT_NAME) {
		memset(view, 0, sizeof(*view));
		view->state = SUSFS_CREATE_ST_NOT_NAME;
		return 1;
	}
	if (op->state != CREATE_OP_LOCKED)
		return op->err ? op->err : 0;
	if (!d_is_positive(op->dentry))
		return 0;

	inode = d_inode(op->dentry);
	if (!inode)
		return 0;
	memset(view, 0, sizeof(*view));
	view->state = SUSFS_CREATE_ST_EXISTS;
	view->ino = inode->i_ino;
	view->s_dev = inode->i_sb->s_dev;
	view->i_uid = inode->i_uid.val;
	view->sb_magic = inode->i_sb->s_magic;
	strscpy(view->name, op->dentry->d_name.name, sizeof(view->name));
	return 1;
}

void susfs_create_vfs_parent_permission_op(struct susfs_create_op *op)
{
	if (!op || !op->parent_held)
		return;
	susfs_create_vfs_parent_permission(d_inode(op->parent.dentry));
}

static __nocfi noinline char *create_d_path(const struct path *path, char *buf,
					   int buflen)
{
	return create_d_path_fn(path, buf, buflen);
}

int susfs_create_vfs_path(struct susfs_create_op *op, char *buf, size_t len)
{
	char tmp[SUSFS_CREATE_PATH_MAX];
	char *p;
	size_t n;

	if (!op || !buf || !len || !create_d_path_fn)
		return -EOPNOTSUPP;
	if (op->state != CREATE_OP_PARENTAT && op->state != CREATE_OP_LOCKED)
		return -EINVAL;
	if (len > sizeof(tmp))
		len = sizeof(tmp);
	p = create_d_path(&op->parent, tmp, (int)len);
	if (IS_ERR_OR_NULL(p))
		return -EIO;
	if (p != tmp) {
		n = strlen(p);
		if (n + 1 > sizeof(tmp))
			return -ENAMETOOLONG;
		memmove(tmp, p, n + 1);
	}
	n = strlen(tmp);
	if (n && tmp[n - 1] != '/') {
		if (n + 1 >= sizeof(tmp))
			return -ENAMETOOLONG;
		tmp[n++] = '/';
	}
	if (n + op->last.len >= sizeof(tmp))
		return -ENAMETOOLONG;
	memcpy(tmp + n, op->last.name, op->last.len);
	n += op->last.len;
	tmp[n] = 0;
	if (n >= len)
		return -ENAMETOOLONG;
	memcpy(buf, tmp, n + 1);
	return 0;
}

void susfs_create_vfs_end(struct susfs_create_op *op)
{
	if (!op)
		return;
	create_op_unlock(op);
	if (op->parent_held)
		path_put(&op->parent);
	if (op->name)
		create_putname(op->name);
	kfree(op);
}

static umode_t create_strip_umask(struct inode *dir, umode_t mode)
{
	if (!IS_POSIXACL(dir))
		mode &= ~current_umask();
	return mode;
}

static __nocfi noinline int create_security_mkdir(const struct path *path,
                                                  struct dentry *dentry, umode_t mode)
{
	if (!create_security_path_mkdir_fn)
		return 0;
	return create_security_path_mkdir_fn(path, dentry, mode);
}

static __nocfi noinline int create_vfs_mkdir(struct path *path,
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

static __nocfi noinline void create_done_path(struct path *path, struct dentry *dentry)
{
	create_done_path_create_fn(path, dentry);
}

/* Commit only the mkdir shape for now.  The security hook runs first, then vfs_mkdir();
 * done_path_create() consumes the parent lock, mount write reference and dentry, so
 * those held bits are cleared here even when the mkdir itself failed. */
int susfs_create_vfs_commit(struct susfs_create_op *op, umode_t mode)
{
	int rc;

	if (!op || op->state != CREATE_OP_LOCKED || !op->dentry_held ||
	    d_is_positive(op->dentry))
		return -EINVAL;
	if (!create_takeover_ready)
		return -EOPNOTSUPP;

	mode = create_strip_umask(d_inode(op->parent.dentry), mode);
	rc = create_security_mkdir(&op->parent, op->dentry, mode);
	if (!rc)
		rc = create_vfs_mkdir(&op->parent, op->dentry, mode);

	create_done_path(&op->parent, op->dentry);
	op->dentry = NULL;
	op->dentry_held = false;
	op->lock_held = false;
	op->write_held = false;
	op->state = rc ? CREATE_OP_COMMITTED_ERROR : CREATE_OP_COMMITTED;
	return rc;
}
