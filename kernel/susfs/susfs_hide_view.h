/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SUSFS_HIDE_VIEW_H
#define __SUSFS_HIDE_VIEW_H
#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
#include <stdbool.h>
#include <string.h>
#endif
/* Input is a NUL-terminated symbol name, validated by the physical adapter. */
/* prefixes to hide (matches upstream's list) */
static const char *const hide_prefixes[] = {
	"ksu_", "__ksu_", "susfs_", "susfs_guard_lkm", "ksud",
	"is_ksu_", "is_manager_", "escape_to_", "setup_selinux",
	"track_throne", "on_post_fs_data", "try_umount", "kernelsu",
	"__initcall__kmod_kernelsu", "apply_kernelsu", "handle_sepolicy",
	"getenforce", "setenforce", "is_zygote",
};

static inline bool susfs_hide_name_decide_core(const char *name)
{
	unsigned int i;

	for (i = 0; i < (sizeof(hide_prefixes) / sizeof(hide_prefixes[0])); i++)
		if (!strncmp(name, hide_prefixes[i], strlen(hide_prefixes[i])))
			return true;

	/* Substring form for the names whose prefix differs from the list above.  Measured: with the prefix rules alone a
	 * 257 -> 7 sweep left anon_ksu_fops, anon_ksu_ioctl(.cfi_jt), anon_ksu_release(.cfi_jt), setup_ksu_cred and
	 * is_task_ksu_domain visible - each a plain "KernelSU is loaded here" tell in /proc/kallsyms. */
	if (strstr(name, "ksu") || strstr(name, "susfs"))
		return true;

	return false;
}

#endif
