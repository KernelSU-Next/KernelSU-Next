/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_supercall_policy.c - supercall decision and dispatch (L layer)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/string.h>

#include "susfs_abi.h"
#include "susfs_enable_log.h"
#include "susfs_log.h"
#include "spoof_cmdline.h"
#include "sus_map.h"
#include "sus_mount.h"
#include "sus_path.h"
#include "susfs_avc_spoof.h"
#include "susfs_kstat.h"
#include "susfs_open_redirect.h"
#include "susfs_supercall_data.h"
#include "susfs_supercall_policy.h"
#include "susfs_uname.h"

static void susfs_show_version(void __user **arg)
{
	struct st_susfs_version info = {0};

	if (copy_from_user(&info, (void __user *)*arg, sizeof(info))) {
		info.err = -EFAULT;
		goto out;
	}
	strscpy(info.susfs_version, SUSFS_VERSION_STR, SUSFS_MAX_VERSION_BUFSIZE);
	info.err = 0;
out:
	if (copy_to_user((void __user *)*arg, &info, sizeof(info)))
		pr_warn("susfs show_version copy_to_user failed\n");
}

static void susfs_show_variant(void __user **arg)
{
	struct st_susfs_variant info = {0};

	if (copy_from_user(&info, (void __user *)*arg, sizeof(info))) {
		info.err = -EFAULT;
		goto out;
	}
	strscpy(info.susfs_variant, SUSFS_VARIANT_STR, SUSFS_MAX_VARIANT_BUFSIZE);
	info.err = 0;
out:
	if (copy_to_user((void __user *)*arg, &info, sizeof(info)))
		pr_warn("susfs show_variant copy_to_user failed\n");
}

static void susfs_show_enabled_features(void __user **arg)
{
	struct st_susfs_enabled_features *info;
	size_t off = 0;
	unsigned int i;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		/* Answering nothing would leave the caller unable to tell "no memory" from
		 * "the kernel said nothing": report it in the ABI's err field. */
		susfs_enabled_features_nomem.err = -ENOMEM;
		if (copy_to_user((void __user *)*arg, &susfs_enabled_features_nomem,
				 sizeof(susfs_enabled_features_nomem)))
			pr_warn("susfs show_enabled_features copy_to_user failed\n");
		pr_warn_ratelimited("susfs: show_enabled_features kzalloc failed, reported -ENOMEM\n");
		return;
	}

	for (i = 0; i < susfs_feature_table_count; i++) {
		const char *name = susfs_feature_table[i].name;
		size_t len = strlen(name);

		if (susfs_feature_table[i].feature >= 0 &&
		    !susfs_supercall_feature_armed(susfs_feature_table[i].feature))
			continue;
		if (off + len >= SUSFS_ENABLED_FEATURES_SIZE)
			break;
		memcpy(info->enabled_features + off, name, len);
		off += len;
	}
	info->err = 0;

	if (copy_to_user((void __user *)*arg, info, sizeof(*info)))
		pr_warn("susfs show_enabled_features copy_to_user failed\n");
	kfree(info);
}

int susfs_supercall_cmd_feature(unsigned int cmd)
{
	switch (cmd) {
	case CMD_SUSFS_ADD_SUS_PATH:
	case CMD_SUSFS_ADD_SUS_PATH_LOOP:
		return SUSFS_FEAT_SUS_PATH;
	case CMD_SUSFS_ADD_SUS_MAP:
		return SUSFS_FEAT_SUS_MAP;
	case CMD_SUSFS_ADD_SUS_KSTAT:
	case CMD_SUSFS_UPDATE_SUS_KSTAT:
	case CMD_SUSFS_ADD_SUS_KSTAT_STATICALLY:
		return SUSFS_FEAT_SUS_KSTAT;
	case CMD_SUSFS_SET_UNAME:
		return SUSFS_FEAT_SPOOF_UNAME;
	case CMD_SUSFS_ENABLE_LOG:
		return SUSFS_FEAT_ENABLE_LOG;
	case CMD_SUSFS_ENABLE_AVC_LOG_SPOOFING:
		return SUSFS_FEAT_AVC_SPOOF;
	case CMD_SUSFS_SET_CMDLINE_OR_BOOTCONFIG:
		return SUSFS_FEAT_SPOOF_CMDLINE;
	case CMD_SUSFS_ADD_OPEN_REDIRECT:
		return SUSFS_FEAT_OPEN_REDIRECT;
	case CMD_SUSFS_HIDE_SUS_MNTS_FOR_NON_SU_PROCS:
		return SUSFS_FEAT_SUS_MOUNT;
	default:
		return -1;
	}
}

bool susfs_supercall_cmd_handled(unsigned int cmd)
{
	int feature;

	switch (cmd) {
	case CMD_SUSFS_SHOW_VERSION:
	case CMD_SUSFS_SHOW_VARIANT:
	case CMD_SUSFS_SHOW_ENABLED_FEATURES:
		return true;
	default:
		break;
	}

	feature = susfs_supercall_cmd_feature(cmd);
	return feature >= 0 && susfs_supercall_feature_armed(feature);
}

void susfs_supercall_dispatch(unsigned int cmd, void __user **arg)
{
	switch (cmd) {
	case CMD_SUSFS_SHOW_VERSION:
		susfs_show_version(arg);
		break;
	case CMD_SUSFS_SHOW_VARIANT:
		susfs_show_variant(arg);
		break;
	case CMD_SUSFS_SHOW_ENABLED_FEATURES:
		susfs_show_enabled_features(arg);
		break;
	case CMD_SUSFS_ADD_SUS_PATH:
	case CMD_SUSFS_ADD_SUS_PATH_LOOP:
		/* The two commands answer a missing path differently upstream. */
		sus_path_supercall(cmd, arg);
		break;
	case CMD_SUSFS_ADD_SUS_MAP:
		susfs_sus_map_supercall(arg);
		break;
	case CMD_SUSFS_ADD_SUS_KSTAT:
	case CMD_SUSFS_UPDATE_SUS_KSTAT:
	case CMD_SUSFS_ADD_SUS_KSTAT_STATICALLY:
		susfs_kstat_supercall(cmd, arg);
		break;
	case CMD_SUSFS_SET_UNAME:
		susfs_uname_supercall(arg);
		break;
	case CMD_SUSFS_ENABLE_LOG:
		susfs_enable_log_supercall(arg);
		break;
	case CMD_SUSFS_ENABLE_AVC_LOG_SPOOFING:
		susfs_avc_spoof_supercall(arg);
		break;
	case CMD_SUSFS_SET_CMDLINE_OR_BOOTCONFIG:
		susfs_spoof_cmdline_supercall(arg);
		break;
	case CMD_SUSFS_ADD_OPEN_REDIRECT:
		susfs_open_redirect_supercall(arg);
		break;
	case CMD_SUSFS_HIDE_SUS_MNTS_FOR_NON_SU_PROCS:
		susfs_sus_mount_supercall(arg);
		break;
	default:
		/* Unreachable: the entry only defers a command cmd_handled()
		 * accepted.  Kept as a net in case the two lists drift apart. */
		pr_warn("susfs_guard_lkm: supercall: unsupported cmd 0x%x reached the worker (cmd_handled() and the switch disagree)\n",
			cmd);
		break;
	}
}
