#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/task_work.h>
#include <asm/current.h>
#include <linux/compat.h>
#include <linux/cred.h>
#include <linux/dcache.h>
#include <linux/err.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
#include <linux/input-event-codes.h>
#else
#include <uapi/linux/input.h>
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 1, 0)
#include <linux/aio.h>
#endif
#ifdef KSU_KPROBES_HOOK
#include <linux/kprobes.h>
#endif
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/namei.h>
#include <linux/workqueue.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/signal.h>
#else
#include <linux/sched.h>
#endif

#include "manager.h"
#include "allowlist.h"
#include "arch.h"
#include "klog.h" // IWYU pragma: keep
#include "ksud.h"
#include "util.h"
#include "selinux/selinux.h"
#include "throne_tracker.h"
#include "kernel_compat.h"

extern int ksu_observer_init(void);
bool ksu_module_mounted __read_mostly = false;
bool ksu_boot_completed __read_mostly = false;

static const char KERNEL_SU_RC[] =
	"\n"

	"on post-fs-data\n"
	"    start logd\n"
	// We should wait for the post-fs-data finish
	"    exec u:r:" KERNEL_SU_DOMAIN ":s0 root -- " KSUD_PATH " post-fs-data\n"
	"\n"

	"on nonencrypted\n"
	"    exec u:r:" KERNEL_SU_DOMAIN ":s0 root -- " KSUD_PATH " services\n"
	"\n"

	"on property:vold.decrypt=trigger_restart_framework\n"
	"    exec u:r:" KERNEL_SU_DOMAIN ":s0 root -- " KSUD_PATH " services\n"
	"\n"

	"on property:sys.boot_completed=1\n"
	"    exec u:r:" KERNEL_SU_DOMAIN ":s0 root -- " KSUD_PATH " boot-completed\n"
	"\n"

	"\n";

static void stop_vfs_read_hook();
static void stop_execve_hook();
static void stop_input_hook();

#ifdef KSU_KPROBES_HOOK
static struct work_struct __maybe_unused stop_vfs_read_work;
static struct work_struct __maybe_unused stop_execve_hook_work;
static struct work_struct __maybe_unused stop_input_hook_work;
#else
bool ksu_vfs_read_hook __read_mostly = true;
bool ksu_input_hook __read_mostly = true;
#endif
bool ksu_execveat_hook __read_mostly = true;

u32 ksu_file_sid;
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
	// sanity check, this may influence the performance
	stop_input_hook();

	ksu_file_sid = ksu_get_ksu_file_sid();
	pr_info("ksu_file sid: %d\n", ksu_file_sid);
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

extern void ksu_avc_spoof_late_init();
void on_boot_completed(void)
{
    ksu_boot_completed = true;
    pr_info("on_boot_completed!\n");
    track_throne(true);
    ksu_avc_spoof_late_init();
}

#define MAX_ARG_STRINGS 0x7FFFFFFF
struct user_arg_ptr {
#ifdef CONFIG_COMPAT
	bool is_compat;
#endif
	union {
		const char __user *const __user *native;
#ifdef CONFIG_COMPAT
		const compat_uptr_t __user *compat;
#endif
	} ptr;
};

static const char __user *get_user_arg_ptr(struct user_arg_ptr argv, int nr)
{
	const char __user *native;

#ifdef CONFIG_COMPAT
	if (unlikely(argv.is_compat)) {
		compat_uptr_t compat;

		if (get_user(compat, argv.ptr.compat + nr))
			return ERR_PTR(-EFAULT);

		return compat_ptr(compat);
	}
#endif

	if (get_user(native, argv.ptr.native + nr))
		return ERR_PTR(-EFAULT);

	return native;
}

/*
 * count() counts the number of strings in array ARGV.
 */

/*
 * Make sure old GCC compiler can use __maybe_unused,
 * Test passed in 4.4.x ~ 4.9.x when use GCC.
 */

