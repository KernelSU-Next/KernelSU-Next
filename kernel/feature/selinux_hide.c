#include <linux/fs.h>
#include <linux/jump_label.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <asm/set_memory.h>
#include <linux/namei.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include "policy/feature.h"
#include "include/ksu.h"
#include  "uapi/feature.h"
#include "selinux/selinux.h"
#include "feature/selinux_hide.h"

static struct page *fake_status = NULL;
static DEFINE_MUTEX(fake_status_init_mutex);

extern bool ksu_input_hook __read_mostly;
extern struct selinux_state selinux_state;

// enabled by default
static bool ksu_selinux_hide_is_enabled __read_mostly = true;

static u32 ksu_sid __read_mostly = 0;
static u32 priv_app_sid __read_mostly = 0;

static int ksu_selinux_get_sids(void)
{
	int err1 = security_secctx_to_secid("u:r:ksu:s0", strlen("u:r:ksu:s0"), &ksu_sid);
	int err2 = security_secctx_to_secid("u:r:priv_app:s0:c512,c768",
					     strlen("u:r:priv_app:s0:c512,c768"), &priv_app_sid);
	if (!err1) pr_info("ksu_selinux_hide: ksu_sid=%u\n", ksu_sid);
	if (!err2) pr_info("ksu_selinux_hide: priv_app_sid=%u\n", priv_app_sid);
	return (!ksu_sid || !priv_app_sid) ? -1 : 0;
}

#if defined(CONFIG_KPROBES)
#include <linux/kprobes.h>
static struct kprobe *slow_avc_audit_kp;

static int slow_avc_audit_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 0) && defined(KSU_COMPAT_USE_SELINUX_STATE)
	u32 *tsid = (u32 *)&PT_REGS_PARM3(regs);
#else
	u32 *tsid = (u32 *)&PT_REGS_PARM2(regs);
#endif
	if (ksu_selinux_hide_is_enabled && *tsid == ksu_sid) {
		*tsid = priv_app_sid;
	}
	return 0;
}

static struct kprobe *init_kprobe(const char *name, kprobe_pre_handler_t handler)
{
	struct kprobe *kp = kzalloc(sizeof(*kp), GFP_KERNEL);
	if (!kp)
		return NULL;
	kp->symbol_name = name;
	kp->pre_handler = handler;
	if (register_kprobe(kp)) {
		kfree(kp);
		return NULL;
	}
	return kp;
}

static void destroy_kprobe(struct kprobe **kp_ptr)
{
	if (!*kp_ptr)
		return;
	unregister_kprobe(*kp_ptr);
	synchronize_rcu();
	kfree(*kp_ptr);
	*kp_ptr = NULL;
}
#endif

static void ksu_selinux_hide_enable(void)
{
	if (ksu_selinux_get_sids())
		pr_warn("ksu_selinux_hide: sid grab failed\n");
#if defined(CONFIG_KPROBES)
	slow_avc_audit_kp = init_kprobe("slow_avc_audit", slow_avc_audit_pre_handler);
#endif
}

static void ksu_selinux_hide_disable(void)
{
#if defined(CONFIG_KPROBES)
	destroy_kprobe(&slow_avc_audit_kp);
#endif
}

static void initialize_fake_status(void)
{
	if (READ_ONCE(fake_status))
		return;

	mutex_lock(&fake_status_init_mutex);
	if (fake_status) /* double-check after lock */
		goto out;

#ifdef KSU_COMPAT_USE_SELINUX_STATE
	struct page *real_page = selinux_kernel_status_page(&selinux_state);
#else
	struct page *real_page = selinux_kernel_status_page();
#endif
	if (!real_page) {
		pr_warn("ksu_selinux_hide: status_page not exists\n");
		goto out;
	}

	struct selinux_kernel_status *status = page_address(real_page);
	if (!status->enforcing && !ksu_late_loaded) {
		pr_warn("ksu_selinux_hide: skip not enforcing\n");
		goto out;
	}

	struct page *new_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!new_page) {
		pr_err("ksu_selinux_hide: failed to allocate fake status page\n");
		goto out;
	}

	struct selinux_kernel_status *new_status = page_address(new_page);
	memcpy(new_status, status, sizeof(*status));
	if (ksu_late_loaded && !new_status->enforcing) {
		/*
		 * In late_load mode we may be loaded after setenforce 0.
		 * Adjust sequence to look like a normal enforcing boot.
		 * Assumes setenforce 0 was called exactly once.
		 */
		new_status->enforcing = 1;
		new_status->sequence = 4;
	}
	
	WRITE_ONCE(fake_status, new_page);
	pr_info("ksu_selinux_hide: fake status ready: sequence=%d policyload=%d enforcing=%d\n",
		new_status->sequence, new_status->policyload,
		new_status->enforcing);
out:
	mutex_unlock(&fake_status_init_mutex);
}

typedef int (*sel_open_handle_status_fn)(struct inode *inode,
					 struct file *filp);
static sel_open_handle_status_fn orig_sel_open_handle_status = NULL;

static int __nocfi my_sel_open_handle_status(struct inode *inode, struct file *filp)
{
	if (likely(test_thread_flag(TIF_SECCOMP) &&
	current_uid().val >= 10000 &&
		   ksu_selinux_hide_is_enabled)) {
		struct page *data = READ_ONCE(fake_status);
		if (data) {
			filp->private_data = page_address(data);
			return 0;
		}
	}

	return orig_sel_open_handle_status(inode, filp);
}

