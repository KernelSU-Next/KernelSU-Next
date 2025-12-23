#ifndef __KSU_H_SELINUX
#define __KSU_H_SELINUX

#include "linux/types.h"
#include "linux/version.h"
#include "linux/cred.h"

#include "objsec.h"
#if defined(SAMSUNG_SELINUX_PORTING) || defined(KSU_COMPAT_HAS_SELINUX_STATE)
#include "security.h" // Samsung SELinux Porting
#endif
#ifndef KSU_COMPAT_HAS_SELINUX_STATE
#include "avc.h"
#endif

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

// TODO: rename to "ksu"
#define KERNEL_SU_DOMAIN "su"
#define KERNEL_SU_FILE "ksu_file"

#define KERNEL_SU_CONTEXT "u:r:" KERNEL_SU_DOMAIN ":s0"
#define KSU_FILE_CONTEXT "u:object_r:" KERNEL_SU_FILE ":s0"

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)) ||                        \
	defined(KSU_COMPAT_HAS_SELINUX_STATE)
#define KSU_COMPAT_USE_SELINUX_STATE
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

void setup_ksu_cred();

#endif
