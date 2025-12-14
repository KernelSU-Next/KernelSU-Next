#ifndef __KSU_H_SELINUX
#define __KSU_H_SELINUX

#include "linux/types.h"
#include "linux/version.h"
#include "linux/cred.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 18, 0)
typedef struct task_security_struct taskcred_sec_t;
#else
typedef struct cred_security_struct taskcred_sec_t;
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