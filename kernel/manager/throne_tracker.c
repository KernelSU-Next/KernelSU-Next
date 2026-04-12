#include <linux/err.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/namei.h>
#include <linux/cred.h>

#include "policy/allowlist.h"
#include "apk_sign.h"
#include "klog.h" // IWYU pragma: keep
#include "ksu.h"
#include "manager_identity.h"
#include "throne_tracker.h"
#include "compat/kernel_compat.h"

uid_t ksu_manager_appid = KSU_INVALID_APPID;

#define SYSTEM_PACKAGES_LIST_PATH "/data/system/packages.list"

struct uid_data {
	struct list_head list;
	u32 uid;
	char package[KSU_MAX_PACKAGE_NAME];
};

static void crown_manager(const char *apk, struct list_head *uid_data)
{
	char pkg[KSU_MAX_PACKAGE_NAME];
	if (get_pkg_from_apk_path(pkg, apk) < 0) {
		pr_err("Failed to get package name from apk path: %s\n", apk);
		return;
	}

	pr_info("manager pkg: %s\n", pkg);

	struct list_head *list = (struct list_head *)uid_data;
	struct uid_data *np;

	list_for_each_entry (np, list, list) {
		if (strncmp(np->package, pkg, KSU_MAX_PACKAGE_NAME) == 0) {
			pr_info("Crowning manager: %s(uid=%d)\n", pkg, np->uid);
			ksu_set_manager_appid(np->uid);
			break;
		}
	}
}

#define DATA_PATH_LEN 384 // 384 is enough for /data/app/<package>/base.apk

struct data_path {
	char dirpath[DATA_PATH_LEN];
	int depth;
	struct list_head list;
};

struct apk_path_hash {
	unsigned int hash;
	bool exists;
	struct list_head list;
};

struct my_dir_context {
	struct dir_context ctx;
	struct list_head *data_path_list;
	char *parent_dir;
	void *private_data;
	int depth;
	int *stop;
};
// https://docs.kernel.org/filesystems/porting.html
// filldir_t (readdir callbacks) calling conventions have changed. Instead of returning 0 or -E... it returns bool now. false means "no more" (as -E... used to) and true - "keep going" (as 0 in old calling conventions). Rationale: callers never looked at specific -E... values anyway. -> iterate_shared() instances require no changes at all, all filldir_t ones in the tree converted.
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
#define FILLDIR_RETURN_TYPE bool
#define FILLDIR_ACTOR_CONTINUE true
#define FILLDIR_ACTOR_STOP false
#else
#define FILLDIR_RETURN_TYPE int
#define FILLDIR_ACTOR_CONTINUE 0
#define FILLDIR_ACTOR_STOP -EINVAL
#endif
extern bool is_manager_apk(char *path);
FILLDIR_RETURN_TYPE my_actor(struct dir_context *ctx, const char *name,
							int namelen, loff_t off, u64 ino,
							unsigned int d_type)
{
	struct my_dir_context *my_ctx =
		container_of(ctx, struct my_dir_context, ctx);

	// we put the apk path we collected here
	char *candidate_path = (char *)my_ctx->private_data;

	char dirpath[DATA_PATH_LEN];

	if (!my_ctx) {
		pr_err("Invalid context\n");
		return FILLDIR_ACTOR_STOP;
	}
	if (my_ctx->stop && *my_ctx->stop) {
		pr_info("Stop searching\n");
		return FILLDIR_ACTOR_STOP;
	}

	if (!strncmp(name, "..", namelen) || !strncmp(name, ".", namelen))
		return FILLDIR_ACTOR_CONTINUE; // Skip "." and ".."

	if ((d_type == DT_DIR || d_type == DT_UNKNOWN) && namelen >= 8 && !strncmp(name, "vmdl", 4) &&
		!strncmp(name + namelen - 4, ".tmp", 4)) {
		pr_info("Skipping directory: %.*s\n", namelen, name);
		return FILLDIR_ACTOR_CONTINUE; // Skip staging package
	}

	if (snprintf(dirpath, DATA_PATH_LEN, "%s/%.*s", my_ctx->parent_dir, namelen,
				name) >= DATA_PATH_LEN) {
		pr_err("Path too long: %s/%.*s\n", my_ctx->parent_dir, namelen, name);
		return FILLDIR_ACTOR_CONTINUE;
	}

	if ((d_type == DT_DIR || d_type == DT_UNKNOWN) && my_ctx->depth > 0 &&
		(my_ctx->stop && !*my_ctx->stop)) {
		struct data_path *data = kzalloc(sizeof(struct data_path), GFP_KERNEL);

		if (!data) {
			pr_err("Failed to allocate memory for %s\n", dirpath);
			return FILLDIR_ACTOR_CONTINUE;
		}

		strscpy(data->dirpath, dirpath, DATA_PATH_LEN);
		data->depth = my_ctx->depth - 1;
		list_add_tail(&data->list, my_ctx->data_path_list);

		return FILLDIR_ACTOR_CONTINUE;
	}

	// now put this on candidate_path
	if (d_type == DT_REG && !strncmp(name, "base.apk", 8)) {
		snprintf(candidate_path, DATA_PATH_LEN, "%s/%.*s", my_ctx->parent_dir, namelen, name);
	}

	return FILLDIR_ACTOR_CONTINUE;
}

