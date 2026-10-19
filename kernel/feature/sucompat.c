#include "linux/file.h"
#include "linux/fcntl.h"
#include "linux/namei.h"
#include <linux/compiler_types.h>
#include <linux/preempt.h>
#include <linux/printk.h>
#include <linux/mm.h>
#include <linux/pgtable.h>
#include <linux/uaccess.h>
#include <asm/current.h>
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/sched/task_stack.h>
#include <linux/ptrace.h>
#include <linux/namei.h>

#include "linux/jump_label.h"

#include <linux/fs_struct.h>

#include <linux/task_work.h>
#ifdef CONFIG_KSU_SUSFS
#include <linux/susfs_def.h>
#include <linux/minmax.h>
#include "selinux/selinux.h"
#include "objsec.h"
#include "runtime/ksud.h"
#endif

#include "arch.h"
#include "policy/allowlist.h"
#include "policy/feature.h"
#include "klog.h" // IWYU pragma: keep
#include "runtime/ksud.h"
#include "feature/sucompat.h"
#include "feature/adb_root.h"
#include "policy/app_profile.h"
#include "hook/syscall_hook.h"
#include "supercall/supercall.h"
#include "sulog/event.h"
#include "ksu.h"
#include "util.h"

#define SU_PATH "/system/bin/su"
#define SH_PATH "/system/bin/sh"

static const char sh_path[] = SH_PATH;
static const char su_path[] = SU_PATH;
static const char ksud_path[] = KSUD_PATH;

DEFINE_STATIC_KEY_TRUE(ksu_su_compat_enabled);

static int su_compat_feature_get(u64 *value)
{
	if (static_key_enabled(&ksu_su_compat_enabled))
		*value = 1;
	else
		*value = 0;
	return 0;
}

static int su_compat_feature_set(u64 value)
{
	bool enable = value != 0;
	if (enable)
		static_branch_enable(&ksu_su_compat_enabled);
	else
		static_branch_disable(&ksu_su_compat_enabled);
	pr_info("su_compat: set to %d\n", enable);
	return 0;
}

static const struct ksu_feature_handler su_compat_handler = {
	.feature_id = KSU_FEATURE_SU_COMPAT,
	.name = "su_compat",
	.get_handler = su_compat_feature_get,
	.set_handler = su_compat_feature_set,
};

static char __user *sh_user_path(void)
{
	static const char sh_path_local[] = "/system/bin/sh";

	return userspace_stack_buffer(sh_path_local, sizeof(sh_path_local));
}

static char __user *ksud_user_path(void)
{
	static const char ksud_path_local[] = KSUD_PATH;

	return userspace_stack_buffer(ksud_path_local, sizeof(ksud_path_local));
}

static char __user *empty_user_path(void)
{
	return userspace_stack_buffer("", sizeof(""));
}

static bool is_ksud_exists()
{
	struct path path;

	if (kern_path(KSUD_PATH, 0, &path) < 0) {
		return false;
	}
	path_put(&path);
	return true;
}

#ifdef CONFIG_KSU_SUSFS
static bool ksu_str_ends_with(const char *str, size_t str_len, const char *suffix)
{
    size_t suffix_len = strlen(suffix);
    if (suffix_len > str_len)
        return false;
    return memcmp(str + str_len - suffix_len, suffix, suffix_len) == 0;
}

static bool ksu_is_zygote_or_adbd(const char *name, size_t len)
{
    if (len < 5)
        return false;

    // Fast-path: Check last character first to skip 99% of processes
    char last = name[len - 1];
    if (last == 'd')
        return ksu_str_ends_with(name, len, "/adbd");
    if (last == 's')
        return ksu_str_ends_with(name, len, "/app_process");
    if (last == '2')
        return ksu_str_ends_with(name, len, "/app_process32");
    if (last == '4')
        return ksu_str_ends_with(name, len, "/app_process64");
    if (last == 'e')
        return ksu_str_ends_with(name, len, "/stub_zygote");

    return false;
}

/*
 * return 0 -> No further checks should be required afterwards
 * return non-zero -> Further checks should be continued afterwards
 */
