#include <linux/preempt.h>
#include <linux/printk.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <asm/current.h>
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
#include <linux/pgtable.h>
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
#include <linux/compiler_types.h>
#include <linux/compiler.h>
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/task_stack.h>
#else
#include <linux/sched.h>
#endif
#include <linux/ptrace.h>

#include "objsec.h"

#include "policy/allowlist.h"
#include "policy/feature.h"
#include "klog.h" // IWYU pragma: keep
#include "runtime/ksud.h"
#include "compat/kernel_compat.h"
#include "sucompat.h"
#include "policy/app_profile.h"
#include "selinux/selinux.h"
#include "tiny_sulog.h"

#define SU_PATH "/system/bin/su"
#define SH_PATH "/system/bin/sh"

bool ksu_su_compat_enabled __read_mostly = true;

static int su_compat_feature_get(u64 *value)
{
	*value = ksu_su_compat_enabled ? 1 : 0;
	return 0;
}

static int su_compat_feature_set(u64 value)
{
	bool enable = value != 0;
	ksu_su_compat_enabled = enable;
	pr_info("su_compat: set to %d\n", enable);
	return 0;
}

static const struct ksu_feature_handler su_compat_handler = {
	.feature_id = KSU_FEATURE_SU_COMPAT,
	.name = "su_compat",
	.get_handler = su_compat_feature_get,
	.set_handler = su_compat_feature_set,
};

static void __user *userspace_stack_buffer(const void *d, size_t len)
{
	// Stack Pointer must be 16-byte aligned.
	// We also subtract a safe margin (256 bytes) 
	// to avoid corrupting local variables or smth
	unsigned long sp = current_user_stack_pointer();
	sp = (sp - len - 256) & ~0xFUL; // Align downwards to nearest 16 bytes

	char __user *p = (char __user *)sp;

	return copy_to_user(p, d, len) ? NULL : p;
}

static char __user *sh_user_path(void)
{
	static const char sh_path[] = "/system/bin/sh";

	return userspace_stack_buffer(sh_path, sizeof(sh_path));
}

static char __user *ksud_user_path(void)
{
	static const char ksud_path[] = KSUD_PATH;

	return userspace_stack_buffer(ksud_path, sizeof(ksud_path));
}

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
		write_sulog('a');
		pr_info("faccessat su->sh!\n");
		*filename_user = sh_user_path();
	}

	return 0;
}

int ksu_handle_stat(int *dfd, const char __user **filename_user, int *flags)
{
	// const char sh[] = SH_PATH;
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
		write_sulog('s');
		pr_info("newfstatat su->sh!\n");
		*filename_user = sh_user_path();
	}

	return 0;
}

long ksu_handle_execve_sucompat(const char __user **filename_user, int orig_nr, const struct pt_regs *regs)
{
	const char su[] = SU_PATH;
	const char __user *fn;
	char path[sizeof(su) + 1];
	long ret;
	unsigned long addr;

	if (unlikely(!filename_user))
		goto do_orig_execve;

	if (!ksu_is_allow_uid_for_current(current_uid().val))
		goto do_orig_execve;

	addr = untagged_addr((unsigned long)*filename_user);
	fn = (const char __user *)addr;
	memset(path, 0, sizeof(path));

	ret = strncpy_from_user_nofault(path, fn, sizeof(path));
	if (ret < 0 && preempt_count()) {
		preempt_enable_no_resched_notrace();
		ret = strncpy_from_user(path, fn, sizeof(path));
		preempt_disable_notrace();
	}

	if (ret < 0) {
		goto do_orig_execve;
	}

	if (likely(memcmp(path, su, sizeof(su))))
		goto do_orig_execve;

    write_sulog('x');

    pr_info("sys_execve su found\n");
    *filename_user = ksud_user_path();

	ret = escape_with_root_profile();
	if (ret) {
		pr_err("escape_with_root_profile failed: %ld\n", ret);
		goto do_orig_execve;
	}
	if (preempt_count() > 0) {
		*filename_user = ksud_user_path();
	} else {
		struct file *f = ksu_filp_open_compat(KSUD_PATH, O_RDONLY, 0);
		if (IS_ERR(f)) {
			pr_warn("ksud inaccesible, aplicando fallback a sh\n");
			*filename_user = sh_user_path();
		} else {
			filp_close(f, NULL);
			*filename_user = ksud_user_path();
		}
	}
do_orig_execve:
	return 0;
}

int ksu_handle_execveat_sucompat(int *fd, struct filename **filename_ptr,
				 void *__never_use_argv, void *__never_use_envp,
				 int *__never_use_flags)
{
	struct filename *filename;
	const char su[] = SU_PATH;
	static const char ksud_path[] = KSUD_PATH;

	if (unlikely(!filename_ptr))
		return 0;

	if (!ksu_is_allow_uid_for_current(current_uid().val))
		return 0;

	filename = *filename_ptr;
	if (IS_ERR(filename))
		return 0;

	if (likely(memcmp(filename->name, su, sizeof(su))))
		return 0;

	pr_info("do_execveat_common su found\n");
	memcpy((void *)filename->name, ksud_path, sizeof(ksud_path));

	escape_with_root_profile();

	return 0;
}

int __ksu_handle_devpts(struct inode *inode)
{
#ifndef KSU_KPROBES_HOOK
	if (!ksu_su_compat_enabled)
		return 0;
#endif

	if (!current->mm) {
		return 0;
	}

	uid_t uid = current_uid().val;
	if (uid % 100000 < 10000) {
		// not untrusted_app, ignore it
		return 0;
	}

	if (likely(!ksu_is_allow_uid(uid)))
		return 0;

	struct inode_security_struct *sec = selinux_inode(inode);

	if (ksu_file_sid && sec)
		sec->sid = ksu_file_sid;
	return 0;
}

// dead code: devpts handling
int __maybe_unused ksu_handle_devpts(struct inode *inode)
{
	return __ksu_handle_devpts(inode);
}

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
