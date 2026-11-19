// SPDX-License-Identifier: GPL-2.0
/*
 * susfs_supercall.c - SUSFS supercall dispatcher (reboot(2) ABI), F layer.
 *
 * The command decision and dispatch live in susfs_supercall_policy.c, the
 * reboot(2) entry hooks and task_work delivery in susfs_supercall_phys.c and
 * the registry/state in susfs_supercall_data.c.  This file owns the hook
 * description, the ABI trip wire and init/exit.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cred.h>

#include "susfs_abi.h"
#include "susfs_log.h"
#include "susfs_fp_hook.h"
#include "susfs_hooks.h"
#include "susfs_supercall.h"
#include "susfs_supercall_data.h"
#include "susfs_supercall_phys.h"

/* sys_call_table is the preferred backend; the kprobe is the documented fallback
 * for kernels that EL2-protect the table.  register() reports which one won
 * through hook.backend. */
static struct susfs_hook reboot_sc_hook = {
	.owner = "supercall",
	.name = "reboot",
	.target = SUSFS_HOOK_TARGET_ARM64_SYS_REBOOT,
	.fp = &susfs_supercall_reboot_fp,
	.probes = &susfs_supercall_reboot_probe,
	.nprobes = 1,
};

/* ---- ABI layout assertions ----
 * These sizes and `err` offsets ARE the userspace contract (ksu_susfs, ksud and
 * tools/susfs_sc compile against these numbers); a drift shows up as "the command returned
 * 0 and userspace printed garbage", not as a build error.
 * Values are aarch64 LP64, the only ABI shipped clients use, and match upstream's structs
 * (kernel_patches/include/linux/susfs.h). */
static void __init susfs_abi_layout_check(void)
{
	BUILD_BUG_ON(sizeof(struct st_susfs_sus_path) != 260);
	BUILD_BUG_ON(offsetof(struct st_susfs_sus_path, err) != 256);

	BUILD_BUG_ON(sizeof(struct st_susfs_sus_map) != 260);
	BUILD_BUG_ON(offsetof(struct st_susfs_sus_map, err) != 256);

	BUILD_BUG_ON(sizeof(struct st_susfs_sus_kstat) != 376);
	BUILD_BUG_ON(offsetof(struct st_susfs_sus_kstat, err) != 372);

	BUILD_BUG_ON(sizeof(struct st_susfs_uname) != 136);
	BUILD_BUG_ON(offsetof(struct st_susfs_uname, err) != 132);

	BUILD_BUG_ON(sizeof(struct st_susfs_log) != 8);
	BUILD_BUG_ON(offsetof(struct st_susfs_log, err) != 4);

	BUILD_BUG_ON(sizeof(struct st_susfs_avc_log_spoofing) != 8);
	BUILD_BUG_ON(offsetof(struct st_susfs_avc_log_spoofing, err) != 4);

	BUILD_BUG_ON(sizeof(struct st_susfs_hide_sus_mnts_for_non_su_procs) != 8);
	BUILD_BUG_ON(offsetof(struct st_susfs_hide_sus_mnts_for_non_su_procs, err) != 4);

	BUILD_BUG_ON(sizeof(struct st_susfs_open_redirect) != 520);
	BUILD_BUG_ON(offsetof(struct st_susfs_open_redirect, err) != 516);

	BUILD_BUG_ON(sizeof(struct st_susfs_spoof_cmdline_or_bootconfig) != 8196);
	BUILD_BUG_ON(offsetof(struct st_susfs_spoof_cmdline_or_bootconfig, err) != 8192);

	BUILD_BUG_ON(sizeof(struct st_susfs_version) != 20);
	BUILD_BUG_ON(offsetof(struct st_susfs_version, err) != 16);

	BUILD_BUG_ON(sizeof(struct st_susfs_variant) != 20);
	BUILD_BUG_ON(offsetof(struct st_susfs_variant, err) != 16);

	BUILD_BUG_ON(sizeof(struct st_susfs_enabled_features) != 8196);
	BUILD_BUG_ON(offsetof(struct st_susfs_enabled_features, err) != 8192);
}

int susfs_supercall_init(void)
{
	int rc;

	susfs_abi_layout_check();

	rc = susfs_hook_register(&reboot_sc_hook);
	if (rc < 0) {
		pr_warn("susfs supercall: no reboot backend could be installed (%d)\n", rc);
		return rc;
	}
	if (reboot_sc_hook.backend == SUSFS_HOOK_BACKEND_FP) {
		susfs_sc_fp_armed = true;
		SUSFS_LOGI("susfs supercall: armed via sys_call_table (reboot ABI, version " SUSFS_VERSION_STR ")\n");
	} else {
		susfs_sc_kp_armed = true;
		pr_warn("susfs supercall: sys_call_table reboot hook unavailable, using kprobe fallback\n");
		SUSFS_LOGI("susfs supercall: armed via kprobe (reboot ABI, version " SUSFS_VERSION_STR ")\n");
	}
	return 0;
}

bool susfs_supercall_kprobe_active(void)
{
	return susfs_sc_kp_armed;
}

void susfs_supercall_exit(void)
{
	susfs_hook_unregister(&reboot_sc_hook);
	if (susfs_sc_fp_armed) {
		susfs_fp_drain();
		susfs_sc_fp_armed = false;
	}
	susfs_sc_kp_armed = false;
}