#define FORCE_VOLATILE(x) *(volatile typeof(x) *)&(x)

static int patch_fops_open(struct file_operations *ops,
			    sel_open_handle_status_fn new_open)
{
	unsigned long addr = (unsigned long)&ops->open;
	unsigned long base = addr & PAGE_MASK;
	unsigned long offset = addr & ~PAGE_MASK;
	
	struct page *page = phys_to_page(__pa(base));
	if (!page)
		return -EFAULT;

void *writable_addr = vmap(&page, 1, VM_MAP, PAGE_KERNEL);
	if (!writable_addr)
		return -ENOMEM;

	void **target_slot = (void **)((unsigned long)writable_addr + offset);

	preempt_disable();
	local_irq_disable();
	FORCE_VOLATILE(*target_slot) = (void *)new_open;
	local_irq_enable();
	preempt_enable();

	vunmap(writable_addr);
	smp_mb();
	return 0;
}

static int resolve_fops(const char *path_str, struct file_operations **out_fops)
{
	struct path path;
	int error = kern_path(path_str, LOOKUP_FOLLOW, &path);
	if (error) {
		pr_err("ksu_selinux_hide: kern_path(%s) failed: %d\n", path_str, error);
		return error;
	}
	
	int ret = -ENOENT;
	if (!path.dentry || !d_inode(path.dentry))
		goto out;
	
	*out_fops = (struct file_operations *)d_inode(path.dentry)->i_fop;
	if (!*out_fops)
		goto out;

	ret = 0;
out:
	path_put(&path);
	return ret;
}

static void hook_selinux_status_open(void)
{
	if (orig_sel_open_handle_status)
	return;

	struct file_operations *ops = NULL;
	if (resolve_fops("/sys/fs/selinux/status", &ops)) {
		pr_err("ksu_selinux_hide: sel_handle_status_ops not found, fake status disabled\n");
		return;
	}

	if (!ops->open) {
		pr_err("ksu_selinux_hide: sel_handle_status_ops->open is NULL\n");
		return;
	}
	
	orig_sel_open_handle_status = ops->open;
	patch_fops_open(ops, my_sel_open_handle_status);
	pr_info("ksu_selinux_hide: hooked sel_handle_status_ops->open\n");
}

static void unhook_selinux_status_open(void)
{
	if (!orig_sel_open_handle_status)
	return;

	struct file_operations *ops = NULL;
	if (resolve_fops("/sys/fs/selinux/status", &ops)) {
		pr_err("ksu_selinux_hide: sel_handle_status_ops not found on unhook\n");
		return;
}

	patch_fops_open(ops, orig_sel_open_handle_status);
	orig_sel_open_handle_status = NULL;
	pr_info("ksu_selinux_hide: unhooked sel_handle_status_ops->open\n");
}

static int selinux_hide_status_feature_get(u64 *value)
{
	*value = ksu_selinux_hide_is_enabled ? 1 : 0;
	return 0;
}

static int selinux_hide_status_feature_set(u64 value)
{
	bool enable = !!value;
	if (enable == ksu_selinux_hide_is_enabled) {
		pr_info("ksu_selinux_hide: no need to change\n");
		return 0;
	}
	ksu_selinux_hide_is_enabled = enable;

	if (!ksu_selinux_hide_is_enabled)
		ksu_selinux_hide_disable();
	else
		ksu_selinux_hide_enable();

	pr_info("ksu_selinux_hide: set to %d\n", enable);
	return 0;
}

static const struct ksu_feature_handler selinux_hide_status_handler = {
	.feature_id = KSU_FEATURE_SELINUX_HIDE_STATUS,
	.name = "selinux_hide_status",
	.get_handler = selinux_hide_status_feature_get,
	.set_handler = selinux_hide_status_feature_set,
};

static int ksu_hide_init_thread(void *data)
{
	set_user_nice(current, 19);

	while (READ_ONCE(ksu_input_hook))
		msleep(5000);

	if (ksu_selinux_hide_is_enabled)
		ksu_selinux_hide_enable();

	int tries = 0;
try_again:
	initialize_fake_status();
	if (READ_ONCE(fake_status))
		goto page_ok;

	msleep(1000);
	if (++tries > 10) {
		pr_warn("ksu_selinux_hide: giving up on fake status page after %d tries\n", tries);
		return 0;
	}
	goto try_again;

page_ok:
	hook_selinux_status_open();
	return 0;
}

void __init ksu_selinux_hide_init(void)
{
	if (ksu_register_feature_handler(&selinux_hide_status_handler))
		pr_err("ksu_selinux_hide: failed to register feature handler\n");

	kthread_run(ksu_hide_init_thread, NULL, "ksu_selinux_hide_init");
}

void __exit ksu_selinux_hide_exit(void)
{
	ksu_unregister_feature_handler(KSU_FEATURE_SELINUX_HIDE_STATUS);
	unhook_selinux_status_open();
	ksu_selinux_hide_disable();
	mutex_lock(&fake_status_init_mutex);
	if (fake_status) {
		__free_page(fake_status);
		fake_status = NULL;
	}
	mutex_unlock(&fake_status_init_mutex);
}