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

#include "arch.h"
#include "klog.h" // IWYU pragma: keep
#include "ksu.h"
#include "ksud.h"
#include "ksud_boot.h"
#include "selinux/selinux.h"
#include "compat/kernel_compat.h"

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

void stop_init_rc_hook();
void stop_execve_hook();
void stop_input_hook();

#ifdef KSU_KPROBES_HOOK
static struct work_struct __maybe_unused stop_init_rc_hook_work;
static struct work_struct __maybe_unused stop_execve_hook_work;
static struct work_struct __maybe_unused stop_input_hook_work;
#else
bool ksu_init_rc_hook __read_mostly = true;
bool __maybe_unused ksu_vfs_read_hook = true;
bool ksu_input_hook __read_mostly = true;
bool ksu_execveat_hook __read_mostly = true;
#endif

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

static void ksu_initialize_selinux_tw_func(struct callback_head *cb)
{
	apply_kernelsu_rules();
	cache_sid();
	setup_ksu_cred();
	kfree(cb);
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
			struct callback_head *cb = kzalloc(sizeof(*cb), GFP_ATOMIC);
			if (cb) {
				cb->func = ksu_initialize_selinux_tw_func;
				if (task_work_add(current, cb, TWA_RESUME)) {
					kfree(cb);
					pr_warn("ksu_initialize_selinux failed to add task work\n");
				}
			} else {
				pr_warn(
					"ksu_initialize_selinux failed to allocate task work\n");
			}
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

static bool is_init_rc(struct file *fp)
{
    if (strcmp(current->comm, "init")) {
        // we are only interest in `init` process
        return false;
    }

    if (!d_is_reg(fp->f_path.dentry)) {
        return false;
    }

    const char *short_name = fp->f_path.dentry->d_name.name;
    if (strcmp(short_name, "init.rc")) {
        // we are only interest `init.rc` file name file
        return false;
    }
    char path[256];
    char *dpath = d_path(&fp->f_path, path, sizeof(path));

    if (IS_ERR(dpath)) {
        return false;
    }

    if (!!strcmp(dpath, "/init.rc") && !!strcmp(dpath, "/system/etc/init/hw/init.rc")) {
        return false;
    }

    return true;
}

static void ksu_apply_init_rc_proxy(struct file *file)
{
    // we only process the first read
    static bool rc_hooked = false;
    if (rc_hooked) {
        // we don't need these kprobe, unregister it!
        stop_init_rc_hook();
        return;
    }
    rc_hooked = true;

    // now we can sure that the init process is reading
    // `/system/etc/init/init.rc`

    pr_info("read init.rc, comm: %s, rc_count: %zu\n", current->comm,
            ksu_rc_len);

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
}

void ksu_handle_sys_read(unsigned int fd)
{
    struct file *file = fget(fd);
    if (!file) return;

    if (is_init_rc(file)) {
        ksu_apply_init_rc_proxy(file);
    }

    fput(file);
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

	if (ksu_late_loaded) {
		return false;
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
	if (ret < 0 && preempt_count()) {
		preempt_enable_no_resched_notrace();
		ret = strncpy_from_user(path, fn, 32);
		preempt_disable_notrace();
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

	ksu_handle_sys_read(fd);
	return 0;
}

static int sys_fstat_handler_pre(struct kretprobe_instance *p,
					struct pt_regs *regs)
{
	struct pt_regs *real_regs = PT_REAL_REGS(regs);
	unsigned int fd = PT_REGS_PARM1(real_regs);
	void *statbuf = (void *)PT_REGS_PARM2(real_regs);
	*(void **)&p->data = NULL;

	struct file *file = fget(fd);
	if (!file)
		return 1;
	if (is_init_rc(file)) {
		pr_info("stat init.rc");
		fput(file);
		*(void **)&p->data = statbuf;
		return 0;
	}
	fput(file);
	return 1;
}

static int sys_fstat_handler_post(struct kretprobe_instance *p,
					struct pt_regs *regs)
{
	void __user *statbuf = *(void **)&p->data;
	size_t size_offset;
	size_t size_bytes;
	long size = 0;
	long new_size = 0;

	if (!statbuf) return 0;

#ifdef CONFIG_COMPAT
	// Check if the process (like init) is 32-bit running on a 64-bit kernel
	if (in_compat_syscall()) {
		size_offset = offsetof(struct compat_stat, st_size);
		size_bytes = sizeof(compat_off_t);
	} else
#endif
	{
		// Native 64-bit or pure 32-bit kernel
		size_offset = offsetof(struct stat, st_size);
		size_bytes = sizeof(off_t);
	}

	void __user *st_size_ptr = statbuf + size_offset;

	// Kretprobes run in Atomic Context. We MUST disable pagefaults 
	// to prevent copy_to_user from sleeping and causing a Kernel Panic.
	pagefault_disable();

	if (!ksu_copy_from_user_nofault(&size, st_size_ptr, size_bytes)) {
		new_size = size + ksu_rc_len;
		pr_info("adding ksu_rc_len: %ld -> %ld", size, new_size);

		// Attempt to overwrite the file size in userspace safely
		if (!copy_to_user(st_size_ptr, &new_size, size_bytes)) {
			pr_info("added ksu_rc_len");
		} else {
			pr_err("add ksu_rc_len failed: statbuf 0x%lx",
					(unsigned long)st_size_ptr);
		}
	}

	pagefault_enable();

	return 0;
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
static struct kprobe sys_read_kp = {
	.symbol_name = SYS_READ_SYMBOL,
	.pre_handler = sys_read_handler_pre,
};

static struct kretprobe sys_fstat_kp = {
	.kp.symbol_name = SYS_FSTAT_SYMBOL,
	.entry_handler = sys_fstat_handler_pre,
	.handler = sys_fstat_handler_post,
	.data_size = sizeof(void *),
};

static struct kprobe input_event_kp = {
	.symbol_name = "input_event",
	.pre_handler = input_handle_event_handler_pre,
};

static void do_stop_init_rc_hook(struct work_struct *work)
{
	unregister_kprobe(&sys_read_kp);
	unregister_kretprobe(&sys_fstat_kp);
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

// working dummies for manual hooks
int __maybe_unused ksu_handle_vfs_read(struct file **file_ptr, char __user **buf_ptr,
                size_t *count_ptr, loff_t **pos)
{
    struct file *file = *file_ptr;

    if (IS_ERR_OR_NULL(file)) return 0;

    if (is_init_rc(file)) {
        ksu_apply_init_rc_proxy(file);
    }

    return 0;
}

#define STAT_NATIVE 0
#define STAT_STAT64 1

__attribute__((cold))
static noinline void ksu_common_newfstat_ret(unsigned int fd_int, void **statbuf_ptr, 
			const int type, const char *syscall_name)
{
	if (!is_init(current_cred()))
		return;

	struct file *file = fget(fd_int);
	if (!file)
		return;

	if (!is_init_rc(file)) {
		fput(file);
		return;
	}
	fput(file);

	pr_info("%s: stat init.rc \n", syscall_name);

	uintptr_t statbuf_ptr_local = (uintptr_t)*(void **)statbuf_ptr;
	void __user *statbuf = (void __user *)statbuf_ptr_local;
	if (!statbuf)
		return;

	void __user *st_size_ptr;
	long size, new_size;
	size_t len;

	st_size_ptr = statbuf + offsetof(struct stat, st_size);
	len = sizeof(long);

#if defined(__ARCH_WANT_STAT64) || defined(__ARCH_WANT_COMPAT_STAT64)
	if (type) {
		st_size_ptr = statbuf + offsetof(struct stat64, st_size);
		len = sizeof(long long);
	}
#endif

	if (copy_from_user(&size, st_size_ptr, len)) {
		pr_info("%s: read statbuf 0x%lx failed \n", syscall_name, (unsigned long)st_size_ptr);
		return;
	}

	new_size = size + ksu_rc_len;
	pr_info("%s: adding ksu_rc_len: %ld -> %ld \n", syscall_name, size, new_size);
		
	if (!copy_to_user(st_size_ptr, &new_size, len))
		pr_info("%s: added ksu_rc_len \n", syscall_name);
	else
		pr_info("%s: add ksu_rc_len failed: statbuf 0x%lx \n", syscall_name, (unsigned long)st_size_ptr);
	
	return;
}

void ksu_handle_newfstat_ret(unsigned int *fd, struct stat __user **statbuf_ptr)
{
	if (likely(!ksu_vfs_read_hook))
		return;

	ksu_common_newfstat_ret(*fd, (void **)statbuf_ptr, STAT_NATIVE, "sys_newfstat");
}

#if defined(__ARCH_WANT_STAT64) || defined(__ARCH_WANT_COMPAT_STAT64)
void ksu_handle_fstat64_ret(unsigned long *fd, struct stat64 __user **statbuf_ptr)
{

	if (likely(!ksu_vfs_read_hook))
		return;

	// WARNING: LE-only!!!
	ksu_common_newfstat_ret(*(unsigned int *)fd, (void **)statbuf_ptr, STAT_STAT64, "sys_fstat64");
}
#endif

#endif

void stop_init_rc_hook()
{
#ifdef KSU_KPROBES_HOOK
	bool ret = schedule_work(&stop_init_rc_hook_work);
    pr_info("unregister init_rc_hook kprobe: %d!\n", ret);
#else
	ksu_init_rc_hook = false;
	pr_info("stop init_rc_hook\n");
#endif
}

void stop_execve_hook()
{
#ifdef KSU_KPROBES_HOOK
	bool ret = schedule_work(&stop_execve_hook_work);
	pr_info("unregister execve kprobe: %d!\n", ret);
#else
	pr_info("stop execve_hook\n");
	ksu_execveat_hook = false;
#endif
}

void stop_input_hook()
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
void __init ksu_ksud_init()
{
#ifdef KSU_KPROBES_HOOK
	int ret;

	ret = register_kprobe(&execve_kp);
	pr_info("ksud: execve_kp: %d\n", ret);

	ret = register_kprobe(&sys_read_kp);
	pr_info("ksud: sys_read_kp: %d\n", ret);

	ret = register_kretprobe(&sys_fstat_kp);
	pr_info("ksud: sys_fstat_kp: %d\n", ret);

	ret = register_kprobe(&input_event_kp);
	pr_info("ksud: input_event_kp: %d\n", ret);

	INIT_WORK(&stop_init_rc_hook_work, do_stop_init_rc_hook);
	INIT_WORK(&stop_execve_hook_work, do_stop_execve_hook);
	INIT_WORK(&stop_input_hook_work, do_stop_input_hook);
#endif
}

void __exit ksu_ksud_exit()
{
#ifdef KSU_KPROBES_HOOK
	unregister_kprobe(&execve_kp);
	// this should be done before unregister sys_read_kp
	// unregister_kprobe(&sys_read_kp);
	unregister_kprobe(&input_event_kp);
#endif
}
