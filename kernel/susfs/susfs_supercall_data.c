/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_supercall_data.c - supercall registry and state (D layer)
 */

#include <linux/kernel.h>
#include <linux/bitmap.h>

#include "susfs_supercall_data.h"

/* List every feature this LKM implements: the first line marks the source (an
 * LKM, not a built-in SUSFS) and the rest use upstream CONFIG macro names.  An
 * entry is reported only while its layer armed it (susfs_supercall_arm_feature,
 * called by susfs_main.c after that layer's init returned 0). */
const struct feature_entry susfs_feature_table[] = {
	/* First line, and deliberately not a CONFIG_* name: the caller has to be able to tell this
	 * list apart from a built-in SUSFS's, whose features live in the kernel image and have a
	 * different lifecycle (no load/unload, no /sys/module entry, no module parameters). */
	{ "SUSFS_LKM_MODULES\n",		-1 },
	{ "CONFIG_KSU_SUSFS_SUS_PATH\n",	SUSFS_FEAT_SUS_PATH },
	{ "CONFIG_KSU_SUSFS_SUS_MOUNT\n",	SUSFS_FEAT_SUS_MOUNT },
	{ "CONFIG_KSU_SUSFS_SUS_KSTAT\n",	SUSFS_FEAT_SUS_KSTAT },
	{ "CONFIG_KSU_SUSFS_SPOOF_UNAME\n",	SUSFS_FEAT_SPOOF_UNAME },
	{ "CONFIG_KSU_SUSFS_ENABLE_LOG\n",	SUSFS_FEAT_ENABLE_LOG },
	{ "CONFIG_KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS\n",	SUSFS_FEAT_HIDE_SYMS },
	{ "CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG\n",	SUSFS_FEAT_SPOOF_CMDLINE },
	{ "CONFIG_KSU_SUSFS_OPEN_REDIRECT\n",	SUSFS_FEAT_OPEN_REDIRECT },
	{ "CONFIG_KSU_SUSFS_SUS_MAP\n",		SUSFS_FEAT_SUS_MAP },
	/* hide_modules is not upstream's, so it is not listed here - the feature itself is unchanged. */
};
const unsigned int susfs_feature_table_count = ARRAY_SIZE(susfs_feature_table);

static DECLARE_BITMAP(susfs_armed_features, SUSFS_FEAT_COUNT);

/* The -ENOMEM answer for CMD_SUSFS_SHOW_ENABLED_FEATURES is the whole ABI struct (8192-byte
 * string + err), copied in one piece.  As a local it broke the 6.1/6.6 builds: "stack frame
 * size (8320) exceeds limit (2048) in 'susfs_tw_func' [-Wframe-larger-than]", an error
 * there.  As a static it is harmless - BSS-zeroed, err = -ENOMEM, same for every caller. */
struct st_susfs_enabled_features susfs_enabled_features_nomem;

susfs_syscall_fn_t susfs_reboot_orig;
bool susfs_sc_fp_armed;		/* sys_call_table path armed */
bool susfs_sc_kp_armed;		/* kprobe fallback armed */

void susfs_supercall_arm_feature(enum susfs_feature feature)
{
	if ((int)feature >= 0 && feature < SUSFS_FEAT_COUNT)
		set_bit(feature, susfs_armed_features);
}

bool susfs_supercall_feature_armed(enum susfs_feature feature)
{
	return (int)feature >= 0 && feature < SUSFS_FEAT_COUNT &&
	       test_bit(feature, susfs_armed_features);
}
