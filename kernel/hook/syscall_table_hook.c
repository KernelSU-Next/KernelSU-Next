#ifdef KSU_SYSCALL_TABLE_HOOK

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/preempt.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <asm/ptrace.h>
#if defined(__aarch64__)
#include <asm/memory.h>
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/task_stack.h>
#else
#include <linux/sched.h>
#endif

#include "arch.h"
#include "klog.h" // IWYU pragma: keep
#include "compat/kernel_compat.h"
#include "hook/syscall_table_hook.h"
#include "hook/patch_memory.h"
#include "infra/symbol_resolver.h"
#include "policy/app_profile.h"
#include "selinux/selinux.h"
#include "feature/sucompat.h"
#include "feature/adb_root.h"
#include "runtime/ksud.h"

// The pt_regs-based syscall ABI (__arm64_sys_*/__x64_sys_*) exists since 4.17
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 17, 0)
#error "syscall table hook requires kernel >= 4.17 (pt_regs syscall ABI)"
#endif

typedef long (*syscall_fn_t)(const struct pt_regs *regs);

static syscall_fn_t *ksu_sth_syscall_table = NULL;

#define KSU_STH_MAX_HOOKS 16

struct ksu_sth_entry {
	int nr;
	syscall_fn_t orig;
	syscall_fn_t wrapper;
};

static DEFINE_MUTEX(ksu_sth_lock);
static struct ksu_sth_entry ksu_sth_entries[KSU_STH_MAX_HOOKS];
static int ksu_sth_count = 0;

// Some architectures (e.g. x86_64) do not provide untagged_addr()
#ifndef untagged_addr
#define untagged_addr(addr) (addr)
#endif

static syscall_fn_t ksu_sth_get_orig(int nr)
{
	int i;
	for (i = 0; i < ksu_sth_count; i++) {
		if (ksu_sth_entries[i].nr == nr)
			return ksu_sth_entries[i].orig;
	}
	return NULL;
}

static inline long ksu_sth_call_orig(int nr, const struct pt_regs *regs)
{
	syscall_fn_t orig = ksu_sth_get_orig(nr);
	if (unlikely(!orig))
		return -ENOSYS;
	return orig(regs);
}

static int ksu_sth_patch(int nr, syscall_fn_t fn)
{
	if (!ksu_sth_syscall_table)
		return -ENOENT;

	pr_info("sth: patch syscall %d: 0x%lx -> 0x%lx\n", nr,
		(unsigned long)READ_ONCE(ksu_sth_syscall_table[nr]),
		(unsigned long)fn);

	if (ksu_patch_text(&ksu_sth_syscall_table[nr], &fn, sizeof(fn),
			   KSU_PATCH_TEXT_FLUSH_DCACHE)) {
		pr_err("sth: patch syscall %d failed\n", nr);
		return -EIO;
	}
	return 0;
}

static void ksu_sth_record(int nr, syscall_fn_t orig)
{
	int i;
	for (i = 0; i < ksu_sth_count; i++) {
		if (ksu_sth_entries[i].nr == nr)
			return;
	}
	if (ksu_sth_count >= KSU_STH_MAX_HOOKS) {
		pr_err("sth: entry table full, cannot track syscall %d\n", nr);
		return;
	}
	ksu_sth_entries[ksu_sth_count].nr = nr;
	ksu_sth_entries[ksu_sth_count].orig = orig;
	ksu_sth_entries[ksu_sth_count].wrapper = NULL;
	ksu_sth_count++;
}

// init exec tracker for the table hook scheme: escape when init executes ksud.
// The tracepoint-based unmark logic does not apply here (no task marking).
static void ksu_sth_init_exec_tracker(const char __user *filename_user)
{
	char path[64];
	unsigned long addr;
	const char __user *fn;
	long ret;

	addr = untagged_addr((unsigned long)filename_user);
	fn = (const char __user *)addr;

	memset(path, 0, sizeof(path));
	ret = strncpy_from_user_nofault(path, fn, sizeof(path));
	if (ret < 0 && preempt_count()) {
		preempt_enable_no_resched_notrace();
		ret = strncpy_from_user(path, fn, sizeof(path));
		preempt_disable_notrace();
	}

	if (ret < 0)
		return;

	if (unlikely(strcmp(path, KSUD_PATH) == 0)) {
		pr_info("sth: escape to root for init executing ksud: %d\n",
			current->pid);
		escape_to_root_for_init();
	}
}

#ifdef __NR_execve
static long ksu_sth_execve(const struct pt_regs *regs)
{
	const char __user **filename_user =
		(const char __user **)&PT_REGS_PARM1(regs);
	long adb_ret = 0;

	if (current->pid != 1 && is_init(current_cred())) {
		ksu_sth_init_exec_tracker(*filename_user);
		adb_ret = ksu_adb_root_handle_execve((struct pt_regs *)regs);
		if (adb_ret)
			pr_err("sth: adb root failed: %ld\n", adb_ret);
	} else {
		ksu_handle_execve_sucompat(filename_user, __NR_execve, regs);
	}

	return ksu_sth_call_orig(__NR_execve, regs);
}
#endif

