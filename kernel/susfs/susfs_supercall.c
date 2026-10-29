// SPDX-License-Identifier: GPL-2.0
/*
 * susfs_supercall.c - SUSFS supercall dispatcher (reboot(2) ABI).
 *
 * ksu_susfs (and SukiSU ksud) call syscall(SYS_reboot, 0xDEADBEEF, 0xFAFAFAFA, cmd_id,
 * &payload) and the handler writes payload.err back.  Upstream SUSFS patches
 * kernel/reboot.c SYSCALL_DEFINE4 to branch into ksu_handle_sys_reboot(); an LKM cannot
 * patch that, so the dispatcher hooks the reboot entry itself.
 *
 * The delivery mechanism is deliberately version-gated: 6.18 is the BTI kernel where
 * replacing the sys_call_table entry is required, while every older KMI keeps the kprobe
 * entry path (6.12/6.6/6.1/5.15 have no BTI problem here, and the table hook is not
 * portable across their vendor/pKVM variants).
 *  - the kprobe path sees regs->regs[0] == struct pt_regs * (the wrapper's __regs
 *    argument); the real user args are in real_regs->regs[0..3] - SukiSU's
 *    PT_REAL_REGS() / arch.h.
 *  - a pre_handler runs in interrupt context and must not copy_from_user, so the command is
 *    deferred to task_work (TWA_RESUME), i.e. process context - the same trick SukiSU uses.
 * The handlers keep upstream's signature `void xxx(void __user **arg)`, *arg pointing at
 * the userspace payload struct.
 */
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/task_work.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/cred.h>
#include <linux/syscalls.h>
#include <linux/string.h>
#include <linux/bitmap.h>
#include <linux/version.h>
#include "susfs_abi.h"
#include "susfs_log.h"
#include "susfs.h"
#include "susfs_fp_hook.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0) && \
    LINUX_VERSION_CODE < KERNEL_VERSION(6, 19, 0)
#define SUSFS_REBOOT_USE_SCT 1
#else
#define SUSFS_REBOOT_USE_SCT 0
#endif

struct susfs_tw {
	struct callback_head cb;
	unsigned int cmd;
	void __user *payload;   /* points at the userspace payload struct */
};

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

/* List every feature this LKM implements: the first line marks the source (an LKM, not a built-in
 * SUSFS) and the rest use upstream CONFIG macro names.  An entry is reported only while its layer
 * armed it (susfs_supercall_arm_feature(), called by susfs_main.c after that layer's init returned
 * 0): advertising a feature whose registration failed is the exact inconsistency a detector probes
 * for, and the same bit is what lets the reboot(2) command through - one registry, so the list and
 * the command table cannot disagree. */
struct feature_entry {
	const char *name;
	int feature;		/* enum susfs_feature, or -1 = always present */
};

static DECLARE_BITMAP(susfs_armed_features, SUSFS_FEAT_COUNT);

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