int ksu_handle_execveat_init(struct filename *filename, struct user_arg_ptr *argv_user, struct user_arg_ptr *envp_user) {
    if (current->pid != 1 && is_init(get_current_cred())) {
        int ret;
        if (unlikely(strcmp(filename->name, KSUD_PATH) == 0)) {
            const char __user *argv_user_ptr = get_user_arg_ptr(*argv_user, 0);
            struct ksu_sulog_pending_event *pending_sucompat = NULL;

            pr_info("hook_manager: escape to root for init executing ksud: %d\n", current->pid);
            ret = escape_to_root_for_init();
            if (ret) {
                pr_err("escape_to_root_for_init() failed: %d\n", ret);
                return ret;
            }
            if (!argv_user_ptr || IS_ERR(argv_user_ptr)) {
                pr_err("!argv_user_ptr || IS_ERR(argv_user_ptr)\n");
                return -EFAULT;
            }
            pending_sucompat = ksu_sulog_capture_sucompat_kernel(filename->name, (const char __user *const __user *)argv_user->ptr.native, GFP_KERNEL);
            ksu_sulog_emit_pending(pending_sucompat, ret, GFP_KERNEL);
            return 0;
        } else {
            size_t len = strlen(filename->name);
            if (likely(!ksu_is_zygote_or_adbd(filename->name, len)) &&
                        !susfs_is_current_proc_no_su())
            {
                pr_info("susfs: mark no sucompat checks for pid: '%d', exec: '%s'\n", current->pid, filename->name);
                susfs_set_current_proc_no_su();
                return 0;
            }
        }

#ifdef CONFIG_COMPAT
        if (unlikely(envp_user->is_compat))
            ret = ksu_adb_root_handle_execveat(filename->name, (void ***)&envp_user->ptr.compat);
        else
            ret = ksu_adb_root_handle_execveat(filename->name, (void ***)&envp_user->ptr.native);
#else
        ret = ksu_adb_root_handle_execveat(filename->name, (void ***)&envp_user->ptr.native);
#endif

        if (ret) {
            pr_err("adb root failed: %d\n", ret);
            return ret;
        }
        return ret;
    }

    return -EINVAL;
}

int ksu_handle_execveat_sucompat(int *fd, struct filename **filename_ptr,
                 void *argv_user, void *envp_user,
                 int *flags)
{
    struct filename *filename;
    struct user_arg_ptr *argv_ptr = (struct user_arg_ptr *)argv_user;
    struct ksu_sulog_pending_event *pending_sucompat = NULL;
    int ret;

    if (unlikely(!filename_ptr))
        return -EINVAL;

    filename = *filename_ptr;
    if (IS_ERR(filename))
        return -EINVAL;

    if (!ksu_handle_execveat_init(filename, (struct user_arg_ptr*)argv_user, (struct user_arg_ptr*)envp_user))
        return -EINVAL;

    if (likely(memcmp(filename->name, su_path, sizeof(su_path))))
        return -EINVAL;

    if (current_chrooted())
    {
        pr_err("ksu_handle_execveat_sucompat: su found but NOT allowed! Because current process is running in chrooted environment\n");
        return -EINVAL;
    }

    if (!ksu_is_allow_uid_for_current(current_uid().val))
        return -EINVAL;

    ksu_compat_sulog('x');
    pr_info("ksu_handle_execveat_sucompat: su found\n");

    pending_sucompat = ksu_sulog_capture_sucompat_kernel(filename->name,
                                                         (const char __user *const __user *)argv_ptr->ptr.native,
                                                         GFP_KERNEL);

    memcpy((void *)filename->name, ksud_path, sizeof(ksud_path));

    ret = escape_with_root_profile();
    if (ret) {
        pr_err("escape_with_root_profile() failed: %d\n", ret);
        return -EINVAL;
    }

    ksu_sulog_emit_pending(pending_sucompat, ret, GFP_KERNEL);

    return 0;
}

int ksu_handle_post_execveat_sucompat(int *fd, struct filename **filename_ptr,
                 void *argv_user, void *envp_user,
                 int *flags, int *retval)
{
    if (*retval >= 0) {
        (void)ksu_install_su_fd();
    }
    return 0;
}