static int __maybe_unused count(struct user_arg_ptr argv, int max)
{
	int i = 0;

	if (argv.ptr.native != NULL) {
		for (;;) {
			const char __user *p = get_user_arg_ptr(argv, i);

			if (!p)
				break;

			if (IS_ERR(p))
				return -EFAULT;

			if (i >= max)
				return -E2BIG;
			++i;

			if (fatal_signal_pending(current))
				return -ERESTARTNOHAND;
#ifndef KSU_KPROBES_HOOK
			cond_resched();
#endif
		}
	}
	return i;
}

static void on_post_fs_data_cbfun(struct callback_head *cb)
{
	on_post_fs_data();
}

static struct callback_head on_post_fs_data_cb = { .func =
							on_post_fs_data_cbfun };

// since _ksud handler only uses argv and envp for comparisons
// this can probably work
// adapted from ksu_handle_execveat_ksud
static int ksu_handle_bprm_ksud(const char *filename, const char *argv1, const char *envp, size_t envp_len)
{
	static const char app_process[] = "/system/bin/app_process";
	static bool first_app_process = true;

	/* This applies to versions Android 10+ */
	static const char system_bin_init[] = "/system/bin/init";
	/* This applies to versions between Android 6 ~ 9  */
	static const char old_system_init[] = "/init";
	static bool init_second_stage_executed = false;

	// return early when disabled
	if (!ksu_execveat_hook)
		return 0;

	if (!filename)
		return 0;

	// debug! remove me!
	pr_info("%s: filename: %s argv1: %s envp_len: %zu\n", __func__, filename, argv1, envp_len);

#ifdef CONFIG_KSU_DEBUG
	const char *envp_n = envp;
	unsigned int envc = 1;
	do {
		pr_info("%s: envp[%d]: %s\n", __func__, envc, envp_n);
		envp_n += strlen(envp_n) + 1;
		envc++;
	} while (envp_n < envp + 256);
#endif

	if (init_second_stage_executed)
		goto first_app_process;

	// /system/bin/init with argv1
	if (!init_second_stage_executed 
		&& (!memcmp(filename, system_bin_init, sizeof(system_bin_init) - 1))) {
		if (argv1 && !strcmp(argv1, "second_stage")) {
			pr_info("%s: /system/bin/init second_stage executed\n", __func__);
			apply_kernelsu_rules();
			setup_ksu_cred();
			init_second_stage_executed = true;
		}
	}

	// /init with argv1
	if (!init_second_stage_executed 
		&& (!memcmp(filename, old_system_init, sizeof(old_system_init) - 1))) {
		if (argv1 && !strcmp(argv1, "--second-stage")) {
			pr_info("%s: /init --second-stage executed\n", __func__);
			apply_kernelsu_rules();
			init_second_stage_executed = true;
		}
	}

	if (!envp || !envp_len)
		goto first_app_process;

	// /init without argv1/useless-argv1 but usable envp
	// untested! TODO: test and debug me!
	if (!init_second_stage_executed && (!memcmp(filename, old_system_init, sizeof(old_system_init) - 1))) {
		
		// we hunt for "INIT_SECOND_STAGE"
		const char *envp_n = envp;
		unsigned int envc = 1;
		do {
			if (strstarts(envp_n, "INIT_SECOND_STAGE"))
				break;
			envp_n += strlen(envp_n) + 1;
			envc++;
		} while (envp_n < envp + envp_len);
		pr_info("%s: envp[%d]: %s\n", __func__, envc, envp_n);
		
		if (!strcmp(envp_n, "INIT_SECOND_STAGE=1")
			|| !strcmp(envp_n, "INIT_SECOND_STAGE=true") ) {
			pr_info("%s: /init +envp: INIT_SECOND_STAGE executed\n", __func__);
			apply_kernelsu_rules();
			init_second_stage_executed = true;
		}
	}

first_app_process:
	if (first_app_process && !memcmp(filename, app_process, sizeof(app_process) - 1)) {
		first_app_process = false;
		pr_info("exec app_process, /data prepared, second_stage: %d\n",
			init_second_stage_executed);
		struct task_struct *init_task;
		rcu_read_lock();
		init_task = rcu_dereference(current->real_parent);
		if (init_task) {
			task_work_add(init_task, &on_post_fs_data_cb,
				      TWA_RESUME);
		}
		rcu_read_unlock();

		stop_execve_hook();
	}

	return 0;
}

