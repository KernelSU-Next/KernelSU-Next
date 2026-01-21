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
#ifdef CONFIG_KSU_SUSFS
#include <linux/susfs_def.h>
#include <linux/namei.h>
#include "selinux/selinux.h"
#include "objsec.h"
#endif // #ifdef CONFIG_KSU_SUSFS

#include "allowlist.h"
#include "feature.h"
#include "klog.h" // IWYU pragma: keep
#include "ksud.h"
#include "sucompat.h"
#include "app_profile.h"
#include "util.h"

extern void write_sulog(uint8_t sym);

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
	// To avoid having to mmap a page in userspace, just write below the stack
	// pointer.
	char __user *p = (void __user *)current_user_stack_pointer() - len;

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

#ifndef CONFIG_KSU_SUSFS
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

int ksu_handle_execve_sucompat(const char __user **filename_user,
				void *__never_use_argv, void *__never_use_envp,
				int *__never_use_flags)
{
	const char su[] = SU_PATH;
	const char __user *fn;
	char path[sizeof(su) + 1];
	long ret;
	unsigned long addr;

	if (unlikely(!filename_user))
		return 0;

	if (!ksu_is_allow_uid_for_current(current_uid().val))
		return 0;

	addr = untagged_addr((unsigned long)*filename_user);
	fn = (const char __user *)addr;
	memset(path, 0, sizeof(path));
	ret = strncpy_from_user_nofault(path, fn, sizeof(path));

	if (ret < 0 && try_set_access_flag(addr)) {
		ret = strncpy_from_user_nofault(path, fn, sizeof(path));
	}

	if (ret < 0 && preempt_count()) {
		/* This is crazy, but we know what we are doing:
			* Temporarily exit atomic context to handle page faults, then restore it */
		pr_info("Access filename failed, try rescue..\n");
		preempt_enable_no_resched_notrace();
		ret = strncpy_from_user(path, fn, sizeof(path));
		preempt_disable_notrace();
	}

	if (ret < 0) {
		pr_warn("Access filename when execve failed: %ld", ret);
		return 0;
	}

	if (likely(memcmp(path, su, sizeof(su))))
		return 0;

    write_sulog('x');

    pr_info("sys_execve su found\n");
    *filename_user = ksud_user_path();

	escape_with_root_profile();

	return 0;
}
#else
static const char sh_path[] = SH_PATH;
static const char su_path[] = SU_PATH;
static const char ksud_path[] = KSUD_PATH;

extern bool ksu_kernel_umount_enabled;

/*
 * return 0 -> No further checks should be required afterwards
 * return 1 -> Further checks should be continued afterwards
 */
int ksu_handle_execveat_init(struct filename *filename) {
	if (current->pid != 1 && is_init(get_current_cred())) {
		if (unlikely(strcmp(filename->name, KSUD_PATH) == 0)) {
			pr_info("hook_manager: escape to root for init executing ksud: %d\n", current->pid);
			escape_to_root_for_init();
		} else if (likely(strstr(filename->name, "/app_process") == NULL &&
				strstr(filename->name, "/adbd") == NULL) &&
				!susfs_is_current_proc_umounted())
		{
			pr_info("susfs: mark no sucompat checks for pid: '%d', exec: '%s'\n", current->pid, filename->name);
			susfs_set_current_proc_umounted();
		}
		return 0;
	}
	return 1;
}

// the call from execve_handler_pre won't provided correct value for __never_use_argument, use them after fix execve_handler_pre, keeping them for consistence for manually patched code
int ksu_handle_execveat_sucompat(int *fd, struct filename **filename_ptr,
			void *__never_use_argv, void *__never_use_envp,
			int *__never_use_flags)
{
	struct filename *filename;

	if (unlikely(!filename_ptr))
		return 0;

	filename = *filename_ptr;
	if (IS_ERR(filename)) {
		return 0;
	}

	if (!ksu_handle_execveat_init(filename)) {
		return 0;
	}

	if (likely(memcmp(filename->name, su_path, sizeof(su_path))))
		return 0;

	pr_info("ksu_handle_execveat_sucompat: su found\n");
	memcpy((void *)filename->name, ksud_path, sizeof(ksud_path));

	escape_with_root_profile();

	return 0;
}

int ksu_handle_execveat(int *fd, struct filename **filename_ptr, void *argv,
		void *envp, int *flags)
{
	if (ksu_handle_execveat_ksud(fd, filename_ptr, argv, envp, flags)) {
		return 0;
	}
	return ksu_handle_execveat_sucompat(fd, filename_ptr, argv, envp,
				flags);
}

int ksu_handle_faccessat(int *dfd, const char __user **filename_user, int *mode,
		int *__unused_flags)
{
	char path[sizeof(su_path) + 1] = {0};

	strncpy_from_user_nofault(path, *filename_user, sizeof(path));

	if (unlikely(!memcmp(path, su_path, sizeof(su_path)))) {
		pr_info("ksu_handle_faccessat: su->sh!\n");
		*filename_user = sh_user_path();
	}

	return 0;

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
int ksu_handle_stat(int *dfd, struct filename **filename, int *flags) {
	if (unlikely(IS_ERR(*filename) || (*filename)->name == NULL)) {
		return 0;
	}

	if (likely(memcmp((*filename)->name, su_path, sizeof(su_path)))) {
		return 0;
	}

	pr_info("ksu_handle_stat: su->sh!\n");
	memcpy((void *)((*filename)->name), sh_path, sizeof(sh_path));
	return 0;
}
#else
int ksu_handle_stat(int *dfd, const char __user **filename_user, int *flags)
{
	if (unlikely(!filename_user)) {
		return 0;
	}

	char path[sizeof(su_path) + 1] = {0};

	strncpy_from_user_nofault(path, *filename_user, sizeof(path));

	if (unlikely(!memcmp(path, su_path, sizeof(su_path)))) {
		pr_info("ksu_handle_stat: su->sh!\n");
		*filename_user = sh_user_path();
	}

	return 0;
}
#endif // #if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)

int ksu_handle_devpts(struct inode *inode)
{
	if (!current->mm) {
		return 0;
	}

	uid_t uid = current_uid().val;
	if (uid % 100000 < 10000) {
		// not untrusted_app, ignore it
		return 0;
	}

	if (!__ksu_is_allow_uid_for_current(uid))
		return 0;

	if (ksu_file_sid) {
		struct inode_security_struct *sec = selinux_inode(inode);
		if (sec) {
			sec->sid = ksu_file_sid;
		}
	}

	return 0;
}
#endif // #ifndef CONFIG_KSU_SUSFS

// sucompat: permitted process can execute 'su' to gain root access.
void ksu_sucompat_init()
{
	if (ksu_register_feature_handler(&su_compat_handler)) {
		pr_err("Failed to register su_compat feature handler\n");
	}
}

void ksu_sucompat_exit()
{
	ksu_unregister_feature_handler(KSU_FEATURE_SU_COMPAT);
}