extern struct static_key_true is_first_zygote;

int ksu_handle_execveat(int *fd, struct filename **filename_ptr, void *argv,
            void *envp, int *flags)
{
    struct ksu_sulog_pending_event *pending_root_execve = NULL;

    if (unlikely(!filename_ptr))
        return -EINVAL;

    if (IS_ERR(*filename_ptr))
        return -EINVAL;

    if (current_euid().val == 0) {
        struct user_arg_ptr *argv_ptr = (struct user_arg_ptr *)argv;
        pending_root_execve = ksu_sulog_capture_root_execve_kernel((*filename_ptr)->name,
                                                                   (const char __user *const __user *)argv_ptr->ptr.native,
                                                                   GFP_KERNEL);
        ksu_sulog_emit_pending(pending_root_execve, 0, GFP_KERNEL);
    }

    if (static_branch_unlikely(&is_first_zygote)) {
        (void)ksu_handle_execveat_ksud(fd, filename_ptr, argv, envp, flags);
    }

    return ksu_handle_execveat_sucompat(fd, filename_ptr, argv, envp,
                        flags);
}
#endif

#ifdef CONFIG_KSU_SUSFS
int ksu_handle_faccessat(int *dfd, struct filename **filename,
		int *mode, int *__unused_flags)
{
	if (unlikely(IS_ERR(*filename) || (*filename)->name == NULL))
		return 0;

	if (likely(memcmp((*filename)->name, su_path, sizeof(su_path))))
		return 0;

	if (current_chrooted()) {
		pr_err("ksu_handle_faccessat: su found but NOT allowed! Because current process is running in chrooted environment\n");
		return 0;
	}

	if (!ksu_is_allow_uid_for_current(current_uid().val))
		return 0;

	ksu_compat_sulog('a');
	pr_info("ksu_handle_faccessat: su->sh!\n");
	memcpy((void *)((*filename)->name), sh_path, sizeof(sh_path));
	return 0;
}
#else
int ksu_handle_faccessat(int *dfd, const char __user **filename_user,
		int *mode, int *__unused_flags)
{
	const char su[] = SU_PATH;

	if (!ksu_is_allow_uid_for_current(current_uid().val)) {
		return 0;
	}

	char path[sizeof(su) + 1];
	memset(path, 0, sizeof(path));
	strncpy_from_user_nofault(path, *filename_user, sizeof(path));

	if (unlikely(!memcmp(path, su, sizeof(su)))) {
		ksu_compat_sulog('a');
		pr_info("faccessat su->sh!\n");
		*filename_user = sh_user_path();
	}

	return 0;
}
#endif

#ifdef CONFIG_KSU_SUSFS
int ksu_handle_stat(int *dfd, struct filename **filename, int *flags) {
	if (unlikely(IS_ERR(*filename) || (*filename)->name == NULL))
		return 0;

	if (likely(memcmp((*filename)->name, su_path, sizeof(su_path))))
		return 0;

	if (current_chrooted()) {
		pr_err("ksu_handle_stat: su found but NOT allowed! Because current process is running in chrooted environment\n");
		return 0;
	}

	if (!ksu_is_allow_uid_for_current(current_uid().val))
		return 0;

	ksu_compat_sulog('s');
	pr_info("ksu_handle_stat: su->sh!\n");
	memcpy((void *)((*filename)->name), sh_path, sizeof(sh_path));
	return 0;
}
#else
int ksu_handle_stat(int *dfd, const char __user **filename_user, int *flags)
{
	const char su[] = SU_PATH;

	if (!ksu_is_allow_uid_for_current(current_uid().val)) {
		return 0;
	}

	if (unlikely(!filename_user)) {
		return 0;
	}

	char path[sizeof(su) + 1];
	memset(path, 0, sizeof(path));
	strncpy_from_user_nofault(path, *filename_user, sizeof(path));

	if (unlikely(!memcmp(path, su, sizeof(su)))) {
		ksu_compat_sulog('s');
		pr_info("newfstatat su->sh!\n");
		*filename_user = sh_user_path();
	}

	return 0;
}
#endif