int ksu_handle_pre_ksud(const char *filename)
{
	if (likely(!ksu_execveat_hook))
		return 0;

	// not /system/bin/init, not /init, not /system/bin/app_process (64/32 thingy)
	// return 0;
	if (likely(strcmp(filename, "/system/bin/init") && strcmp(filename, "/init")
		&& !strstarts(filename, "/system/bin/app_process") ))
		return 0;

	if (!current || !current->mm)
		return 0;

	// https://elixir.bootlin.com/linux/v4.14.1/source/include/linux/mm_types.h#L429
	// unsigned long arg_start, arg_end, env_start, env_end;
	unsigned long arg_start = current->mm->arg_start;
	unsigned long arg_end = current->mm->arg_end;
	unsigned long env_start = current->mm->env_start;
	unsigned long env_end = current->mm->env_end;

	size_t arg_len = arg_end - arg_start;
	size_t envp_len = env_end - env_start;

	if (arg_len <= 0 || envp_len <= 0) // this wont make sense, filter it
		return 0;

	#define ARGV_MAX 32  // this is enough for argv1
	#define ENVP_MAX 256  // this is enough for INIT_SECOND_STAGE
	char args[ARGV_MAX];
	size_t argv_copy_len = (arg_len > ARGV_MAX) ? ARGV_MAX : arg_len;
	char envp[ENVP_MAX];
	size_t envp_copy_len = (envp_len > ENVP_MAX) ? ENVP_MAX : envp_len;

	// we cant use strncpy on here, else it will truncate once it sees \0
	if (ksu_copy_from_user_retry(args, (void __user *)arg_start, argv_copy_len))
		return 0;

	if (ksu_copy_from_user_retry(envp, (void __user *)env_start, envp_copy_len))
		return 0;

	args[ARGV_MAX - 1] = '\0';
	envp[ENVP_MAX - 1] = '\0';

	// we only need argv1 !
	// abuse strlen here since it only gets length up to \0
	char *argv1 = args + strlen(args) + 1;
	if (argv1 >= args + argv_copy_len) // out of bounds!
		argv1 = "";

	return ksu_handle_bprm_ksud(filename, argv1, envp, envp_copy_len);
}

static bool check_argv(struct user_arg_ptr argv, int index,
		       const char *expected, char *buf, size_t buf_len)
{
	const char __user *p;
	int argc;

	argc = count(argv, MAX_ARG_STRINGS);
	if (argc <= index)
		return false;

	p = get_user_arg_ptr(argv, index);
	if (!p || IS_ERR(p))
		return false;

	if (strncpy_from_user_nofault(buf, p, buf_len) <= 0)
		return false;

	buf[buf_len - 1] = '\0';
	return !strcmp(buf, expected);
}

// IMPORTANT NOTE: the call from execve_handler_pre WON'T provided correct value for envp and flags in GKI version
int ksu_handle_execveat_ksud(int *fd, struct filename **filename_ptr,
				struct user_arg_ptr *argv,
				struct user_arg_ptr *envp, int *flags)
{
#ifndef KSU_KPROBES_HOOK
	if (!ksu_execveat_hook) {
		return 0;
	}
#endif
	struct filename *filename;

	static const char app_process[] = "/system/bin/app_process";
	static bool first_zygote = true;

	/* This applies to versions Android 10+ */
	static const char system_bin_init[] = "/system/bin/init";
	/* This applies to versions between Android 6 ~ 9  */
	static const char old_system_init[] = "/init";
	static bool init_second_stage_executed = false;

	if (!filename_ptr)
		return 0;

	filename = *filename_ptr;
	if (IS_ERR(filename)) {
		return 0;
	}

