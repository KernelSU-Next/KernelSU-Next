// SPDX-License-Identifier: GPL-2.0-only
/*
 * susfs_selinux_hide.c - selinux_hide entry layer, LSM replacement form.
 *
 * SUSFS 2.4.0 hides an app's su context at three SELinux entry points:
 *   selinux_inode_setxattr / selinux_inode_post_setxattr / selinux_setprocattr
 * by running a copy of the function with susfs_security_context_to_sid(_force).
 * A loadable module cannot replace the context parser inside security/selinux/ss,
 * so this layer replaces the three SELinux hook slots instead (ksu_lsm_hook) and:
 *   - app caller (uid % 100000 >= 10000) + hide enabled + value is a su context
 *     -> succeed, exactly like the upstream force parse did (0 / size);
 *   - everything else -> call the saved original, so SELinux still decides.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/xattr.h>

#include "lsm_hook.h"
#include "symbol_resolver.h"
#include "susfs_selinux_hide.h"
#include "susfs_log.h"

/* 6.2 moved the first argument from user_namespace to mnt_idmap. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 2, 0)
typedef struct mnt_idmap susfs_lsm_idmap_t;
#else
typedef struct user_namespace susfs_lsm_idmap_t;
#endif

#define SUSFS_SELINUX_HIDE_MIN_UID 10000

/* Default: follow KernelSU's own switch; selinux_hide_force=1 makes the layer
 * effective even when that variable is not resolvable (older managers). */
static bool susfs_selinux_hide_force;
module_param_named(selinux_hide_force, susfs_selinux_hide_force, bool, 0644);

static bool *pfn_ksu_selinux_hide_running;

static bool susfs_selinux_hide_enabled(void)
{
	if (susfs_selinux_hide_force)
		return true;
	return pfn_ksu_selinux_hide_running && *pfn_ksu_selinux_hide_running;
}

/* The context names are fixed strings (u:r:su:, u:r:ksu:, u:r:magisk:,
 * u:r:zygisk:), so a bounded substring search is enough - no sid lookup. */
static bool susfs_selinux_is_su_context(const void *value, size_t size)
{
	static const char *const needles[] = {
		"u:r:su:", "u:r:ksu:", "u:r:magisk:", "u:r:zygisk:", "u:r:ksud:",
	};
	char buf[256];
	size_t n = size;
	size_t i;

	if (!value || !n)
		return false;
	if (n > sizeof(buf) - 1)
		n = sizeof(buf) - 1;
	memcpy(buf, value, n);
	buf[n] = 0;
	for (i = 0; i < ARRAY_SIZE(needles); i++)
		if (strstr(buf, needles[i]))
			return true;
	return false;
}

static bool susfs_selinux_should_spoof(const char *name, const void *value,
				       size_t size)
{
	if (current_uid().val < SUSFS_SELINUX_HIDE_MIN_UID)
		return false;
	if (!susfs_selinux_hide_enabled())
		return false;
	if (!value || !size)
		return false;
	if (!name ||
	    (strcmp(name, XATTR_NAME_SELINUX) && strcmp(name, "current")))
		return false;
	return susfs_selinux_is_su_context(value, size);
}

typedef int (*susfs_lsm_inode_setxattr_fn)(susfs_lsm_idmap_t *idmap,
					   struct dentry *dentry,
					   const char *name,
					   const void *value, size_t size,
					   int flags);
typedef void (*susfs_lsm_inode_post_setxattr_fn)(struct dentry *dentry,
						 const char *name,
						 const void *value, size_t size,
						 int flags);
typedef int (*susfs_lsm_setprocattr_fn)(const char *name, void *value,
					size_t size);

/* The KSU_LSM_HOOK_INIT macros below name the replacements, so declare them
 * first. */
static int __nocfi susfs_selinux_hide_inode_setxattr(susfs_lsm_idmap_t *idmap,
						     struct dentry *dentry,
						     const char *name,
						     const void *value,
						     size_t size, int flags);
static void __nocfi susfs_selinux_hide_inode_post_setxattr(struct dentry *dentry,
							   const char *name,
							   const void *value,
							   size_t size,
							   int flags);
