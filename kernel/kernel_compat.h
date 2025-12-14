#ifndef __KSU_H_KERNEL_COMPAT
#define __KSU_H_KERNEL_COMPAT

#include <linux/fs.h>
#include <linux/version.h>
#include <linux/task_work.h>
#include "ss/policydb.h"
#include "linux/key.h"

#ifndef KSU_OPTIONAL_STRNCPY
extern long strncpy_from_user_nofault(char *dst, const void __user *unsafe_addr,
				   long count);
#endif // #ifndef KSU_OPTIONAL_STRNCPY

// Linux >= 5.7
// task_work_add (struct, struct, enum)
// Linux pre-5.7
// task_work_add (struct, struct, bool)
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 7, 0)
#ifndef TWA_RESUME
#define TWA_RESUME true
#endif
#endif

#endif // #ifndef __KSU_H_KERNEL_COMPAT