	if (unlikely(!memcmp(filename->name, system_bin_init,
				sizeof(system_bin_init) - 1) &&
			argv)) {
		char buf[16];
		if (!init_second_stage_executed &&
		    check_argv(*argv, 1, "second_stage", buf, sizeof(buf))) {
			pr_info("/system/bin/init second_stage executed\n");
			apply_kernelsu_rules();
			setup_ksu_cred();
			init_second_stage_executed = true;
		}
	} else if (unlikely(!memcmp(filename->name, old_system_init,
					sizeof(old_system_init) - 1) &&
				argv)) {
		char buf[16];
		if (!init_second_stage_executed &&
		    check_argv(*argv, 1, "--second-stage", buf, sizeof(buf))) {
			/* This applies to versions between Android 6 ~ 7 */
			pr_info("/init second_stage executed\n");
			apply_kernelsu_rules();
			setup_ksu_cred();
			init_second_stage_executed = true;
		} else if (count(*argv, MAX_ARG_STRINGS) == 1 &&
			   !init_second_stage_executed && envp) {
			/* This applies to versions between Android 8 ~ 9  */
			int envc = count(*envp, MAX_ARG_STRINGS);
			if (envc > 0) {
				int n;
				for (n = 1; n <= envc; n++) {
					const char __user *p = get_user_arg_ptr(*envp, n);
					if (!p || IS_ERR(p)) {
						continue;
					}
					char env[256];
					// Reading environment variable strings from user space
					if (strncpy_from_user_nofault(env, p, sizeof(env)) < 0)
						continue;
					// Parsing environment variable names and values
					char *env_name = env;
					char *env_value = strchr(env, '=');
					if (env_value == NULL)
						continue;
					// Replace equal sign with string terminator
					*env_value = '\0';
					env_value++;
					// Check if the environment variable name and value are matching
					if (!strcmp(env_name, "INIT_SECOND_STAGE") &&
					    (!strcmp(env_value, "1") ||
					     !strcmp(env_value, "true"))) {
						pr_info("/init second_stage executed\n");
						apply_kernelsu_rules();
						setup_ksu_cred();
						init_second_stage_executed = true;
					}
				}
			}
		}
	}

	if (unlikely(first_zygote && !memcmp(filename->name, app_process,
			     sizeof(app_process) - 1) && argv)) {
		char buf[16];
		if (check_argv(*argv, 1, "-Xzygote", buf, sizeof(buf))) {
			pr_info("exec zygote, /data prepared, second_stage: %d\n",
				init_second_stage_executed);
			rcu_read_lock();
			struct task_struct *init_task =
				rcu_dereference(current->real_parent);
			if (init_task)
				task_work_add(init_task, &on_post_fs_data_cb, TWA_RESUME);
			rcu_read_unlock();
			first_zygote = false;
			stop_execve_hook();
		}
	}

	return 0;
}

static ssize_t (*orig_read)(struct file *, char __user *, size_t, loff_t *);
static ssize_t (*orig_read_iter)(struct kiocb *, struct iov_iter *);
static struct file_operations fops_proxy;
static ssize_t ksu_rc_pos = 0;
const size_t ksu_rc_len = sizeof(KERNEL_SU_RC) - 1;

// https://cs.android.com/android/platform/superproject/main/+/main:system/core/init/parser.cpp;l=144;drc=61197364367c9e404c7da6900658f1b16c42d0da
// https://cs.android.com/android/platform/superproject/main/+/main:system/libbase/file.cpp;l=241-243;drc=61197364367c9e404c7da6900658f1b16c42d0da
// The system will read init.rc file until EOF, whenever read() returns 0,
// so we begin append ksu rc when we meet EOF.

static ssize_t read_proxy(struct file *file, char __user *buf, size_t count,
				loff_t *pos)
{
	ssize_t ret = 0;
	size_t append_count;
	if (ksu_rc_pos && ksu_rc_pos < ksu_rc_len)
		goto append_ksu_rc;

	ret = orig_read(file, buf, count, pos);
	if (ret != 0 || ksu_rc_pos >= ksu_rc_len) {
		return ret;
	} else {
		pr_info("read_proxy: orig read finished, start append rc\n");
	}
append_ksu_rc:
	append_count = ksu_rc_len - ksu_rc_pos;
	if (append_count > count - ret)
		append_count = count - ret;
	// copy_to_user returns the number of not copied
	if (copy_to_user(buf + ret, KERNEL_SU_RC + ksu_rc_pos, append_count)) {
		pr_info("read_proxy: append error, totally appended %ld\n", ksu_rc_pos);
	} else {
		pr_info("read_proxy: append %ld\n", append_count);

		ksu_rc_pos += append_count;
		if (ksu_rc_pos == ksu_rc_len) {
			pr_info("read_proxy: append done\n");
		}
		ret += append_count;
	}

	return ret;
}

