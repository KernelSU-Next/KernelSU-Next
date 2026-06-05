#include "selinux.h"
#include "linux/cred.h"
#include "linux/sched.h"
#include "linux/security.h"
#include "objsec.h"
#include "linux/version.h"
#include "klog.h" // IWYU pragma: keep
#include "ksu.h"

/*
 * Cached SID values for frequently checked contexts.
 * These are resolved once at init and used for fast u32 comparison
 * instead of expensive string operations on every check.
 *
 * A value of 0 means "no cached SID is available" for that context.
 * This covers both the initial "not yet cached" state and any case
 * where resolving the SID (e.g. via security_secctx_to_secid) failed.
 * In all such cases we intentionally fall back to the slower
 * string-based comparison path; this degrades performance only and
 * does not cause a functional failure.
 */
static u32 cached_su_sid __read_mostly = 0;
static u32 cached_zygote_sid __read_mostly = 0;
static u32 cached_init_sid __read_mostly = 0;
u32 ksu_file_sid __read_mostly = 0;

static int transive_to_domain(const char *domain, struct cred *cred)
{
    struct task_security_struct *tsec;
    u32 sid;
    int error;

    tsec = selinux_cred(cred);
    if (!tsec) {
        pr_err("tsec == NULL!\n");
        return -1;
    }

    error = security_secctx_to_secid(domain, strlen(domain), &sid);
    if (error) {
        pr_info("security_secctx_to_secid %s -> sid: %d, error: %d\n", domain,
                sid, error);
    }
    if (!error) {
        tsec->sid = sid;
        tsec->create_sid = 0;
        tsec->keycreate_sid = 0;
        tsec->sockcreate_sid = 0;
    }
    return error;
}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(4, 19, 0)
bool __maybe_unused
is_ksu_transition(const struct task_security_struct *old_tsec,
		  const struct task_security_struct *new_tsec)
{
	static u32 ksu_sid;
	char *secdata;
	u32 seclen;
	bool allowed = false;

	if (!ksu_sid)
		security_secctx_to_secid(KERNEL_SU_CONTEXT,
					 strlen(KERNEL_SU_CONTEXT), &ksu_sid);

	if (security_secid_to_secctx(old_tsec->sid, &secdata, &seclen))
		return false;

	allowed = (!strcmp("u:r:init:s0", secdata) && new_tsec->sid == ksu_sid);
	security_release_secctx(secdata, seclen);
	return allowed;
}
#endif

void setup_selinux(const char *domain, struct cred *cred)
{
    if (transive_to_domain(domain, cred)) {
        pr_err("transive domain failed.\n");
        return;
    }
}

void setup_ksu_cred(void)
{
    if (ksu_cred && transive_to_domain(KERNEL_SU_CONTEXT, ksu_cred)) {
        pr_err("setup ksu cred failed.\n");
    }
}

void setenforce(bool enforce)
{
#ifdef CONFIG_SECURITY_SELINUX_DEVELOP
#ifdef KSU_COMPAT_USE_SELINUX_STATE
	selinux_state.enforcing = enforce;
#else
	selinux_enforcing = enforce;
#endif
#endif
}

bool getenforce(void)
{
#ifdef CONFIG_SECURITY_SELINUX_DISABLE
#ifdef KSU_COMPAT_USE_SELINUX_STATE
	if (selinux_state.disabled) {
		return false;
	}
#else
	if (selinux_disabled) {
		return false;
	}
#endif // KSU_COMPAT_USE_SELINUX_STATE
#endif // CONFIG_SECURITY_SELINUX_DISABLE

#ifdef CONFIG_SECURITY_SELINUX_DEVELOP
#ifdef KSU_COMPAT_USE_SELINUX_STATE
	return selinux_state.enforcing;
#else
	return selinux_enforcing;
#endif
#else
	return true;
#endif
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 14, 0)
struct lsm_context {
    char *context;
    u32 len;
};

