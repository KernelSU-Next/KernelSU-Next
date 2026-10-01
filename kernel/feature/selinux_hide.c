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
#include "ksu.h"
#include "arch.h"
#include "uapi/feature.h"
#include "uapi/selinux.h"
#include "selinux/selinux.h"
#include "feature/selinux_hide.h"
#include "compat/kernel_compat.h"

#ifndef KSU_KPROBES_HOOK
extern bool ksu_input_hook __read_mostly;
#endif
static struct page *fake_status = NULL;
static DEFINE_MUTEX(fake_status_init_mutex);
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

void ksu_slow_avc_audit(u32 *tsid)
{
	if (unlikely(!ksu_selinux_hide_is_enabled))
		return;

	if (*tsid != ksu_sid)
		return;

	pr_info("selinux_hide: slow_avc_audit: replace tsid: %u with priv_app_sid: %u\n", *tsid, priv_app_sid);
	*tsid = priv_app_sid;

	return;
}

char *ksu_hide_type_list __read_mostly = NULL;
size_t ksu_hide_type_len = 0;
char *ksu_hide_rule_list __read_mostly = NULL;
size_t ksu_hide_rule_len = 0;
DEFINE_MUTEX(selinux_hide_list_mutex);

static inline bool ksu_should_destroy_context(char *str)
{
	if (!str)
		return false;

	char padded[160];
	int n = snprintf(padded, sizeof(padded), ":%s:", str);
	if (n < 0 || n >= (int)sizeof(padded))
		return false;

	bool status = false;
	mutex_lock(&selinux_hide_list_mutex);

	size_t offset = 0;
	while (offset < ksu_hide_type_len) {
		const char *current_entry = ksu_hide_type_list + offset;

		if (strstr(padded, current_entry)) {
			status = true;
			goto out_unlock;
		}

		offset = offset + strlen(current_entry) + 1;
	}
	// double strstr
	char *str2 = strchr(str, ' ');
	if (!str2)
		goto out_unlock;

	char padded2[160];
	int n2 = snprintf(padded2, sizeof(padded2), ":%s:", str2 + 1);
	if (n2 < 0 || n2 >= (int)sizeof(padded2))
		goto out_unlock;

	offset = 0;
	while (offset < ksu_hide_rule_len) {
		const char *src_rule = ksu_hide_rule_list + offset;
		size_t src_sz = strlen(src_rule) + 1;

		const char *tgt_rule = src_rule + src_sz;
		size_t tgt_sz = strlen(tgt_rule) + 1;

		if (strstr(padded, src_rule) && strstr(padded2, tgt_rule)) {
			status = true;
			goto out_unlock;
		}

		offset = offset + src_sz + tgt_sz;
	}

out_unlock:
	mutex_unlock(&selinux_hide_list_mutex);
	return status;
}