void search_manager(const char *path, int depth, struct list_head *uid_data)
{
	int i, stop = 0;
	struct list_head data_path_list;
	INIT_LIST_HEAD(&data_path_list);

	// First depth
	struct data_path data;
	strscpy(data.dirpath, path, DATA_PATH_LEN);
	data.depth = depth;
	list_add_tail(&data.list, &data_path_list);

	// we put the apk path we collected here
	char candidate_path[DATA_PATH_LEN];

	for (i = depth; i >= 0; i--) {
		struct data_path *pos, *n;

		list_for_each_entry_safe (pos, n, &data_path_list, list) {
			struct my_dir_context ctx = { .ctx.actor = my_actor,
										.data_path_list = &data_path_list,
										.parent_dir = pos->dirpath,
										.private_data = candidate_path,
										.depth = pos->depth,
										.stop = &stop };

			// make sure to clean buffer on every iteration
			memset(candidate_path, 0, DATA_PATH_LEN);

			struct file *file;

			if (!stop) {
				file = ksu_filp_open_compat(pos->dirpath, O_RDONLY | O_NOFOLLOW, 0);
				if (IS_ERR(file)) {
					pr_err("Failed to open directory: %s, err: %ld\n",
						pos->dirpath, PTR_ERR(file));
					goto skip_iterate;
				}

				iterate_dir(file, &ctx.ctx);
				filp_close(file, NULL);

				// ^ oh so thats the issue!
				// we were calling is_manager_apk inside iterate_dir
				// now we defer file opens after iterate_dir
				// this way we dont open apks while inside that
				if (!strstarts(candidate_path, "/data/ap") )
					goto skip_iterate;

				bool is_manager = is_manager_apk(candidate_path);
				pr_info("Found new base.apk at path: %s, is_manager: %d\n", candidate_path, is_manager);

				if (likely(!is_manager))
					goto skip_iterate;

				crown_manager(candidate_path, uid_data);
				stop = 1;
			}
		skip_iterate:
			list_del(&pos->list);
			if (pos != &data)
				kfree(pos);
		}
	}
}

static bool is_uid_exist(uid_t uid, char *package, void *data)
{
	struct list_head *list = (struct list_head *)data;
	struct uid_data *np;

	bool exist = false;
	list_for_each_entry (np, list, list) {
		if (np->uid == uid % PER_USER_RANGE &&
			strncmp(np->package, package, KSU_MAX_PACKAGE_NAME) == 0) {
			exist = true;
			break;
		}
	}
	return exist;
}

// Helper to know if Android is modifying the file
static bool is_lock_held(const char *path) 
{
	struct path kpath;

	if (kern_path(path, 0, &kpath))
		return true; // If we cannot find the route, we assume it is not safe

	if (!kpath.dentry) {
		path_put(&kpath);
		return true;
	}

	// Check the VFS lock (d_lock) without blocking ourselves
	if (!spin_trylock(&kpath.dentry->d_lock)) {
		pr_info("%s: lock held on %s, bail out!\n", __func__, path);
		path_put(&kpath);
		return true;
	}

	spin_unlock(&kpath.dentry->d_lock);
	path_put(&kpath);
	return false;
}

struct ksu_throne_work_data {
	struct delayed_work dwork;
	bool prune_only;
	int retries;
};

static struct ksu_throne_work_data throne_data;
static DEFINE_MUTEX(throne_tracker_mutex);

