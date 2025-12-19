#ifndef __KSU_H_SELINUX
#define __KSU_H_SELINUX

#include "linux/types.h"
#include "linux/version.h"
#include "linux/cred.h"

#include "objsec.h"
#ifdef SAMSUNG_SELINUX_PORTING
#include "security.h" // Samsung SELinux Porting
#endif
#ifndef KSU_COMPAT_USE_SELINUX_STATE
#include "avc.h"
#endif

static inline bool is_selinux_disabled(void)
{
#ifdef CONFIG_SECURITY_SELINUX_DISABLE
#ifdef KSU_COMPAT_USE_SELINUX_STATE
	return selinux_state.disabled;
#else
	return selinux_disabled;
#endif
#else
	return false;
#endif
}

static inline bool is_selinux_enforcing(void)
{
#ifdef CONFIG_SECURITY_SELINUX_DEVELOP
#ifdef KSU_COMPAT_USE_SELINUX_STATE
	return selinux_state.enforcing;
#elif defined(SAMSUNG_SELINUX_PORTING) || !defined(KSU_COMPAT_USE_SELINUX_STATE)
	return selinux_enforcing;
#endif
#else
	return true;
#endif
}

static inline void do_setenforce(bool val)
{
#ifdef CONFIG_SECURITY_SELINUX_DEVELOP
#ifdef KSU_COMPAT_USE_SELINUX_STATE
	selinux_state.enforcing = val;
#else
	selinux_enforcing = val;
#endif
#else
	/* do nothing */
#endif
}

#ifdef KSU_OPTIONAL_SELINUX_CRED
#define __selinux_cred(cred) (selinux_cred(cred))
#else
#define __selinux_cred(cred) (cred->security)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 18, 0)
typedef struct task_security_struct taskcred_sec_t;
#else
typedef struct cred_security_struct taskcred_sec_t;
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)) &&                         \
	!defined(KSU_COMPAT_HAS_CURRENT_SID)
/*
 * get the subjective security ID of the current task
 */
static inline u32 current_sid(void)
{
	const struct task_security_struct *tsec = current_security();

	return tsec->sid;
}
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 1, 0) &&                           \
     !defined(KSU_OPTIONAL_SELINUX_CRED))
static inline taskcred_sec_t *selinux_cred(const struct cred *cred)
{
	return (taskcred_sec_t *)cred->security;
}
#endif

void setup_selinux(const char *);

void setenforce(bool);

bool getenforce();

bool is_task_ksu_domain(const struct cred* cred);

bool is_ksu_domain();

bool is_zygote(const struct cred* cred);

bool is_init(const struct cred* cred);

void apply_kernelsu_rules();

u32 ksu_get_ksu_file_sid();

int handle_sepolicy(unsigned long arg3, void __user *arg4);

#endif