void ksu_add_probe_to_list(u32 cmd, const char *args[])
{
	if (!args || !args[0])
		return;

	int argc = sepol_expected_argc(cmd);
	if (argc < 1)
		return;

	mutex_lock(&selinux_hide_list_mutex);

	if (cmd == KSU_SEPOLICY_CMD_TYPE || cmd == KSU_SEPOLICY_CMD_TYPE_ATTR || cmd == KSU_SEPOLICY_CMD_TYPE_STATE || cmd == KSU_SEPOLICY_CMD_ATTR) {

		const char *name = args[0];

		// anti duplicate: compare against stored (unpadded) name directly,
		// no fixed-size scratch buffer, so long names can't be truncated
		// into a false match/false non-match.
		if (ksu_hide_type_list) {
			size_t offset = 0;
			size_t name_len = strlen(name);
			while (offset < ksu_hide_type_len) {
				const char *current_type = ksu_hide_type_list + offset;
				size_t cur_len = strlen(current_type);
				if (cur_len == name_len + 2 &&
				    memcmp(current_type + 1, name, name_len) == 0)
					goto out_unlock; // already tracked
				offset += cur_len + 1;
			}
		}

		size_t needed_len = strlen(name) + 3; // :type:\0
		size_t new_total_len = ksu_hide_type_len + needed_len;

		char *new_ptr = krealloc(ksu_hide_type_list, new_total_len, GFP_KERNEL);
		if (!new_ptr)
			goto out_unlock;

		ksu_hide_type_list = new_ptr;

		char *w_ptr = ksu_hide_type_list + ksu_hide_type_len;
		sprintf(w_ptr, ":%s:", name);

		ksu_hide_type_len = new_total_len;

		pr_info("selinux_hide: tracking type: %s\n", w_ptr);

	} else if (argc >= 2) {

		if (!args[1])
			goto out_unlock;

		const char *src = args[0];
		const char *tgt = args[1];

		size_t src_needed = strlen(src) + 3; // :src:\0
		size_t tgt_needed = strlen(tgt) + 3; // :tgt:\0
		size_t needed_len = src_needed + tgt_needed;

		// anti duplicate: same approach as the type list above -
		// compare unpadded stored strings directly, no fixed buffer.
		if (ksu_hide_rule_list) {
			size_t offset = 0;
			size_t src_len = strlen(src);
			size_t tgt_len = strlen(tgt);
			while (offset < ksu_hide_rule_len) {
				const char *src_chk = ksu_hide_rule_list + offset;
				size_t src_sz = strlen(src_chk) + 1;

				const char *tgt_chk = src_chk + src_sz;
				size_t tgt_sz = strlen(tgt_chk) + 1;

				size_t src_chk_len = src_sz - 1;
				size_t tgt_chk_len = tgt_sz - 1;

				if (src_chk_len == src_len + 2 &&
				    tgt_chk_len == tgt_len + 2 &&
				    memcmp(src_chk + 1, src, src_len) == 0 &&
				    memcmp(tgt_chk + 1, tgt, tgt_len) == 0)
					goto out_unlock; // already tracked

				offset = offset + src_sz + tgt_sz;
			}
		}

		size_t new_total_len = ksu_hide_rule_len + needed_len;
		char *new_ptr = krealloc(ksu_hide_rule_list, new_total_len, GFP_KERNEL);
		if (!new_ptr)
			goto out_unlock;

		ksu_hide_rule_list = new_ptr;

		char *w_ptr_src = ksu_hide_rule_list + ksu_hide_rule_len;
		sprintf(w_ptr_src, ":%s:", src);

		char *w_ptr_tgt = w_ptr_src + strlen(w_ptr_src) + 1;
		sprintf(w_ptr_tgt, ":%s:", tgt);

		ksu_hide_rule_len = new_total_len;

		pr_info("selinux_hide: tracking rule: %s %s\n", w_ptr_src, w_ptr_tgt);
	}

out_unlock:
	mutex_unlock(&selinux_hide_list_mutex);
}

#if defined(CONFIG_KPROBES)
#include <linux/kprobes.h>

static struct kprobe *slow_avc_audit_kp;

static int slow_avc_audit_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	u32 *tsid = (u32 *)&PT_REGS_PARM2(regs);

	ksu_slow_avc_audit(tsid);

	return 0;
}

static struct kprobe *init_kprobe(const char *name, kprobe_pre_handler_t handler)
{
	struct kprobe *kp = kzalloc(sizeof(struct kprobe), GFP_KERNEL);
	if (!kp)
		return NULL;
	kp->symbol_name = name;
	kp->pre_handler = handler;

