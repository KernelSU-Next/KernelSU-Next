// SPDX-License-Identifier: GPL-2.0-only
/*
 * susfs_priority_uid.c - priority_uid_filter: filtered app uids stay invisible
 * to getpriority()/setpriority()/ioprio_get()/ioprio_set().
 *
 * Same behaviour as upstream 2.4.0 (KSU_SUSFS_PRIORITY_UID_FILTER): the manager
 * sends a comma-separated package list, the module resolves the uids from
 * /data/system/packages.list and publishes a 10000-bit bitmap over RCU.  The
 * syscall layer (susfs_priority_hooks.c) answers -EINVAL for those uids when the
 * caller is an app (uid % 100000 >= 10000), exactly like the upstream
 * susfs_is_priority_uid_filtered() call sites in kernel/sys.c and block/ioprio.c.
 *
 * Upstream re-parses through KernelSU's package observer; an LKM has no such
 * observer, so the list is rebuilt whenever the manager sends the command again
 * (and through the priority_filter module parameter, for testing).
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/mm.h>		/* kvmalloc/kvfree (5.10 declares them here) */
#include <linux/vmalloc.h>
#include <linux/rcupdate.h>
#include <linux/bitops.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/uidgid.h>
#include <linux/workqueue.h>

#include "susfs_abi.h"
#include "susfs_priority_uid.h"
#include "susfs_log.h"
#include "symbol_resolver.h"	/* find_kernel_symbol_exact_any: module symbol */
#include "susfs_hook_engine.h"	/* susfs_hook_override: the hook resolver's table */
#include "hk_inline.h"		/* entry-stub inline hook for KernelSU's observer */

/* KernelSU's package observer callback (kernel/manager/pkg_observer.c,
 * "ksu_defer_track_throne"): called from /data/system's fsnotify event, i.e. on
 * every package install/uninstall/update.  It starts with paciasp, so it is
 * hooked through the entry stub (susfs_ksu_stub.S). */
extern void susfs_ksu_observer_stub(void);
extern unsigned long susfs_ksu_observer_tramp;
static struct hk_inline ksu_observer_hook;
static bool ksu_observer_armed;

/* Last package list the manager sent.  Re-reads use it, because the fsnotify
 * event only says "something under /data/system changed", not which packages
 * are wanted. */
static char *priority_apps;
static struct work_struct priority_rebuild_work;
static bool priority_work_inited;

#define PRIORITY_UID_MIN 10000
#define PRIORITY_UID_MAX 19999
#define PRIORITY_UID_NUM (PRIORITY_UID_MAX - PRIORITY_UID_MIN + 1)

struct susfs_priority_bitmap {
	struct rcu_head rcu;
	unsigned long bits[BITS_TO_LONGS(PRIORITY_UID_NUM)];
};

static struct susfs_priority_bitmap __rcu *priority_bitmap;
static DEFINE_MUTEX(priority_lock);

/* Parse one comma/whitespace-separated token and mark its uid in bits.
 * Returns 1 on a match, 0 when the package is not in the list, -errno on a
 * malformed package list. */
static int priority_mark_package(unsigned long *bits, const char *packages,
				 const char *token, size_t token_len)
{
	const char *p = packages;

	while (*p) {
		const char *name, *uid_start;
		unsigned long uid;
		char namebuf[256];
		size_t name_len;

		while (*p == ' ' || *p == '\t' || *p == '\n')
			p++;
		if (!*p)
			break;
		name = p;
		while (*p && *p != ' ' && *p != '\t' && *p != '\n')
			p++;
		name_len = p - name;
		while (*p == ' ' || *p == '\t')
			p++;
		uid_start = p;
		/* Parse the uid in place: kstrtoul()/kstrtoull() reject a string
		 * that does not end right after the number (measured: it failed on
		 * "10055" followed by the next column). */
		uid = 0;
		while (*p >= '0' && *p <= '9') {
			uid = uid * 10 + (*p - '0');
			p++;
		}
		if (uid_start == p || name_len == 0 || name_len >= sizeof(namebuf))
			goto next_line;
		if (name_len != token_len || strncmp(name, token, token_len))
			goto next_line;
		uid %= 100000;
		if (uid < PRIORITY_UID_MIN || uid > PRIORITY_UID_MAX)
			goto next_line;
		set_bit(uid - PRIORITY_UID_MIN, bits);
		return 1;
next_line:
		/* The remaining columns (data dir, permissions, ...) are not
		 * package entries; without this the loop would read the next
		 * column of the same line as a name (measured). */
		while (*p && *p != '\n')
			p++;
	}
	return 0;
}

/* Rebuild the bitmap from /data/system/packages.list for the given package
 * list.  Returns the number of packages found, or -errno. */