static bool do_track_throne_core(bool prune_only)
{
	if (is_lock_held(SYSTEM_PACKAGES_LIST_PATH)) {
		return false; // The file is blocked by Android, we ask for a retry
	}

	struct file *fp = ksu_filp_open_compat(SYSTEM_PACKAGES_LIST_PATH, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		pr_info("throne_tracker: %s not ready yet: %ld\n", SYSTEM_PACKAGES_LIST_PATH, PTR_ERR(fp));
		return false; // It does not yet exist or cannot be read, we ask for a retry
	}

	struct list_head uid_list;
	INIT_LIST_HEAD(&uid_list);

	char chr = 0;
	loff_t pos = 0;
	loff_t line_start = 0;
	char buf[KSU_MAX_PACKAGE_NAME];
	for (;;) {
		ssize_t count = ksu_kernel_read_compat(fp, &chr, sizeof(chr), &pos);
		if (count != sizeof(chr))
			break;
		if (chr != '\n')
			continue;

		count = ksu_kernel_read_compat(fp, buf, sizeof(buf), &line_start);

		struct uid_data *data = kzalloc(sizeof(struct uid_data), GFP_KERNEL);
		if (!data) {
			filp_close(fp, 0);
			goto out;
		}

		char *tmp = buf;
		const char *delim = " ";
		char *package = strsep(&tmp, delim);
		char *uid = strsep(&tmp, delim);
		if (!uid || !package) {
			kfree(data);
			pr_err("update_uid: package or uid is NULL!\n");
			break;
		}

		u32 res;
		if (kstrtou32(uid, 10, &res)) {
			kfree(data);
			pr_err("update_uid: uid parse err\n");
			break;
		}
		data->uid = res;
		strncpy(data->package, package, KSU_MAX_PACKAGE_NAME);
		list_add_tail(&data->list, &uid_list);
		// reset line start
		line_start = pos;
	}
	filp_close(fp, 0);

	// now update uid list
	struct uid_data *np;
	struct uid_data *n;

	if (prune_only)
		goto prune;

	// first, check if manager_uid exist!
	bool manager_exist = false;
	list_for_each_entry (np, &uid_list, list) {
		if (np->uid == ksu_get_manager_appid()) {
			manager_exist = true;
			break;
		}
	}

	if (!manager_exist) {
		if (ksu_is_manager_appid_valid()) {
			pr_info("manager is uninstalled, invalidate it!\n");
			ksu_invalidate_manager_uid();
			goto prune;
		}
		pr_info("Searching manager...\n");
		search_manager("/data/app", 2, &uid_list);
		pr_info("Search manager finished\n");
	}

prune:
	// then prune the allowlist
	ksu_prune_allowlist(is_uid_exist, &uid_list);
out:
	// free uid_list
	list_for_each_entry_safe (np, n, &uid_list, list) {
		list_del(&np->list);
		kfree(np);
	}

	return true; // success
}

// kworker
static void ksu_throne_work_fn(struct work_struct *work)
{
	struct ksu_throne_work_data *data = container_of(to_delayed_work(work), struct ksu_throne_work_data, dwork);
	bool success;

	mutex_lock(&throne_tracker_mutex);

	// Temporarily lend root credentials to the kworker
	const struct cred *saved_cred = override_creds(ksu_cred);

	success = do_track_throne_core(data->prune_only);

	revert_creds(saved_cred);
	mutex_unlock(&throne_tracker_mutex);

	if (!success && data->retries < 10) {
		data->retries++;
		pr_info("throne_tracker: retrying (%d/10) in 100ms...\n", data->retries);
		// Reschedule exactly this work instance
		schedule_delayed_work(&data->dwork, msecs_to_jiffies(100));
	} else {
		if (!success) {
			pr_warn("throne_tracker: giving up after 10 retries.\n");
		}
		data->retries = 0; // Resets for future triggers
	}
}

void track_throne(bool prune_only)
{
	static bool throne_tracker_first_run __read_mostly = true;

	// First scan must be synchronous to not break FDE/FBEv1 on older kernels
	if (unlikely(throne_tracker_first_run)) {
		mutex_lock(&throne_tracker_mutex);
		
		const struct cred *saved_cred = override_creds(ksu_cred);
		do_track_throne_core(prune_only);
		revert_creds(saved_cred);
		
		mutex_unlock(&throne_tracker_mutex);
		throne_tracker_first_run = false;
		return;
	}

	// For asynchronous runs, if a work is already pending, canceling it
	// ensures we don't clobber the prune_only state while it's waiting.
	cancel_delayed_work_sync(&throne_data.dwork);

	// Update state safely and queue the new work
	throne_data.prune_only = prune_only;
	throne_data.retries = 0;
	schedule_delayed_work(&throne_data.dwork, 0);
}

void __init ksu_throne_tracker_init(void)
{
	INIT_DELAYED_WORK(&throne_data.dwork, ksu_throne_work_fn);
}

void __exit ksu_throne_tracker_exit(void)
{
	cancel_delayed_work_sync(&throne_data.dwork);
}
