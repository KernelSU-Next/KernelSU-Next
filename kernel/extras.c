#include <linux/security.h>
#include <linux/atomic.h>
#include <linux/version.h>

#include "feature.h"
#include "klog.h"
#include "ksud.h"
#include "seccomp_cache.h"
#include "selinux/selinux.h"

// sorry for the ifdef hell
// but im too lazy to fragment this out.
// theres only one feature so far anyway
// - xx, 20251019

static u32 su_sid = 0;

// init as disabled by default
static atomic_t disable_spoof = ATOMIC_INIT(1);

void ksu_avc_spoof_enable();
void ksu_avc_spoof_disable();

static bool ksu_avc_spoof_enabled = true;
static bool boot_completed = false;

static int avc_spoof_feature_get(u64 *value)
{
	*value = ksu_avc_spoof_enabled ? 1 : 0;
	return 0;
}

static int avc_spoof_feature_set(u64 value)
{
	bool enable = value != 0;

	if (enable == ksu_avc_spoof_enabled) {
		pr_info("avc_spoof: no need to change\n");
		return 0;
	}

	ksu_avc_spoof_enabled = enable;

	if (boot_completed) {
		if (enable) {
			ksu_avc_spoof_enable();
		} else {
			ksu_avc_spoof_disable();
		}
	}

	pr_info("avc_spoof: set to %d\n", enable);

	return 0;
}

static const struct ksu_feature_handler avc_spoof_handler = {
	.feature_id = KSU_FEATURE_AVC_SPOOF,
	.name = "avc_spoof",
	.get_handler = avc_spoof_feature_get,
	.set_handler = avc_spoof_feature_set,
};

static int get_sid()
{
	// dont load at all if we cant get sids
	int err = security_secctx_to_secid(KERNEL_SU_CONTEXT, strlen(KERNEL_SU_CONTEXT), &su_sid);
	if (err) {
		pr_info("avc_spoof/get_sid: su_sid not found!\n");
		return -1;
	}
	pr_info("avc_spoof/get_sid: su_sid: %u\n", su_sid);

	return 0;
}

static int ksu_handle_slow_avc_audit_new(u32 tsid, u16 *tclass)
{
	if (atomic_read(&disable_spoof))
		return 0;

	if (tsid != su_sid)
		return 0;

	// we can just zero out tclass for gki
	// since theres a check that if !tclass; return -EINVAL;
	// this way all logging is prevented for su_sid
	// this can cause splats due to WARN_ON though

	pr_info("avc_spoof/slow_avc_audit: prevent log for sid: %u\n", su_sid);
	*tclass = 0;

	return 0;
}

#ifdef KSU_KPROBES_HOOK
#include <linux/kprobes.h>
#include <linux/slab.h>
#include "arch.h"
static struct kprobe *slow_avc_audit_kp;

static int slow_avc_audit_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	if (atomic_read(&disable_spoof))
		return 0;

	u32 tsid;
	u16 *tclass;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	tsid = (u32)PT_REGS_PARM2(regs);
	tclass = (u16 *)&PT_REGS_PARM3(regs);
#else
	tsid = (u32)PT_REGS_PARM3(regs);
	tclass = (u16 *)&PT_REGS_CCALL_PARM4(regs);
#endif

	ksu_handle_slow_avc_audit_new(tsid, tclass);

	return 0;
}

// copied from upstream
static struct kprobe *init_kprobe(const char *name,
				  kprobe_pre_handler_t handler)
{
	struct kprobe *kp = kzalloc(sizeof(struct kprobe), GFP_KERNEL);
	if (!kp)
		return NULL;
	kp->symbol_name = name;
	kp->pre_handler = handler;

	int ret = register_kprobe(kp);
	pr_info("sucompat: register_%s kprobe: %d\n", name, ret);
	if (ret) {
		kfree(kp);
		return NULL;
	}

	return kp;
}
static void destroy_kprobe(struct kprobe **kp_ptr)
{
	struct kprobe *kp = *kp_ptr;
	if (!kp)
		return;
	unregister_kprobe(kp);
	synchronize_rcu();
	kfree(kp);
	*kp_ptr = NULL;
}
#endif // KSU_KPROBES_HOOK

void ksu_avc_spoof_disable(void)
{
#ifdef KSU_KPROBES_HOOK
	pr_info("avc_spoof/exit: unregister slow_avc_audit kprobe!\n");
	destroy_kprobe(&slow_avc_audit_kp);
#endif
	atomic_set(&disable_spoof, 1);
	pr_info("avc_spoof/exit: slow_avc_audit spoofing disabled!\n");
}

void ksu_avc_spoof_enable(void) 
{
	int ret = get_sid();
	if (ret) {
		pr_info("avc_spoof/init: sid grab fail!\n");
		return;
	}

#ifdef KSU_KPROBES_HOOK
	pr_info("avc_spoof/init: register slow_avc_audit kprobe!\n");
	slow_avc_audit_kp = init_kprobe("slow_avc_audit", slow_avc_audit_pre_handler);
#endif	
	// once we get the sids, we can now enable the hook handler
	atomic_set(&disable_spoof, 0);
	
	pr_info("avc_spoof/init: slow_avc_audit spoofing enabled!\n");
}

void ksu_avc_spoof_late_init()
{
	boot_completed = true;
	
    if (ksu_avc_spoof_enabled) {
		ksu_avc_spoof_enable();
	}
}

void ksu_avc_spoof_init()
{
	if (ksu_register_feature_handler(&avc_spoof_handler)) {
		pr_err("Failed to register avc spoof feature handler\n");
	}
}

void ksu_avc_spoof_exit()
{
	if (ksu_avc_spoof_enabled) {
		ksu_avc_spoof_disable();
	}
	ksu_unregister_feature_handler(KSU_FEATURE_AVC_SPOOF);
}