static int priority_build_bitmap(const char *apps, unsigned long *bits)
{
	struct file *f;
	loff_t pos = 0;
	char *buf;
	ssize_t n;
	int found = 0;
	const char *p = apps;

	f = filp_open("/data/system/packages.list", O_RDONLY, 0);
	if (IS_ERR(f)) {
		SUSFS_LOGI("priority_uid: open packages.list failed %ld\n",
			   PTR_ERR(f));
		return PTR_ERR(f);
	}
	buf = kvmalloc(1024 * 1024, GFP_KERNEL);
	if (!buf) {
		filp_close(f, NULL);
		return -ENOMEM;
	}
	n = kernel_read(f, buf, 1024 * 1024 - 1, &pos);
	filp_close(f, NULL);
	if (n <= 0) {
		SUSFS_LOGI("priority_uid: read packages.list n=%zd\n", n);
		kvfree(buf);
		return n < 0 ? n : -ENOENT;
	}
	buf[n] = 0;

	while (*p) {
		const char *tok;

		while (*p == ',' || *p == ' ' || *p == '\t')
			p++;
		if (!*p)
			break;
		tok = p;
		while (*p && *p != ',' && *p != ' ' && *p != '\t')
			p++;
		if (priority_mark_package(bits, buf, tok, p - tok) > 0)
			found++;
	}
	kvfree(buf);
	return found;
}

static int priority_rebuild(const char *apps)
{
	struct susfs_priority_bitmap *new_bitmap, *old;
	int found;

	new_bitmap = kzalloc(sizeof(*new_bitmap), GFP_KERNEL);
	if (!new_bitmap)
		return -ENOMEM;
	found = priority_build_bitmap(apps, new_bitmap->bits);
	if (found < 0) {
		kfree(new_bitmap);
		return found;
	}
	mutex_lock(&priority_lock);
	old = rcu_dereference_protected(priority_bitmap,
					lockdep_is_held(&priority_lock));
	rcu_assign_pointer(priority_bitmap, new_bitmap);
	mutex_unlock(&priority_lock);
	if (old)
		kfree_rcu(old, rcu);
	SUSFS_LOGI("priority_uid: %d package(s) filtered (%s)\n", found, apps);
	return 0;
}

int susfs_priority_uid_set(const char *apps)
{
	char *copy;

	if (!apps)
		return -EINVAL;
	copy = kstrdup(apps, GFP_KERNEL);
	if (!copy)
		return -ENOMEM;
	mutex_lock(&priority_lock);
	kfree(priority_apps);
	priority_apps = copy;
	mutex_unlock(&priority_lock);
	return priority_rebuild(apps);
}

/* KernelSU observer hit (entry stub on ksu_handle_inode_event, fsnotify
 * context): only defer, and only for packages.list like KernelSU itself.
 * The rebuild reads /data/system/packages.list, which may sleep. */
void susfs_priority_observer_hit(const void *qstr)
{
	const u32 *q = (const u32 *)qstr;

	/* struct qstr in 5.15: hash @0, len @4, name @8. */
	if (!q || q[1] != 13 || memcmp(*(const void *const *)(q + 2),
				       "packages.list", 13))
		return;
	if (priority_work_inited && priority_apps)
		schedule_work(&priority_rebuild_work);
}

static void priority_rebuild_work_fn(struct work_struct *w)
{
	char *apps;

	mutex_lock(&priority_lock);
	apps = priority_apps ? kstrdup(priority_apps, GFP_KERNEL) : NULL;
	mutex_unlock(&priority_lock);
	if (!apps)
		return;
	SUSFS_LOGI("priority_uid: observer event, re-reading packages.list\n");
	priority_rebuild(apps);
	kfree(apps);
}

bool susfs_priority_uid_filtered(uid_t uid)
{
	struct susfs_priority_bitmap *bitmap;
	unsigned int app_id = uid % 100000;
	bool filtered = false;

	if (app_id < PRIORITY_UID_MIN || app_id > PRIORITY_UID_MAX)
		return false;
	rcu_read_lock();
	bitmap = rcu_dereference(priority_bitmap);
	if (bitmap)
		filtered = test_bit(app_id - PRIORITY_UID_MIN, bitmap->bits);
	rcu_read_unlock();
	return filtered;
}