static int __security_secid_to_secctx(u32 secid, struct lsm_context *cp)
{
    return security_secid_to_secctx(secid, &cp->context, &cp->len);
}
static void __security_release_secctx(struct lsm_context *cp)
{
    security_release_secctx(cp->context, cp->len);
}
#else
#define __security_secid_to_secctx security_secid_to_secctx
#define __security_release_secctx security_release_secctx
#endif

/*
 * Initialize cached SID values for frequently checked SELinux contexts.
 * Called once after SELinux policy is loaded (post-fs-data).
 * This eliminates expensive string comparisons in hot paths.
 */

void cache_sid(void)
{
    int err;

    err = security_secctx_to_secid(KERNEL_SU_CONTEXT, strlen(KERNEL_SU_CONTEXT),
                                   &cached_su_sid);
    if (err) {
        pr_warn("Failed to cache kernel su domain SID: %d\n", err);
        cached_su_sid = 0;
    } else {
        pr_info("Cached su SID: %u\n", cached_su_sid);
    }

    err = security_secctx_to_secid(ZYGOTE_CONTEXT, strlen(ZYGOTE_CONTEXT),
                                   &cached_zygote_sid);
    if (err) {
        pr_warn("Failed to cache zygote SID: %d\n", err);
        cached_zygote_sid = 0;
    } else {
        pr_info("Cached zygote SID: %u\n", cached_zygote_sid);
    }

    err = security_secctx_to_secid(INIT_CONTEXT, strlen(INIT_CONTEXT),
                                   &cached_init_sid);
    if (err) {
        pr_warn("Failed to cache init SID: %d\n", err);
        cached_init_sid = 0;
    } else {
        pr_info("Cached init SID: %u\n", cached_init_sid);
    }

    err = security_secctx_to_secid(KSU_FILE_CONTEXT, strlen(KSU_FILE_CONTEXT),
                                   &ksu_file_sid);
    if (err) {
        pr_warn("Failed to cache ksu_file SID: %d\n", err);
        ksu_file_sid = 0;
    } else {
        pr_info("Cached ksu_file SID: %u\n", ksu_file_sid);
    }
}

/*
 * Fast path: compare task's SID directly against cached value.
 * Falls back to string comparison if cache is not initialized.
 */
static bool is_sid_match(const struct cred *cred, u32 cached_sid,
                         const char *fallback_context)
{
    if (!cred) {
        return false;
    }
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 18, 0)
    const struct task_security_struct *tsec = selinux_cred(cred);
#else
    const struct cred_security_struct *tsec = selinux_cred(cred);
#endif
    if (!tsec) {
        return false;
    }
    
    // Fast path: use cached SID if available
    if (likely(cached_sid != 0)) {
        return tsec->sid == cached_sid;
    }

    // Slow path fallback: string comparison (only before cache is initialized)
    struct lsm_context ctx;
    bool result;
    if (__security_secid_to_secctx(tsec->sid, &ctx)) {
        return false;
    }
    result = strncmp(fallback_context, ctx.context, ctx.len) == 0;
    __security_release_secctx(&ctx);
    return result;
}

bool is_task_ksu_domain(const struct cred *cred)
{
    return is_sid_match(cred, cached_su_sid, KERNEL_SU_CONTEXT);
}

bool is_ksu_domain(void)
{
    return is_task_ksu_domain(current_cred());
}

bool is_zygote(const struct cred *cred)
{
    return is_sid_match(cred, cached_zygote_sid, ZYGOTE_CONTEXT);
}

bool is_init(const struct cred *cred)
{
    return is_sid_match(cred, cached_init_sid, INIT_CONTEXT);
}

/* --------------- fake SELinux status page --------------- */

#include <linux/fs.h>
#include <linux/jump_label.h>
#include <linux/kallsyms.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include "policy/feature.h"
#include "include/ksu.h"

static DEFINE_STATIC_KEY_FALSE(fake_status_initialize_key);
static struct page *fake_status = NULL;
static DEFINE_MUTEX(fake_status_init_mutex);
static bool ksu_selinux_hide_status_enabled = true;