	int ret = register_kprobe(kp);
	pr_info("%s: register %s kprobe: %d\n", __func__, name, ret);
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
#endif // CONFIG_KPROBES

static void ksu_selinux_hide_enable(void)
{
	if (ksu_selinux_get_sids())
		pr_warn("ksu_selinux_hide: sid grab failed\n");
#if defined(CONFIG_KPROBES)
	slow_avc_audit_kp = init_kprobe("slow_avc_audit", slow_avc_audit_pre_handler);
#endif

	ksu_selinux_hide_is_enabled = true;
}

static void ksu_selinux_hide_disable(void)
{
#if defined(CONFIG_KPROBES)
	pr_info("selinux_hide: unregitered slow_avc_audit");
	destroy_kprobe(&slow_avc_audit_kp);
#endif

	pr_info("selinux_hide: going down");
	ksu_selinux_hide_is_enabled = false;
}

static ssize_t (*selinux_transaction_write_fn)(struct file *file, const char __user *buf, size_t size, loff_t *pos) __read_mostly = NULL;
static __nocfi ssize_t ksu_selinux_transaction_write(struct file *file, const char __user *buf, size_t size, loff_t *pos)
{
	if (unlikely(!ksu_selinux_hide_is_enabled))
		goto skip_destroy;

	if (!test_thread_flag(TIF_SECCOMP))
		goto skip_destroy;

	if (current_uid().val < 10000)
		goto skip_destroy;

	char kbuf[128] = { 0 };
	if (ksu_copy_from_user_retry(kbuf, buf, 127))
		goto skip_destroy;

	if (!ksu_should_destroy_context(kbuf))
		goto skip_destroy;

	// or copy_to_user? is it writable? or we vm_mmap? or hunt for writable section on start_stack again?
	// NOTE: if this is 'timeable', to equalize, we should call selinux_transaction_write_fn before ret EINVAL
	pr_info("selinux_hide: selinux_transaction_write: destroy: %s \n", kbuf);
	return -EINVAL;

skip_destroy:
	return selinux_transaction_write_fn(file, buf, size, pos);
}

#if defined(KSU_COMPAT_USE_SELINUX_STATE)
extern struct selinux_state selinux_state;
#define ksu_selinux_kernel_status_page() selinux_kernel_status_page(&selinux_state)
#else
#define ksu_selinux_kernel_status_page() selinux_kernel_status_page()
#endif
static u32 ksu_hidden_reload_count = 0; /* # of KSU-caused reloads we've suppressed */
static DEFINE_SPINLOCK(fake_status_sync_lock);

void ksu_hide_notify_reload(bool ksu_triggered)
{
	struct page *real_page = ksu_selinux_kernel_status_page();
	struct page *fp = smp_load_acquire(&fake_status);
	if (!real_page || !fp)
		return;

	struct selinux_kernel_status *real = page_address(real_page);
	struct selinux_kernel_status *fake = page_address(fp);

	spin_lock(&fake_status_sync_lock);

	if (ksu_triggered) {
		/* our own reload just happened on the real page; don't
		 * reflect it on the fake page, just remember we owe an offset */
		ksu_hidden_reload_count++;
		spin_unlock(&fake_status_sync_lock);
		return;
	}

	if (unlikely(ksu_hidden_reload_count > real->policyload)) {
		pr_warn("ksu_selinux_hide: reload count desync (hidden=%u real=%u), resetting\n",
			ksu_hidden_reload_count, real->policyload);
		ksu_hidden_reload_count = 0;
	}

	/* external reload: catch fake up to real, minus what we've hidden */
	fake->policyload = real->policyload - ksu_hidden_reload_count;
	fake->sequence   = real->sequence - (ksu_hidden_reload_count * 2);
	fake->enforcing  = real->enforcing;

	spin_unlock(&fake_status_sync_lock);
}

static int ksu_status_poll_thread(void *data)
{
	u32 last_seen_policyload = 0;

	while (!kthread_should_stop()) {
		struct page *real_page = ksu_selinux_kernel_status_page();
		if (real_page) {
			struct selinux_kernel_status *real = page_address(real_page);
			if (real->policyload != last_seen_policyload) {
				last_seen_policyload = real->policyload;
				ksu_hide_notify_reload(false); /* treat as external, sync fake */
			}
		}
		msleep(2000);
	}
	return 0;
}

static void initialize_fake_status(void)
{
	if (READ_ONCE(fake_status))
		return;

	mutex_lock(&fake_status_init_mutex);
	if (fake_status) /* double-check after lock */
		goto out;

	struct page *real_page = ksu_selinux_kernel_status_page();

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

	smp_store_release(&fake_status, new_page);
	pr_info("ksu_selinux_hide: fake status ready: sequence=%d policyload=%d enforcing=%d\n",
		new_status->sequence, new_status->policyload,
		new_status->enforcing);
out:
	mutex_unlock(&fake_status_init_mutex);
}

typedef int (*sel_open_handle_status_fn)(struct inode *inode,
					 struct file *filp);
static sel_open_handle_status_fn orig_sel_open_handle_status = NULL;

static int __nocfi ksu_sel_open_handle_status(struct inode *inode, struct file *filp)
{
	if (likely(test_thread_flag(TIF_SECCOMP) &&
	current_uid().val >= 10000 &&
		   ksu_selinux_hide_is_enabled)) {
		struct page *data = smp_load_acquire(&fake_status);
		if (data) {
			filp->private_data = data;
			return 0;
		}
	}

	return orig_sel_open_handle_status(inode, filp);
}

static int patch_fops_member(void **member_addr, void *new_fn)
{
	unsigned long addr = (unsigned long)member_addr;
	unsigned long base = addr & PAGE_MASK;
	unsigned long offset = addr & ~PAGE_MASK;

	struct page *page = phys_to_page(__pa(base));
	if (!page)
		return -EFAULT;

	void *writable_addr = vmap(&page, 1, VM_MAP, PAGE_KERNEL);
	if (!writable_addr)
		return -ENOMEM;

	void **target_slot = (void **)((unsigned long)writable_addr + offset);

	WRITE_ONCE(*target_slot, new_fn);
	smp_mb();

	vunmap(writable_addr);
	return 0;
}

static int patch_fops_open(struct file_operations *ops,
					sel_open_handle_status_fn new_open)
{
	return patch_fops_member((void **)&ops->open, (void *)new_open);
}

static int patch_fops_write(struct file_operations *ops,
			     ssize_t (*new_write)(struct file *, const char __user *, size_t, loff_t *))
{
	return patch_fops_member((void **)&ops->write, (void *)new_write);
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

static void hook_selinux_transaction_write(void)
{
	if (selinux_transaction_write_fn)
		return;

	struct file_operations *ops = NULL;
	if (resolve_fops("/sys/fs/selinux/context", &ops)) {
		pr_err("ksu_selinux_hide: sel_context_ops not found, context hide disabled\n");
		return;
	}

	if (!ops->write) {
		pr_err("ksu_selinux_hide: sel_context_ops->write is NULL\n");
		return;
	}

	selinux_transaction_write_fn = ops->write;
	patch_fops_write(ops, ksu_selinux_transaction_write);
	pr_info("ksu_selinux_hide: hooked sel_context_ops->write\n");
}

static void unhook_selinux_transaction_write(void)
{
	if (!selinux_transaction_write_fn)
		return;

	struct file_operations *ops = NULL;
	if (resolve_fops("/sys/fs/selinux/context", &ops)) {
		pr_err("ksu_selinux_hide: sel_context_ops not found on unhook\n");
		return;
	}

	patch_fops_write(ops, selinux_transaction_write_fn);
	selinux_transaction_write_fn = NULL;
	pr_info("ksu_selinux_hide: unhooked sel_context_ops->write\n");
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
	patch_fops_open(ops, ksu_sel_open_handle_status);
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

/*
 * The init thread's one hook attempt can run before selinuxfs is mounted
 * (no wait in kprobes builds); retry here like the status hook does.
 */
void ksu_selinux_hide_handle_second_stage(void)
{
	hook_selinux_transaction_write();
	initialize_fake_status();
	if (READ_ONCE(fake_status))
		hook_selinux_status_open();
}

void ksu_selinux_hide_handle_post_fs_data(void)
{
	hook_selinux_transaction_write();
	initialize_fake_status();
	if (READ_ONCE(fake_status))
		hook_selinux_status_open();
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
	.feature_id = KSU_FEATURE_SELINUX_HIDE,
	.name = "selinux_hide_status",
	.get_handler = selinux_hide_status_feature_get,
	.set_handler = selinux_hide_status_feature_set,
};

/* file-scope so ksu_selinux_hide_exit() can stop it (was a local static,
 * which was invisible outside ksu_hide_init_thread) */
static struct task_struct *ksu_status_poll_task = NULL;

static int ksu_hide_init_thread(void *data)
{
	set_user_nice(current, 19);

#ifndef KSU_KPROBES_HOOK
	while (READ_ONCE(ksu_input_hook))
		msleep(5000);
#endif

	if (ksu_selinux_hide_is_enabled)
		ksu_selinux_hide_enable();

	hook_selinux_transaction_write();

	int tries = 0;
try_again:
	initialize_fake_status();
	if (smp_load_acquire(&fake_status))
		goto page_ok;

	msleep(1000);
	if (++tries > 10) {
		pr_warn("ksu_selinux_hide: giving up on fake status page after %d tries\n", tries);
		return 0;
	}
	goto try_again;

page_ok:
	hook_selinux_status_open();

	ksu_status_poll_task = kthread_run(ksu_status_poll_thread, NULL, "ksu_selinux_poll");
	if (IS_ERR(ksu_status_poll_task)) {
		pr_err("ksu_selinux_hide: failed to start poll thread: %ld\n",
		       PTR_ERR(ksu_status_poll_task));
		ksu_status_poll_task = NULL;
	}
	return 0;
}

void __init ksu_selinux_hide_init(void)
{
	if (ksu_register_feature_handler(&selinux_hide_status_handler))
		pr_err("ksu_selinux_hide: failed to register feature handler\n");

	ksu_add_probe_to_list(KSU_SEPOLICY_CMD_TYPE, (const char *[]){ KERNEL_SU_DOMAIN, NULL });
	ksu_add_probe_to_list(KSU_SEPOLICY_CMD_TYPE, (const char *[]){ KERNEL_SU_FILE, NULL });
	kthread_run(ksu_hide_init_thread, NULL, "ksu_selinux_hide_init");
}

void __exit ksu_selinux_hide_exit(void)
{
	ksu_unregister_feature_handler(KSU_FEATURE_SELINUX_HIDE);

	if (ksu_status_poll_task) {
		kthread_stop(ksu_status_poll_task);
		ksu_status_poll_task = NULL;
	}

	unhook_selinux_status_open();
	unhook_selinux_transaction_write();
	ksu_selinux_hide_disable();
	/* fake_status is intentionally never freed: filp->private_data on any
	 * still-open /sys/fs/selinux/status fd may reference it indefinitely,
	 * and there is no refcount tying page lifetime to open-file lifetime. */
}