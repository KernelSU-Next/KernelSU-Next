#include <linux/version.h>

#ifdef CONFIG_KSU_SUSFS

#include <linux/cred.h>
#include <linux/sched.h>
#include <linux/security.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
#include <linux/sched/task_stack.h>
#endif

#include "ksu_susfs.h"
#include "selinux/selinux.h"
#include "objsec.h"

/*
 * SUSFS kernel-side glue for the restructured legacy tree.
 *
 * The fs/ side of susfs (fs/susfs.c) expects a few symbols from the
 * KernelSU side. They used to live in the old monolithic core_hook.c /
 * selinux.c; they are collected here.
 */

// SIDs shared with the patched selinux avc audit code
u32 susfs_ksu_sid = 0;
u32 susfs_priv_app_sid = 0;

static u32 ksu_susfs_sid_from_name(const char *ctx)
{
	u32 sid = 0;
	int err;

	if (!ctx)
		return 0;

	err = security_secctx_to_secid(ctx, strlen(ctx), &sid);
	if (err) {
		pr_err("ksu_susfs: failed getting sid for '%s', err: %d\n",
		       ctx, err);
		return 0;
	}
	return sid;
}

void __init ksu_susfs_init_sids(void)
{
	susfs_ksu_sid = ksu_susfs_sid_from_name(KERNEL_SU_CONTEXT);
	susfs_priv_app_sid =
		ksu_susfs_sid_from_name("u:r:priv_app:s0:c512,c768");
	pr_info("ksu_susfs: ksu_sid=%u priv_app_sid=%u\n", susfs_ksu_sid,
		susfs_priv_app_sid);
}

bool susfs_is_current_ksu_domain(void)
{
	return is_ksu_domain();
}

bool susfs_is_current_zygote_domain(void)
{
	return is_zygote(current_cred());
}

// Provided for the devpts manual hook point in fs/devpts/inode.c.
// Legacy handles devpts through the LSM permission hook, so this only
// needs to exist as a symbol.
int ksu_handle_devpts(struct inode *inode)
{
	return 0;
}

#else // !CONFIG_KSU_SUSFS

bool susfs_is_current_ksu_domain(void)
{
	return false;
}

bool susfs_is_current_zygote_domain(void)
{
	return false;
}

int ksu_handle_devpts(struct inode *inode)
{
	return 0;
}

void __init ksu_susfs_init_sids(void)
{
}

#endif // CONFIG_KSU_SUSFS
