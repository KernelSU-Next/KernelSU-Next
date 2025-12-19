#include <linux/compiler.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
#include <linux/sched/signal.h>
#endif
#include <linux/slab.h>
#include <linux/task_work.h>
#include <linux/thread_info.h>
#include <linux/seccomp.h>
#include <linux/bpf.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/uidgid.h>
#include <linux/version.h>

#include "allowlist.h"
#include "setuid_hook.h"
#include "feature.h"
#include "klog.h" // IWYU pragma: keep
#include "manager.h"
#include "selinux/selinux.h"
#include "seccomp_cache.h"
#include "supercalls.h"
#include "syscall_hook_manager.h"
#include "kernel_umount.h"

static bool ksu_enhanced_security_enabled = false;

static int enhanced_security_feature_get(u64 *value)
{
    *value = ksu_enhanced_security_enabled ? 1 : 0;
    return 0;
}

static int enhanced_security_feature_set(u64 value)
{
    bool enable = value != 0;
    ksu_enhanced_security_enabled = enable;
    pr_info("enhanced_security: set to %d\n", enable);
    return 0;
}

static const struct ksu_feature_handler enhanced_security_handler = {
    .feature_id = KSU_FEATURE_ENHANCED_SECURITY,
    .name = "enhanced_security",
    .get_handler = enhanced_security_feature_get,
    .set_handler = enhanced_security_feature_set,
};

// force_sig kcompat, TODO: move it out of core_hook.c
// https://elixir.bootlin.com/linux/v5.3-rc1/source/kernel/signal.c#L1613
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
#define send_sigkill() force_sig(SIGKILL)
#else
#define send_sigkill() force_sig(SIGKILL, current)
#endif

extern void disable_seccomp(struct task_struct *tsk);

static void ksu_install_manager_fd_tw_func(struct callback_head *cb)
{
    ksu_install_fd();
    kfree(cb);
}

int ksu_handle_setresuid(uid_t ruid, uid_t euid, uid_t suid)
{
    // we rely on the fact that zygote always call setresuid(3) with same uids
    uid_t new_uid = ruid;
    uid_t old_uid = current_uid().val;

    pr_info("handle_setresuid from %d to %d\n", old_uid, new_uid);

    // if old process is root, ignore it.
    if (old_uid != 0 && ksu_enhanced_security_enabled) {
        // disallow any non-ksu domain escalation from non-root to root!
        // euid is what we care about here as it controls permission
        if (unlikely(euid == 0)) {
            if (!is_ksu_domain()) {
                pr_warn("find suspicious EoP: %d %s, from %d to %d\n", 
                    current->pid, current->comm, old_uid, new_uid);
                send_sigkill();
                return 0;
            }
        }
        // disallow appuid decrease to any other uid if it is not allowed to su
        if (is_appuid(old_uid)) {
            if (euid < current_euid().val && !ksu_is_allow_uid_for_current(old_uid)) {
                pr_warn("find suspicious EoP: %d %s, from %d to %d\n", 
                    current->pid, current->comm, old_uid, new_uid);
                send_sigkill();
                return 0;
            }
        }
        return 0;
    }

    if (ksu_get_manager_appid() == new_uid % PER_USER_RANGE) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
        ksu_seccomp_allow_cache(current->seccomp.filter, __NR_reboot);
#ifdef KSU_KPROBES_HOOK
		ksu_set_task_tracepoint_flag(current);
#endif
#else
		disable_seccomp(current);
#endif

        pr_info("install fd for manager: %d\n", new_uid);
        struct callback_head *cb = kzalloc(sizeof(*cb), GFP_ATOMIC);
        if (!cb)
            return 0;
        cb->func = ksu_install_manager_fd_tw_func;
        if (task_work_add(current, cb, TWA_RESUME)) {
            kfree(cb);
            pr_warn("install manager fd add task_work failed\n");
        }
        return 0;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
    if (ksu_is_allow_uid_for_current(new_uid)) {
        if (current->seccomp.mode == SECCOMP_MODE_FILTER &&
            current->seccomp.filter) {
            ksu_seccomp_allow_cache(current->seccomp.filter, __NR_reboot);
        }
#ifdef KSU_KPROBES_HOOK
		ksu_set_task_tracepoint_flag(current);
	} else {
		ksu_clear_task_tracepoint_flag_if_needed(current);
#endif
    }
#else
	if (ksu_is_allow_uid_for_current(new_uid)) {
		disable_seccomp(current);
	}
#endif

    // Handle kernel umount
    ksu_handle_umount(old_uid, new_uid);

    return 0;
}

extern void ksu_lsm_hook_init(void);
void ksu_setuid_hook_init(void)
{
    ksu_kernel_umount_init();
    if (ksu_register_feature_handler(&enhanced_security_handler)) {
        pr_err("Failed to register enhanced security feature handler\n");
    }
}

void ksu_setuid_hook_exit(void)
{
    pr_info("ksu_core_exit\n");
    ksu_kernel_umount_exit();
    ksu_unregister_feature_handler(KSU_FEATURE_ENHANCED_SECURITY);
}