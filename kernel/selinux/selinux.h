#ifndef __KSU_H_SELINUX
#define __KSU_H_SELINUX

#include "linux/types.h"
#include "linux/version.h"
#include "linux/cred.h"

#include "objsec.h"
#include "security.h" // Samsung SELinux Porting

#ifndef KSU_COMPAT_USE_SELINUX_STATE
#include "avc.h"
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

// TODO: rename to "ksu"
#define KERNEL_SU_DOMAIN "su"
#define KERNEL_SU_FILE "ksu_file"

#define KERNEL_SU_CONTEXT "u:r:" KERNEL_SU_DOMAIN ":s0"
#define KSU_FILE_CONTEXT "u:object_r:" KERNEL_SU_FILE ":s0"

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

#ifdef CONFIG_KSU_SUSFS
bool susfs_is_sid_equal(const struct cred *cred, u32 sid2);
u32 susfs_get_sid_from_name(const char *secctx_name);
u32 susfs_get_current_sid(void);
void susfs_set_zygote_sid(void);
bool susfs_is_current_zygote_domain(void);
void susfs_set_ksu_sid(void);
bool susfs_is_current_ksu_domain(void);
void susfs_set_init_sid(void);
bool susfs_is_current_init_domain(void);
void susfs_set_priv_app_sid(void);
#endif // #ifdef CONFIG_KSU_SUSFS

#endif