static ssize_t read_iter_proxy(struct kiocb *iocb, struct iov_iter *to)
{
	ssize_t ret = 0;
	size_t append_count;
	if (ksu_rc_pos && ksu_rc_pos < ksu_rc_len)
		goto append_ksu_rc;

	ret = orig_read_iter(iocb, to);
	if (ret != 0 || ksu_rc_pos >= ksu_rc_len) {
		return ret;
	} else {
		pr_info("read_iter_proxy: orig read finished, start append rc\n");
	}
append_ksu_rc:
	// copy_to_iter returns the number of copied bytes
	append_count =
		copy_to_iter(KERNEL_SU_RC + ksu_rc_pos, ksu_rc_len - ksu_rc_pos, to);
	if (!append_count) {
		pr_info("read_iter_proxy: append error, totally appended %ld\n",
			ksu_rc_pos);
	} else {
		pr_info("read_iter_proxy: append %ld\n", append_count);

		ksu_rc_pos += append_count;
		if (ksu_rc_pos == ksu_rc_len) {
			pr_info("read_iter_proxy: append done\n");
		}
		ret += append_count;
	}
	return ret;
}


static bool check_init_path(char *dpath)
{
	const char *valid_paths[] = { "/system/etc/init/hw/init.rc",
				      "/init.rc" };
	bool path_match = false;
	int i;

	for (i = 0; i < ARRAY_SIZE(valid_paths); i++) {
		if (strcmp(dpath, valid_paths[i]) == 0) {
			path_match = true;
			break;
		}
	}

	if (!path_match) {
		pr_err("vfs_read: couldn't determine init.rc path for %s\n",
		       dpath);
		return false;
	}

	pr_info("vfs_read: got init.rc path: %s\n", dpath);
	return true;
}

int ksu_handle_vfs_read(struct file **file_ptr, char __user **buf_ptr,
				size_t *count_ptr, loff_t **pos)
{
#ifndef KSU_KPROBES_HOOK
	if (!ksu_vfs_read_hook) {
		return 0;
	}
#endif
	struct file *file;
	size_t count;

	if (strcmp(current->comm, "init")) {
		// we are only interest in `init` process
		return 0;
	}

	file = *file_ptr;
	if (IS_ERR(file)) {
		return 0;
	}

	if (!d_is_reg(file->f_path.dentry)) {
		return 0;
	}

	const char *short_name = file->f_path.dentry->d_name.name;
	if (strcmp(short_name, "init.rc")) {
		// we are only interest `init.rc` file name file
		return 0;
	}
	char path[256];
	char *dpath = d_path(&file->f_path, path, sizeof(path));

	if (IS_ERR(dpath)) {
		return 0;
	}

	if (!check_init_path(dpath)) {
		return 0;
	}

	// we only process the first read
	static bool rc_hooked = false;
	if (rc_hooked) {
		// we don't need this kprobe, unregister it!
		stop_vfs_read_hook();
		return 0;
	}
	rc_hooked = true;

	// now we can sure that the init process is reading
	// `/system/etc/init/init.rc`
	count = *count_ptr;

	pr_info("vfs_read: %s, comm: %s, count: %zu, rc_count: %zu\n", dpath,
		current->comm, count, ksu_rc_len);

	// Now we need to proxy the read and modify the result!
	// But, we can not modify the file_operations directly, because it's in read-only memory.
	// We just replace the whole file_operations with a proxy one.
	memcpy(&fops_proxy, file->f_op, sizeof(struct file_operations));
	orig_read = file->f_op->read;
	if (orig_read) {
		fops_proxy.read = read_proxy;
	}
	orig_read_iter = file->f_op->read_iter;
	if (orig_read_iter) {
		fops_proxy.read_iter = read_iter_proxy;
	}
	// replace the file_operations
	file->f_op = &fops_proxy;

	return 0;
}