#if defined(CONFIG_KPROBES) && !defined(CONFIG_KSU_SUSFS)
long ksu_handle_faccessat_sucompat(int orig_nr, struct pt_regs *regs)
{
	const char __user **filename_user, *orig_filename;
	long ret;
	const struct cred *old_cred;

	if (!ksu_is_allow_uid_for_current(current_uid().val)) {
		goto do_orig_facessat;
	}

	filename_user = (const char __user **)&PT_REGS_PARM2(regs);

	char path[sizeof(su_path) + 1];
	memset(path, 0, sizeof(path));
	strncpy_from_user_nofault(path, *filename_user, sizeof(path));

	if (unlikely(!memcmp(path, su_path, sizeof(su_path)))) {
		if (current_chrooted()) {
			pr_err("ksu_handle_faccessat_sucompat: su found but NOT allowed! Because current process is running in chrooted environment\n");
			goto do_orig_facessat;
		}
		old_cred = override_creds(ksu_cred);
		if (is_ksud_exists()) {
			ksu_compat_sulog('a');
			pr_info("faccessat su->ksud!\n");
			orig_filename = *filename_user;
			*filename_user = ksud_user_path();
			ret = ksu_syscall_table[orig_nr](regs);
			revert_creds(old_cred);
			*filename_user = orig_filename;
			return ret;
		} else {
			revert_creds(old_cred);
		}
	}

do_orig_facessat:
	return ksu_syscall_table[orig_nr](regs);
}

long ksu_handle_stat_sucompat(int orig_nr, struct pt_regs *regs)
{
	const char __user **filename_user, *orig_filename;
	long ret;
	const struct cred *old_cred;

	if (!ksu_is_allow_uid_for_current(current_uid().val)) {
		goto do_orig_stat;
	}

	filename_user = (const char __user **)&PT_REGS_PARM2(regs);

	char path[sizeof(su_path) + 1];
	memset(path, 0, sizeof(path));
	strncpy_from_user_nofault(path, *filename_user, sizeof(path));

	if (unlikely(!memcmp(path, su_path, sizeof(su_path)))) {
		if (current_chrooted()) {
			pr_err("ksu_handle_stat_sucompat: su found but NOT allowed! Because current process is running in chrooted environment\n");
			goto do_orig_stat;
		}
		old_cred = override_creds(ksu_cred);
		if (is_ksud_exists()) {
			ksu_compat_sulog('s');
			pr_info("newfstatat su->ksud!\n");
			orig_filename = *filename_user;
			*filename_user = ksud_user_path();
			ret = ksu_syscall_table[orig_nr](regs);
			revert_creds(old_cred);
			*filename_user = orig_filename;
			return ret;
		} else {
			revert_creds(old_cred);
		}
	}

do_orig_stat:
	return ksu_syscall_table[orig_nr](regs);
}

