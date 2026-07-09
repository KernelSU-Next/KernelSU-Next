#ifndef __KSU_H_USER_ARG_PTR
#define __KSU_H_USER_ARG_PTR

#include <linux/compat.h>
#include <linux/uaccess.h>

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

#endif