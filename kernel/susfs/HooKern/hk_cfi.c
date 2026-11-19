// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include "hk.h"
#include "hk_patch.h"
#include "hk_cfi.h"

#define HK_CFI_RET 0xD65F03C0
#define HK_CFI_MOV 0xD2800020

static int hk_patch_ret_addr(const char *what, unsigned long fn)
{
	u32 cur;
	u32 ret = HK_CFI_RET;

	if (!fn || !hk_ker_addr_ok(fn))
		return -ENOENT;
	if (copy_from_kernel_nofault(&cur, (void *)fn, sizeof(cur)))
		return -EFAULT;
	if (cur == ret)
		return 0;
	hk_write_text((void *)fn, &ret, 4);
	pr_info("[lkmhook] cfi bypass %s\n", what);
	return 0;
}

static int hk_patch_ret(const char *name)
{
	return hk_patch_ret_addr(name, hk_resolve(name));
}

static int hk_patch_mov_ret_addr(const char *what, unsigned long fn)
{
	u32 mov_ret[2] = { HK_CFI_MOV, HK_CFI_RET };
	u32 cur;

	if (!fn || !hk_ker_addr_ok(fn))
		return -ENOENT;
	if (copy_from_kernel_nofault(&cur, (void *)fn, sizeof(cur)))
		return -EFAULT;
	if (cur == mov_ret[0])
		return 0;
	hk_write_text((void *)fn, mov_ret, sizeof(mov_ret));
	pr_info("[lkmhook] cfi bypass %s\n", what);
	return 0;
}

int hk_cfi_bypass(void)
{
	static const char *const ret_syms[] = {
		"cfi_failure_handler",
		"__cfi_check_fail",
		"__ubsan_handle_cfi_check_fail_abort",
		"__ubsan_handle_cfi_check_fail",
		"__cfi_slowpath",
		"__cfi_slowpath_diag",
		"_cfi_slowpath",
	};
	unsigned long fn;
	int i;

	for (i = 0; i < (int)ARRAY_SIZE(ret_syms); i++)
		hk_patch_ret(ret_syms[i]);
	fn = hk_resolve("report_cfi_failure");
	hk_patch_mov_ret_addr("report_cfi_failure", fn);
	return 0;
}