int ksu_handle_sys_read(unsigned int fd, char __user **buf_ptr,
				size_t *count_ptr)
{
	struct file *file = fget(fd);
	if (!file) {
		return 0;
	}
	int result = ksu_handle_vfs_read(&file, buf_ptr, count_ptr, NULL);
	fput(file);
	return result;
}

static unsigned int volumedown_pressed_count = 0;

static bool is_volumedown_enough(unsigned int count)
{
	return count >= 3;
}

int ksu_handle_input_handle_event(unsigned int *type, unsigned int *code,
					int *value)
{
#ifndef KSU_KPROBES_HOOK
	if (!ksu_input_hook) {
		return 0;
	}
#endif
	if (*type == EV_KEY && *code == KEY_VOLUMEDOWN) {
		int val = *value;
		pr_info("KEY_VOLUMEDOWN val: %d\n", val);
		if (val) {
			// key pressed, count it
			volumedown_pressed_count += 1;
			if (is_volumedown_enough(volumedown_pressed_count)) {
				stop_input_hook();
			}
		}
	}

	return 0;
}

bool ksu_is_safe_mode()
{
	static bool safe_mode = false;
	if (safe_mode) {
		// don't need to check again, userspace may call multiple times
		return true;
	}

	// stop hook first!
	stop_input_hook();

	pr_info("volumedown_pressed_count: %d\n", volumedown_pressed_count);
	if (is_volumedown_enough(volumedown_pressed_count)) {
		// pressed over 3 times
		pr_info("KEY_VOLUMEDOWN pressed max times, safe mode detected!\n");
		safe_mode = true;
		return true;
	}

	return false;
}

#ifdef KSU_KPROBES_HOOK

static int sys_execve_handler_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct pt_regs *real_regs = PT_REAL_REGS(regs);
	const char __user **filename_user =
		(const char **)&PT_REGS_PARM1(real_regs);
	const char __user *const __user *__argv =
		(const char __user *const __user *)PT_REGS_PARM2(real_regs);
	struct user_arg_ptr argv = { .ptr.native = __argv };
	struct filename filename_in, *filename_p;
	char path[32];
	long ret;
	unsigned long addr;
	const char __user *fn;

	if (!filename_user)
		return 0;

	addr = untagged_addr((unsigned long)*filename_user);
	fn = (const char __user *)addr;

	memset(path, 0, sizeof(path));
	ret = strncpy_from_user_nofault(path, fn, 32);
	if (ret < 0 && try_set_access_flag(addr)) {
		ret = strncpy_from_user_nofault(path, fn, 32);
	}
	if (ret < 0) {
		pr_err("Access filename failed for execve_handler_pre\n");
		return 0;
	}
	filename_in.name = path;

	filename_p = &filename_in;
	return ksu_handle_execveat_ksud(AT_FDCWD, &filename_p, &argv, NULL, NULL);
}

static int sys_read_handler_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct pt_regs *real_regs = PT_REAL_REGS(regs);
	unsigned int fd = PT_REGS_PARM1(real_regs);
	char __user **buf_ptr = (char __user **)&PT_REGS_PARM2(real_regs);
	size_t count_ptr = (size_t *)&PT_REGS_PARM3(real_regs);

	return ksu_handle_sys_read(fd, buf_ptr, count_ptr);
}

static int input_handle_event_handler_pre(struct kprobe *p,
						struct pt_regs *regs)
{
	unsigned int *type = (unsigned int *)&PT_REGS_PARM2(regs);
	unsigned int *code = (unsigned int *)&PT_REGS_PARM3(regs);
	int *value = (int *)&PT_REGS_CCALL_PARM4(regs);
	return ksu_handle_input_handle_event(type, code, value);
}

static struct kprobe execve_kp = {
	.symbol_name = SYS_EXECVE_SYMBOL,
	.pre_handler = sys_execve_handler_pre,
};
static struct kprobe vfs_read_kp = {
	.symbol_name = SYS_READ_SYMBOL,
	.pre_handler = sys_read_handler_pre,
};