#ifdef __NR_execveat
static long ksu_sth_execveat(const struct pt_regs *regs)
{
	const char __user **filename_user =
		(const char __user **)&PT_REGS_PARM2(regs);
	long adb_ret = 0;

	// New bionic maps execve to execveat(AT_FDCWD, path, argv, envp, 0)
	if ((int)PT_REGS_PARM1(regs) == AT_FDCWD &&
	    (int)PT_REGS_SYSCALL_PARM4(regs) == 0) {
		if (current->pid != 1 && is_init(current_cred())) {
			ksu_sth_init_exec_tracker(*filename_user);
			adb_ret = ksu_adb_root_handle_execveat(
				(struct pt_regs *)regs);
			if (adb_ret)
				pr_err("sth: adb root failed: %ld\n", adb_ret);
		} else {
			ksu_handle_execveat_sucompat_user(filename_user,
							  __NR_execveat, regs);
		}
	}

	return ksu_sth_call_orig(__NR_execveat, regs);
}
#endif

#ifdef __NR_faccessat
static long ksu_sth_faccessat(const struct pt_regs *regs)
{
	int *dfd = (int *)&PT_REGS_PARM1(regs);
	const char __user **filename_user =
		(const char __user **)&PT_REGS_PARM2(regs);
	int *mode = (int *)&PT_REGS_PARM3(regs);

	ksu_handle_faccessat(dfd, filename_user, mode, NULL);

	return ksu_sth_call_orig(__NR_faccessat, regs);
}
#endif

#ifdef __NR_newfstatat
static long ksu_sth_newfstatat(const struct pt_regs *regs)
{
	int *dfd = (int *)&PT_REGS_PARM1(regs);
	const char __user **filename_user =
		(const char __user **)&PT_REGS_PARM2(regs);
	int *flags = (int *)&PT_REGS_SYSCALL_PARM4(regs);

	ksu_handle_stat(dfd, filename_user, flags);

	return ksu_sth_call_orig(__NR_newfstatat, regs);
}
#endif

void __init ksu_syscall_table_hook_init(void)
{
	struct {
		int nr;
		syscall_fn_t wrapper;
	} hooks[] = {
#ifdef __NR_execve
		{ __NR_execve, ksu_sth_execve },
#endif
#ifdef __NR_execveat
		{ __NR_execveat, ksu_sth_execveat },
#endif
#ifdef __NR_faccessat
		{ __NR_faccessat, ksu_sth_faccessat },
#endif
#ifdef __NR_newfstatat
		{ __NR_newfstatat, ksu_sth_newfstatat },
#endif
	};
	int i, patched = 0;

	ksu_init_symbol_resolver();

	ksu_sth_syscall_table =
		(syscall_fn_t *)ksu_resolve_symbol_for_functable_hook(
			"sys_call_table");
	pr_info("sth: sys_call_table=0x%lx\n",
		(unsigned long)ksu_sth_syscall_table);

	if (!ksu_sth_syscall_table) {
		pr_err("sth: sys_call_table not found, table hook disabled\n");
		return;
	}

	mutex_lock(&ksu_sth_lock);

	for (i = 0; i < ARRAY_SIZE(hooks); i++) {
		syscall_fn_t orig =
			READ_ONCE(ksu_sth_syscall_table[hooks[i].nr]);
		if (!orig) {
			pr_warn("sth: syscall %d has no original, skip\n",
				hooks[i].nr);
			continue;
		}
		ksu_sth_record(hooks[i].nr, orig);
		if (!ksu_sth_patch(hooks[i].nr, hooks[i].wrapper))
			patched++;
	}

	mutex_unlock(&ksu_sth_lock);

	pr_info("sth: %d/%ld syscalls hooked\n", patched,
		(long)ARRAY_SIZE(hooks));
}

void __exit ksu_syscall_table_hook_exit(void)
{
	int i;

	if (!ksu_sth_syscall_table)
		return;

	mutex_lock(&ksu_sth_lock);

	for (i = 0; i < ksu_sth_count; i++) {
		int nr = ksu_sth_entries[i].nr;
		syscall_fn_t orig = ksu_sth_entries[i].orig;

		pr_info("sth: restore syscall %d to 0x%lx\n", nr,
			(unsigned long)orig);
		if (ksu_patch_text(&ksu_sth_syscall_table[nr], &orig,
				   sizeof(orig),
				   KSU_PATCH_TEXT_FLUSH_DCACHE))
			pr_err("sth: restore syscall %d failed\n", nr);
	}
	ksu_sth_count = 0;

	mutex_unlock(&ksu_sth_lock);
}

#else // !KSU_SYSCALL_TABLE_HOOK

#include "hook/syscall_table_hook.h"

void __init ksu_syscall_table_hook_init(void)
{
}

void __exit ksu_syscall_table_hook_exit(void)
{
}

#endif // KSU_SYSCALL_TABLE_HOOK