void susfs_priority_uid_supercall(void __user **arg)
{
	struct st_susfs_priority_uid_filter *info;
	int err = 0;

	info = kvmalloc(sizeof(*info), GFP_KERNEL | __GFP_ZERO);
	if (!info)
		return;
	if (copy_from_user(info, (struct st_susfs_priority_uid_filter __user *)*arg,
			   sizeof(*info))) {
		err = -EFAULT;
		goto out;
	}
	/* the app name string must be NUL terminated within the buffer */
	if (!memchr(info->app_strings, 0, sizeof(info->app_strings))) {
		err = -EINVAL;
		goto out;
	}
	if (!info->app_strings[0])
		err = -EINVAL;
	else
		err = priority_rebuild(info->app_strings);
out:
	if (copy_to_user(&((struct st_susfs_priority_uid_filter __user *)*arg)->err,
			 &err, sizeof(err)))
		err = -EFAULT;
	if (err)
		SUSFS_LOGI("CMD_SUSFS_SET_PRIORITY_UID_FILTER -> ret: %d\n", err);
	kvfree(info);
}

/* ---- module parameter: same list, easier to drive from a shell during tests ---- */
static int priority_filter_param_set(const char *val, const struct kernel_param *kp)
{
	/* susfs_priority_uid_set() also records the list for the observer
	 * rebuild; rebuilding directly left priority_apps NULL, so observer
	 * events could never trigger a re-read (measured). */
	return susfs_priority_uid_set(val);
}

static const struct kernel_param_ops priority_filter_ops = {
	.set = priority_filter_param_set,
};

module_param_cb(priority_filter, &priority_filter_ops, NULL, 0644);

int susfs_priority_init(void)
{
	int err;

	err = susfs_priority_uid_init();
	if (err)
		return err;
	err = susfs_priority_hooks_init();
	if (err) {
		susfs_priority_uid_exit();
		return err;
	}
	return 0;
}

void susfs_priority_exit(void)
{
	susfs_priority_hooks_exit();
	susfs_priority_uid_exit();
}

int susfs_priority_uid_init(void)
{
	struct susfs_priority_bitmap *bitmap;
	int err;

	INIT_WORK(&priority_rebuild_work, priority_rebuild_work_fn);
	priority_work_inited = true;

	/* Follow KernelSU's own package observer: the entry-stub form is PAC-safe
	 * (the callback starts with paciasp).  Not fatal: without it the list is
	 * rebuilt only when the manager sends the command again.
	 *
	 * The hook resolver ignores module symbols by design, and this one lives
	 * in the kernelsu module, so hand its address over through the override
	 * table (found with the any-symbol lookup). */
	{
		unsigned long obs_addr = find_kernel_symbol_exact_any(
			"ksu_handle_inode_event");

		if (obs_addr)
			susfs_hook_override("ksu_handle_inode_event", obs_addr);
	}
	err = hk_inline_hook_stub(&ksu_observer_hook, "ksu_handle_inode_event",
				  (unsigned long)susfs_ksu_observer_stub,
				  &susfs_ksu_observer_tramp);
	if (!err) {
		ksu_observer_armed = true;
		SUSFS_LOGI("priority_uid: KernelSU observer hook armed\n");
	} else {
		SUSFS_LOGI("priority_uid: KernelSU observer hook unavailable %d; "
			   "manual re-set only\n", err);
	}

	/* The module parameter is parsed BEFORE the layers run, so a
	 * priority_filter=... on the load line has already published a real
	 * bitmap; do not overwrite it with an empty one. */
	mutex_lock(&priority_lock);
	if (rcu_dereference_protected(priority_bitmap,
				      lockdep_is_held(&priority_lock))) {
		mutex_unlock(&priority_lock);
		SUSFS_LOGI("priority_uid: armed (uid %d..%d, list from parameters)\n",
			   PRIORITY_UID_MIN, PRIORITY_UID_MAX);
		return 0;
	}
	mutex_unlock(&priority_lock);

	bitmap = kzalloc(sizeof(*bitmap), GFP_KERNEL);
	if (!bitmap)
		return -ENOMEM;
	rcu_assign_pointer(priority_bitmap, bitmap);
	SUSFS_LOGI("priority_uid: armed (uid %d..%d)\n",
		   PRIORITY_UID_MIN, PRIORITY_UID_MAX);
	return 0;
}

void susfs_priority_uid_exit(void)
{
	struct susfs_priority_bitmap *bitmap;

	if (priority_work_inited) {
		cancel_work_sync(&priority_rebuild_work);
		priority_work_inited = false;
	}
	if (ksu_observer_armed) {
		hk_inline_unhook(&ksu_observer_hook);
		ksu_observer_armed = false;
	}
	mutex_lock(&priority_lock);
	bitmap = rcu_dereference_protected(priority_bitmap,
					   lockdep_is_held(&priority_lock));
	rcu_assign_pointer(priority_bitmap, NULL);
	kfree(priority_apps);
	priority_apps = NULL;
	mutex_unlock(&priority_lock);
	if (bitmap)
		kfree_rcu(bitmap, rcu);
}
