#include <linux/err.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/printk.h>
#include <linux/workqueue.h>

#include "feature/selinux_hide.h"

#include "policy/allowlist.h"
#include "klog.h" // IWYU pragma: keep
#include "runtime/ksud_boot.h"
#include "runtime/ksud.h"
#include "manager/manager_observer.h"
#include "manager/manager_identity.h"
#include "manager/throne_tracker.h"
#include "runtime/debug_state.h"
#include "compat/kernel_compat.h"

bool ksu_module_mounted __read_mostly = false;
bool ksu_boot_completed __read_mostly = false;
bool ksu_ksud_present __read_mostly = false;

void ksu_set_ksud_present(bool present)
{
	if (READ_ONCE(ksu_ksud_present) == present)
		return;
	WRITE_ONCE(ksu_ksud_present, present);
	pr_info("ksud %s, su goes to %s\n", present ? "installed" : "removed",
		present ? "ksud" : "sh");
}

static void ksu_check_ksud(void)
{
	struct path ksud;
	bool present = !kern_path(KSUD_PATH, 0, &ksud);

	if (present)
		path_put(&ksud);
	ksu_set_ksud_present(present);
}

static void ksu_check_ksud_work_fn(struct work_struct *work)
{
	ksu_check_ksud();
}

static DECLARE_WORK(ksu_check_ksud_work, ksu_check_ksud_work_fn);

/*
 * Safe from atomic context. The /data/adb observer normally keeps
 * ksu_ksud_present current; this is the fallback if it couldn't be set up.
 */
void ksu_recheck_ksud(void)
{
	schedule_work(&ksu_check_ksud_work);
}

extern void ksu_avc_spoof_late_init();

void on_post_fs_data(void)
{
	static bool done = false;
	if (done) {
		pr_info("on_post_fs_data already done\n");
		return;
	}
	done = true;
	pr_info("on_post_fs_data!\n");

	ksu_load_allow_list();
	ksu_observer_init();
	/* After the observer is up, so a ksud install can't slip in between. */
	ksu_check_ksud();
	pr_info("ksud %s\n", ksu_ksud_present ? "present" : "not installed");
	// sanity check, this may influence the performance
	ksu_stop_input_hook_runtime();
	ksu_selinux_hide_handle_post_fs_data();

#ifdef CONFIG_KSU_DEBUG
	int compat_err;

	/*
	 * A kernel_read() argument order mismatch cannot be detected at compile
	 * time and only shows up as short reads, which silently disables manager
	 * crowning. Surface it here, once, now that /proc is mounted. Debug
	 * builds only, so release builds pay nothing for the check.
	 */
	compat_err = ksu_compat_read_selftest();
	if (compat_err)
		pr_err("kernel_compat: read selftest failed: %d\n", compat_err);
	ksu_dbg_log("compat: read selftest %s (%d)",
		    compat_err ? "FAILED" : "ok", compat_err);
#endif

	/*
	 * The manager is usually not crowned yet, and without an appid
	 * allowed_for_su() stays false, so the manager can never obtain the
	 * driver fd, ksud is never installed and boot_completed never arrives.
	 * /data/system/packages.list does not exist this early, so this only
	 * queues a scan that is retried once the list has been written.
	 */
	ksu_dbg_log("post_fs_data: ksud=%d appid=%d", ksu_ksud_present ? 1 : 0,
		    ksu_get_manager_appid());
	ksu_request_manager_scan(false);
}

extern void ext4_unregister_sysfs(struct super_block *sb);

int nuke_ext4_sysfs(const char *mnt)
{
	struct path path;
	int err = kern_path(mnt, 0, &path);
	if (err) {
		pr_err("nuke path err: %d\n", err);
		return err;
	}

	struct super_block *sb = path.dentry->d_inode->i_sb;
	const char *name = sb->s_type->name;
	if (strcmp(name, "ext4") != 0) {
		pr_info("nuke but module aren't mounted\n");
		path_put(&path);
		return -EINVAL;
	}

	ext4_unregister_sysfs(sb);
	path_put(&path);
	return 0;
}

void on_module_mounted(void)
{
	pr_info("on_module_mounted!\n");
	ksu_module_mounted = true;
}

void on_boot_completed(void)
{
    ksu_boot_completed = true;
    pr_info("on_boot_completed!\n");
    ksu_dbg_log("boot_completed: appid=%d", ksu_get_manager_appid());
    track_throne(true);
    ksu_avc_spoof_late_init();
}