static struct kprobe input_event_kp = {
	.symbol_name = "input_event",
	.pre_handler = input_handle_event_handler_pre,
};

static void do_stop_vfs_read_hook(struct work_struct *work)
{
	unregister_kprobe(&vfs_read_kp);
}

static void do_stop_execve_hook(struct work_struct *work)
{
	unregister_kprobe(&execve_kp);
}

static void do_stop_input_hook(struct work_struct *work)
{
	unregister_kprobe(&input_event_kp);
}
#else
static int ksu_execve_ksud_common(const char __user *filename_user,
				  struct user_arg_ptr *argv)
{
	struct filename filename_in, *filename_p;
	char path[32];
	long len;

	// return early if disabled.
	if (!ksu_execveat_hook) {
		return 0;
	}

	if (!filename_user)
		return 0;

	len = strncpy_from_user_nofault(path, filename_user, 32);
	if (len <= 0)
		return 0;

	path[sizeof(path) - 1] = '\0';

	// this is because ksu_handle_execveat_ksud calls it filename->name
	filename_in.name = path;
	filename_p = &filename_in;

	return ksu_handle_execveat_ksud(AT_FDCWD, &filename_p, argv, NULL,
					NULL);
}

int __maybe_unused
ksu_handle_execve_ksud(const char __user *filename_user,
		       const char __user *const __user *__argv)
{
	struct user_arg_ptr argv = { .ptr.native = __argv };
	return ksu_execve_ksud_common(filename_user, &argv);
}

#if defined(CONFIG_COMPAT) && defined(CONFIG_64BIT)
int __maybe_unused ksu_handle_compat_execve_ksud(
	const char __user *filename_user, const compat_uptr_t __user *__argv)
{
	struct user_arg_ptr argv = { .ptr.compat = __argv };
	return ksu_execve_ksud_common(filename_user, &argv);
}
#endif /* COMPAT & 64BIT */

#endif

static void stop_vfs_read_hook()
{
#ifdef KSU_KPROBES_HOOK
	bool ret = schedule_work(&stop_vfs_read_work);
	pr_info("unregister vfs_read kprobe: %d!\n", ret);
#else
	ksu_vfs_read_hook = false;
	pr_info("stop vfs_read_hook\n");
#endif
}

static void stop_execve_hook()
{
#ifdef KSU_KPROBES_HOOK
	bool ret = schedule_work(&stop_execve_hook_work);
	pr_info("unregister execve kprobe: %d!\n", ret);
#else
	pr_info("stop execve_hook\n");
	ksu_execveat_hook = false;
#endif
}

static void stop_input_hook()
{
#ifdef KSU_KPROBES_HOOK
	static bool input_hook_stopped = false;
	if (input_hook_stopped) {
		return;
	}
	input_hook_stopped = true;
	bool ret = schedule_work(&stop_input_hook_work);
	pr_info("unregister input kprobe: %d!\n", ret);
#else
	if (!ksu_input_hook) {
		return;
	}
	ksu_input_hook = false;
	pr_info("stop input_hook\n");
#endif
}


// ksud: module support
void ksu_ksud_init()
{
#ifdef KSU_KPROBES_HOOK
	int ret;

	ret = register_kprobe(&execve_kp);
	pr_info("ksud: execve_kp: %d\n", ret);

	ret = register_kprobe(&vfs_read_kp);
	pr_info("ksud: vfs_read_kp: %d\n", ret);

	ret = register_kprobe(&input_event_kp);
	pr_info("ksud: input_event_kp: %d\n", ret);

	INIT_WORK(&stop_vfs_read_work, do_stop_vfs_read_hook);
	INIT_WORK(&stop_execve_hook_work, do_stop_execve_hook);
	INIT_WORK(&stop_input_hook_work, do_stop_input_hook);
#endif
}

void ksu_ksud_exit()
{
#ifdef KSU_KPROBES_HOOK
	unregister_kprobe(&execve_kp);
	// this should be done before unregister vfs_read_kp
	// unregister_kprobe(&vfs_read_kp);
	unregister_kprobe(&input_event_kp);
#endif
}
