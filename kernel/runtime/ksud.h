#ifndef __KSU_H_KSUD
#define __KSU_H_KSUD

#include <linux/compat.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
#include <linux/compiler_types.h>
#else
#include <linux/compiler.h>
#endif
#include <linux/types.h>

#define KSUD_PATH "/data/adb/ksud"

struct filename;

/*
 * Mirrors the kernel's own struct user_arg_ptr (fs/exec.c): a wrapper around
 * an argv/envp array that records whether it came from a 32-bit compat task.
 * Declared here so the manual-hook bridge in core/init.c can use the real
 * prototype instead of re-declaring the entry point with a conflicting type.
 */
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

int ksu_handle_execveat_ksud(int *fd, struct filename **filename_ptr,
			     struct user_arg_ptr *argv,
			     struct user_arg_ptr *envp, int *flags);

void ksu_ksud_init();
void ksu_ksud_exit();

void ksu_stop_input_hook_runtime(void);

extern bool ksu_execveat_hook __read_mostly;

#endif