static int __nocfi susfs_selinux_hide_setprocattr(const char *name, void *value,
						  size_t size);

static struct ksu_lsm_hook sh_setxattr =
	KSU_LSM_HOOK_INIT(inode_setxattr, "selinux_inode_setxattr",
			  susfs_selinux_hide_inode_setxattr, 0);
static struct ksu_lsm_hook sh_post_setxattr =
	KSU_LSM_HOOK_INIT(inode_post_setxattr, "selinux_inode_post_setxattr",
			  susfs_selinux_hide_inode_post_setxattr, 0);
static struct ksu_lsm_hook sh_setprocattr =
	KSU_LSM_HOOK_INIT(setprocattr, "selinux_setprocattr",
			  susfs_selinux_hide_setprocattr, 0);

static int __nocfi
susfs_selinux_hide_inode_setxattr(susfs_lsm_idmap_t *idmap, struct dentry *dentry,
				  const char *name, const void *value,
				  size_t size, int flags)
{
	susfs_lsm_inode_setxattr_fn orig =
		(susfs_lsm_inode_setxattr_fn)sh_setxattr.original;

	if (susfs_selinux_should_spoof(name, value, size))
		return 0;	/* upstream force parse: the write succeeds */
	return orig ? orig(idmap, dentry, name, value, size, flags) : 0;
}

static void __nocfi
susfs_selinux_hide_inode_post_setxattr(struct dentry *dentry, const char *name,
				       const void *value, size_t size, int flags)
{
	susfs_lsm_inode_post_setxattr_fn orig =
		(susfs_lsm_inode_post_setxattr_fn)sh_post_setxattr.original;

	if (susfs_selinux_should_spoof(name, value, size))
		return;		/* the inode keeps its old label, as before */
	if (orig)
		orig(dentry, name, value, size, flags);
}

static int __nocfi
susfs_selinux_hide_setprocattr(const char *name, void *value, size_t size)
{
	susfs_lsm_setprocattr_fn orig =
		(susfs_lsm_setprocattr_fn)sh_setprocattr.original;

	if (name && !strcmp(name, "current") &&
	    susfs_selinux_should_spoof("current", value, size))
		return (int)size;	/* upstream: commit_creds + return size */
	return orig ? orig(name, value, size) : -EINVAL;
}

int susfs_selinux_hide_init(void)
{
	int ret;

	pfn_ksu_selinux_hide_running = (bool *)find_kernel_symbol_exact(
		"ksu_selinux_hide_running");
	if (!pfn_ksu_selinux_hide_running)
		SUSFS_LOGI("selinux_hide: ksu_selinux_hide_running not found; "
			   "enable with selinux_hide_force=1\n");

	ret = ksu_lsm_hook(&sh_setxattr);
	if (ret) {
		SUSFS_LOGI("selinux_hide: setxattr hook failed %d\n", ret);
		return ret;
	}
	ret = ksu_lsm_hook(&sh_post_setxattr);
	if (ret) {
		SUSFS_LOGI("selinux_hide: post_setxattr hook failed %d\n", ret);
		ksu_lsm_unhook(&sh_setxattr);
		return ret;
	}
	ret = ksu_lsm_hook(&sh_setprocattr);
	if (ret) {
		SUSFS_LOGI("selinux_hide: setprocattr hook failed %d\n", ret);
		ksu_lsm_unhook(&sh_post_setxattr);
		ksu_lsm_unhook(&sh_setxattr);
		return ret;
	}
	SUSFS_LOGI("selinux_hide: armed (uid >= %d, force=%d, ksu flag=%s)\n",
		   SUSFS_SELINUX_HIDE_MIN_UID, (int)susfs_selinux_hide_force,
		   pfn_ksu_selinux_hide_running ? "found" : "missing");
	return 0;
}

void susfs_selinux_hide_exit(void)
{
	ksu_lsm_unhook(&sh_setprocattr);
	ksu_lsm_unhook(&sh_post_setxattr);
	ksu_lsm_unhook(&sh_setxattr);
}