static long ksu_handle_execve_sucompat_common(const char __user **filename_user,
					      const char __user *const __user *argv_user, unsigned long envp,
					      bool execveat, int orig_nr, struct pt_regs *regs)
{
	const char __user *fn;
	struct ksu_sulog_pending_event *pending_sucompat = NULL;
	char path[sizeof(su_path) + 1];
	long ret, orig_regs[5];
	unsigned long addr;
	int su_fd = -1;
	int tmp_fd;
	struct file *ksud_file;
	const struct cred *old_cred;

	if (execveat && ((int)PT_REGS_SYSCALL_PARM1(regs) != AT_FDCWD || (int)PT_REGS_PARM5(regs) != 0))
		goto do_orig_execve;

	if (unlikely(!filename_user))
		goto do_orig_execve;

	if (!ksu_is_allow_uid_for_current(current_uid().val))
		goto do_orig_execve;

	addr = untagged_addr((unsigned long)*filename_user);
	fn = (const char __user *)addr;
	memset(path, 0, sizeof(path));
	ret = strncpy_from_user(path, fn, sizeof(path));

	if (ret < 0) {
		pr_warn("Access filename when execve failed: %ld", ret);
		goto do_orig_execve;
	}

	if (likely(memcmp(path, su_path, sizeof(su_path))))
		goto do_orig_execve;

	if (current_chrooted()) {
		pr_err("ksu_handle_execve_sucompat: su found but NOT allowed! Because current process is running in chrooted environment\n");
		goto do_orig_execve;
	}

	ksu_compat_sulog('x');
	pr_info("sys_execve su found\n");

	tmp_fd = get_unused_fd_flags(O_CLOEXEC);
	if (tmp_fd < 0) {
		pr_err("alloc tmp fd err: %d\n", tmp_fd);
		goto do_orig_execve;
	}

	old_cred = override_creds(ksu_cred);
	ksud_file = filp_open(KSUD_PATH, O_PATH, 0);
	revert_creds(old_cred);
	if (IS_ERR(ksud_file)) {
		pr_err("open ksud err: %ld\n", PTR_ERR(ksud_file));
		put_unused_fd(tmp_fd);
		goto do_orig_execve;
	}

	fd_install(tmp_fd, ksud_file);

	pending_sucompat = ksu_sulog_capture_sucompat(*filename_user, argv_user, GFP_KERNEL);
	// execve(file, argv, environ)
	// execveat(fd, file, argv, environ, flags)
	orig_regs[0] = PT_REGS_SYSCALL_PARM1(regs);
	orig_regs[1] = regs->__PT_PARM2_REG;
	orig_regs[2] = regs->__PT_PARM3_REG;
	orig_regs[3] = regs->__PT_SYSCALL_PARM4_REG;
	orig_regs[4] = regs->__PT_PARM5_REG;
	regs->__PT_PARM5_REG = AT_EMPTY_PATH;
	regs->__PT_SYSCALL_PARM4_REG = envp;
	regs->__PT_PARM3_REG = (unsigned long)argv_user;
	regs->__PT_PARM2_REG = empty_user_path();
	PT_REGS_SYSCALL_PARM1(regs) = tmp_fd;

	ret = escape_with_root_profile();
	if (ret) {
		pr_err("escape_with_root_profile failed: %ld\n", ret);
	}
	ksu_sulog_emit_pending(pending_sucompat, ret, GFP_KERNEL);

	ret = ksu_syscall_table[__NR_execveat](regs);
	if (ret < 0) {
		ksu_close_fd(tmp_fd);
		PT_REGS_SYSCALL_PARM1(regs) = orig_regs[0];
		regs->__PT_PARM2_REG = orig_regs[1];
		regs->__PT_PARM3_REG = orig_regs[2];
		regs->__PT_SYSCALL_PARM4_REG = orig_regs[3];
		regs->__PT_PARM5_REG = orig_regs[4];
	} else {
		// Only grant the scoped driver capability after the selected root
		// profile has been applied successfully.
		su_fd = ksu_install_su_fd();
		if (su_fd < 0) {
			pr_warn("install su session fd failed: %d\n", su_fd);
		}
	}
	return ret;

do_orig_execve:
	return ksu_syscall_table[orig_nr](regs);
}

long ksu_handle_execve_sucompat(const char __user **filename_user, int orig_nr, struct pt_regs *regs)
{
	return ksu_handle_execve_sucompat_common(filename_user, (const char __user *const __user *)PT_REGS_PARM2(regs),
						 PT_REGS_PARM3(regs), false, orig_nr, regs);
}

long ksu_handle_execveat_sucompat(const char __user **filename_user, int orig_nr, struct pt_regs *regs)
{
	return ksu_handle_execve_sucompat_common(filename_user, (const char __user *const __user *)PT_REGS_PARM3(regs),
						 PT_REGS_SYSCALL_PARM4(regs), true, orig_nr, regs);
}
#endif

// sucompat: permitted process can execute 'su' to gain root access.
void __init ksu_sucompat_init()
{
	if (ksu_register_feature_handler(&su_compat_handler)) {
		pr_err("Failed to register su_compat feature handler\n");
	}
}

void __exit ksu_sucompat_exit()
{
	ksu_unregister_feature_handler(KSU_FEATURE_SU_COMPAT);
}