static const struct feature_entry enabled_features[] = {
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

/* The -ENOMEM answer for CMD_SUSFS_SHOW_ENABLED_FEATURES is the whole ABI struct (8192-byte
 * string + err), copied in one piece.  As a local it broke the 6.1/6.6 builds: "stack frame
 * size (8320) exceeds limit (2048) in 'susfs_tw_func' [-Wframe-larger-than]", an error
 * there.  As a static it is harmless - BSS-zeroed, err = -ENOMEM, same for every caller. */
static struct st_susfs_enabled_features susfs_enabled_features_nomem;

static void susfs_show_enabled_features(void __user **arg)
{
	struct st_susfs_enabled_features *info;
	size_t off = 0;
	int i;

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

	for (i = 0; i < ARRAY_SIZE(enabled_features); i++) {
		const char *name = enabled_features[i].name;
		size_t len = strlen(name);

		if (enabled_features[i].feature >= 0 &&
		    !susfs_supercall_feature_armed(enabled_features[i].feature))
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

static void susfs_tw_func(struct callback_head *cb)
{
	struct susfs_tw *tw = container_of(cb, struct susfs_tw, cb);
	void __user *arg = tw->payload;

	switch (tw->cmd) {
	case CMD_SUSFS_SHOW_VERSION:
		susfs_show_version(&arg);
		break;
	case CMD_SUSFS_SHOW_VARIANT:
		susfs_show_variant(&arg);
		break;
	case CMD_SUSFS_SHOW_ENABLED_FEATURES:
		susfs_show_enabled_features(&arg);
		break;
	case CMD_SUSFS_ADD_SUS_PATH:
	case CMD_SUSFS_ADD_SUS_PATH_LOOP:
		/* The two commands answer a missing path differently upstream. */
		sus_path_supercall(tw->cmd, &arg);
		break;
	case CMD_SUSFS_ADD_SUS_MAP:
		susfs_sus_map_supercall(&arg);
		break;
	case CMD_SUSFS_ADD_SUS_KSTAT:
	case CMD_SUSFS_UPDATE_SUS_KSTAT:
	case CMD_SUSFS_ADD_SUS_KSTAT_STATICALLY:
		susfs_kstat_supercall(tw->cmd, &arg);
		break;
	case CMD_SUSFS_SET_UNAME:
		susfs_uname_supercall(&arg);
		break;
	case CMD_SUSFS_ENABLE_LOG:
		susfs_enable_log_supercall(&arg);
		break;
	case CMD_SUSFS_ENABLE_AVC_LOG_SPOOFING:
		susfs_avc_spoof_supercall(&arg);
		break;
	case CMD_SUSFS_SET_CMDLINE_OR_BOOTCONFIG:
		susfs_spoof_cmdline_supercall(&arg);
		break;
	case CMD_SUSFS_ADD_OPEN_REDIRECT:
		susfs_open_redirect_supercall(&arg);
		break;
	case CMD_SUSFS_HIDE_SUS_MNTS_FOR_NON_SU_PROCS:
		susfs_sus_mount_supercall(&arg);
		break;
	default:
		/* Unreachable: reboot_pre() only defers a command susfs_cmd_handled()
		 * accepted.  Kept as a net in case the two lists drift apart. */
		pr_warn("susfs_guard_lkm: supercall: unsupported cmd 0x%x reached the worker (susfs_cmd_handled() and the switch disagree)\n",
			tw->cmd);
		break;
	}
	kfree(tw);

	/* Pairs with the try_module_get() in reboot_pre(): the callback runs from module .text,
	 * after the syscall has already returned to userspace. */
	module_put(THIS_MODULE);
}

/* Commands susfs_tw_func() above actually dispatches - keep the two in sync.
 *
 * The kprobe consults this BEFORE swallowing the syscall: upstream answers an unrecognised
 * command with `return -EINVAL` from ksu_handle_sys_reboot()
 * (KernelSU/10_enable_susfs_for_ksu.patch:2925-2926), and reboot.c's `if (ret) goto
 * orig_flow;` falls through to the real reboot path, whose magic check rejects
 * 0xDEADBEEF/0xFAFAFAFA with -EINVAL.  Upstream writes NOTHING to payload.err then, and 126
 * (ERR_CMD_NOT_SUPPORTED) is a USERSPACE sentinel the ksu_susfs C tool pre-seeds and reads
 * back as "the kernel never handled this command" (ksu_susfs/jni/features/sus_map.c:51-53,
 * ksu_susfs/jni/includes/susfs_defs.h:16-18).  So an unknown command must NOT be hijacked:
 * leave the regs and err alone, while every command listed here still short-circuits to 0.
 *
 * A command is handled only while its feature is armed, so a command for a failed non-fatal
 * layer falls through exactly like an unknown one - the same registry SHOW_ENABLED_FEATURES
 * reads, so the advertised list and the accepted commands cannot disagree. */
static int susfs_cmd_feature(unsigned int cmd)
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

static bool susfs_cmd_handled(unsigned int cmd)
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

	feature = susfs_cmd_feature(cmd);
	return feature >= 0 && susfs_supercall_feature_armed(feature);
}

#if SUSFS_REBOOT_USE_SCT

static susfs_syscall_fn_t reboot_orig;

static long __nocfi reboot_wrapper(const struct pt_regs *regs)
{
	int magic1, magic2;
	unsigned int cmd;
	void __user *payload;
	struct susfs_tw *tw;

	magic1 = (int)regs->regs[0];
	magic2 = (int)regs->regs[1];
	cmd = (unsigned int)regs->regs[2];
	payload = (void __user *)regs->regs[3];

	if (magic1 != KSU_INSTALL_MAGIC1)
		goto orig;
	if (magic2 != SUSFS_MAGIC)
		goto orig;
	if (current_uid().val != 0)
		goto orig;

	if (!susfs_cmd_handled(cmd)) {
		SUSFS_LOGI("susfs supercall: unsupported cmd 0x%x\n", cmd);
		goto orig;
	}

	/* The callback runs from module .text when this task next returns to userspace.  Pin the
	 * module until it has run: the syscall table entry is restored before that on rmmod. */
	if (!try_module_get(THIS_MODULE))
		goto orig;

	tw = kzalloc(sizeof(*tw), GFP_ATOMIC);
	if (!tw) {
		module_put(THIS_MODULE);
		goto orig;
	}
	tw->cmd = cmd;
	tw->payload = payload;
	tw->cb.func = susfs_tw_func;

	if (task_work_add(current, &tw->cb, TWA_RESUME)) {
		kfree(tw);
		module_put(THIS_MODULE);
		pr_warn("susfs supercall: task_work_add failed\n");
		goto orig;
	}

	/* A handled supercall returns 0 to userspace; the command itself runs from task_work. */
	return 0;

orig:
	return reboot_orig ? reboot_orig(regs) : -EINVAL;
}

static struct susfs_fp_hook reboot_hook = {
	.nr = __NR_reboot,
	.nr_compat = 0,
	.name = "supercall/reboot",
	.sym = "__arm64_sys_reboot",
	.wrapper = reboot_wrapper,
	.orig_slot = &reboot_orig,
};

static bool sc_registered;

#else /* !SUSFS_REBOOT_USE_SCT: pre-6.18 kprobe delivery */

static int reboot_pre(struct kprobe *kp, struct pt_regs *regs)
{
	struct pt_regs *real_regs = (struct pt_regs *)regs->regs[0];
	int magic1, magic2;
	unsigned int cmd;
	void __user *payload;
	struct susfs_tw *tw;

	/* A prober runs before the callee, so an argument the callee would have checked is
	 * still raw here (see the filename_lookup lesson in sus_path.c). */
	if (!real_regs)
		return 0;

	magic1 = (int)real_regs->regs[0];
	magic2 = (int)real_regs->regs[1];
	cmd = (unsigned int)real_regs->regs[2];
	payload = (void __user *)real_regs->regs[3];

	if (magic1 != KSU_INSTALL_MAGIC1)
		return 0;
	if (magic2 != SUSFS_MAGIC)
		return 0;
	if (current_uid().val != 0)
		return 0;

	/* Not ours to answer: leave the syscall alone so reboot(2) reports the -EINVAL
	 * upstream reports and payload.err keeps the caller's value. */
	if (!susfs_cmd_handled(cmd)) {
		SUSFS_LOGI("susfs supercall: unsupported cmd 0x%x\n", cmd);
		return 0;
	}

	/* The callback runs from module .text when this task next returns to userspace, which can
	 * be after `rmmod` has already freed the module: the kprobe is on a vmlinux symbol, so
	 * its registration does not hold a reference for us, and unregister_kprobe() cannot
	 * cancel a task_work that is already queued.  Pin the module until the callback has
	 * run.  GFP_ATOMIC context, so no sleeping here. */
	if (!try_module_get(THIS_MODULE))
		return 0;

	tw = kzalloc(sizeof(*tw), GFP_ATOMIC);
	if (!tw) {
		module_put(THIS_MODULE);
		return 0;
	}
	tw->cmd = cmd;
	tw->payload = payload;
	tw->cb.func = susfs_tw_func;

	if (task_work_add(current, &tw->cb, TWA_RESUME)) {
		kfree(tw);
		module_put(THIS_MODULE);
		pr_warn("susfs supercall: task_work_add failed\n");
		return 0;
	}

	/* Upstream's reboot.c patch makes a handled supercall return 0 instead of falling
	 * through to the real reboot path:
	 *     if (ret) goto orig_flow;
	 *     return ret;
	 * We cannot patch reboot.c, so mirror it from the kprobe: skip the rest of
	 * __arm64_sys_reboot and return 0 - with these magic values reboot would otherwise
	 * return -EINVAL, and the prebuilt ksu_susfs tool checks the syscall result.  The
	 * command itself still runs from task_work before we return to userspace. */
	regs->pc = regs->regs[30];
	regs->regs[0] = 0;
	return 1;
}

static struct kprobe reboot_kp = {
	.symbol_name = "__arm64_sys_reboot",
	.pre_handler = reboot_pre,
};

static bool sc_kp_registered;

#endif /* SUSFS_REBOOT_USE_SCT */

/* ---- ABI layout assertions ----
 * These sizes and `err` offsets ARE the userspace contract (ksu_susfs, ksud and
 * tools/susfs_sc compile against these numbers); a drift shows up as "the command returned
 * 0 and userspace printed garbage", not as a build error, and the repository's own
 * abi_layout_check/ harness is not wired into any build step - hence the trip wire here.
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
#if SUSFS_REBOOT_USE_SCT
	rc = susfs_fp_install(&reboot_hook);
	if (rc) {
		pr_warn("susfs supercall: sys_call_table reboot hook failed %d\n", rc);
		return rc;
	}
	sc_registered = true;
	SUSFS_LOGI("susfs supercall: armed via sys_call_table (reboot ABI, version " SUSFS_VERSION_STR ")\n");
#else
	rc = register_kprobe(&reboot_kp);
	if (rc) {
		pr_warn("susfs supercall: register_kprobe(reboot) failed %d\n", rc);
		return rc;
	}
	sc_kp_registered = true;
	SUSFS_LOGI("susfs supercall: armed via kprobe (reboot ABI, version " SUSFS_VERSION_STR ")\n");
#endif
	return 0;
}

void susfs_supercall_exit(void)
{
#if SUSFS_REBOOT_USE_SCT
	if (sc_registered) {
		susfs_fp_remove(&reboot_hook);
		susfs_fp_drain();
		sc_registered = false;
	}
#else
	if (sc_kp_registered) {
		unregister_kprobe(&reboot_kp);
		sc_kp_registered = false;
	}
#endif
}