static void initialize_fake_status(void)
{
	if (READ_ONCE(fake_status))
		return;

	mutex_lock(&fake_status_init_mutex);
	if (fake_status) /* double-check after lock */
		goto out;

	struct page *real_page = selinux_kernel_status_page(&selinux_state);
	if (!real_page) {
		pr_warn("ksu_selinux_hide: status_page not exist\n");
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

static int my_sel_open_handle_status(struct inode *inode, struct file *filp)
{
	if (likely(current_uid().val >= 10000 &&
		   ksu_selinux_hide_status_enabled)) {
		struct page *data = READ_ONCE(fake_status);
		if (data) {
			filp->private_data = page_address(data);
			return 0;
		}
	}

	int ret = orig_sel_open_handle_status(inode, filp);
	if (static_branch_unlikely(&fake_status_initialize_key) && !ret &&
	    !fake_status) {
		initialize_fake_status();
	}
	return ret;
}

static void hook_selinux_status_open(void)
{
	if (orig_sel_open_handle_status)
		return;

	struct file_operations *ops =
		(struct file_operations *)kallsyms_lookup_name(
			"sel_handle_status_ops");
	if (!ops) {
		pr_err("ksu_selinux_hide: sel_handle_status_ops not found, fake status disabled\n");
		return;
	}

	orig_sel_open_handle_status = ops->open;
	ops->open = my_sel_open_handle_status;
	pr_info("ksu_selinux_hide: hooked sel_handle_status_ops->open\n");
}

static void unhook_selinux_status_open(void)
{
	if (!orig_sel_open_handle_status)
		return;

	struct file_operations *ops =
		(struct file_operations *)kallsyms_lookup_name(
			"sel_handle_status_ops");
	if (!ops) {
		pr_err("ksu_selinux_hide: sel_handle_status_ops not found on unhook\n");
		return;
	}

	ops->open = orig_sel_open_handle_status;
	orig_sel_open_handle_status = NULL;
	pr_info("ksu_selinux_hide: unhooked sel_handle_status_ops->open\n");
}

static int selinux_hide_status_feature_get(u64 *value)
{
	*value = ksu_selinux_hide_status_enabled ? 1 : 0;
	return 0;
}

static int selinux_hide_status_feature_set(u64 value)
{
	bool enable = !!value;
	if (enable == ksu_selinux_hide_status_enabled) {
		pr_info("ksu_selinux_hide: no need to change\n");
		return 0;
	}
	ksu_selinux_hide_status_enabled = enable;
	pr_info("ksu_selinux_hide: set to %d\n", enable);
	return 0;
}

static const struct ksu_feature_handler selinux_hide_status_handler = {
	.feature_id = KSU_FEATURE_SELINUX_HIDE_STATUS,
	.name = "selinux_hide_status",
	.get_handler = selinux_hide_status_feature_get,
	.set_handler = selinux_hide_status_feature_set,
};

void ksu_selinux_hide_status_handle_second_stage(void)
{
	initialize_fake_status();
	if (READ_ONCE(fake_status)) {
		static_key_disable(&fake_status_initialize_key.key);
	} else {
		pr_warn("ksu_selinux_hide: fake status needs late initialization\n");
	}
}

void ksu_selinux_hide_status_handle_post_fs_data(void)
{
	static_key_disable(&fake_status_initialize_key.key);
	if (!READ_ONCE(fake_status))
		pr_err("ksu_selinux_hide: fake status not initialized after post-fs-data!\n");
}

void __init ksu_selinux_hide_status_init(void)
{
	if (ksu_register_feature_handler(&selinux_hide_status_handler))
		pr_err("ksu_selinux_hide: failed to register feature handler\n");

	if (ksu_late_loaded) {
		initialize_fake_status();
	} else {
		static_key_enable(&fake_status_initialize_key.key);
	}
	hook_selinux_status_open();
}

void __exit ksu_selinux_hide_status_exit(void)
{
	ksu_unregister_feature_handler(KSU_FEATURE_SELINUX_HIDE_STATUS);
	unhook_selinux_status_open();
	mutex_lock(&fake_status_init_mutex);
	if (fake_status) {
		__free_page(fake_status);
		fake_status = NULL;
	}
	mutex_unlock(&fake_status_init_mutex);
}