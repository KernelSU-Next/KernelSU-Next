#ifndef __KSU_H_KLOG
#define __KSU_H_KLOG

#include <linux/printk.h>

#ifdef pr_fmt
#undef pr_fmt
#define pr_fmt(fmt) "KernelSU: " fmt
#endif

#ifdef CONFIG_KSU_DEBUG
#define ksu_dbg(fmt, ...) pr_info(fmt, ##__VA_ARGS__)
#else
#define ksu_dbg(fmt, ...) no_printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
#endif

#endif
