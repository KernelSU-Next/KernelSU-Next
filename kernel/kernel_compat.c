#include <linux/version.h>
#include <linux/fs.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
#include <linux/sched/task.h>
#else
#include <linux/sched.h>
#endif
#include <linux/uaccess.h>
#include <linux/fdtable.h>
#include "klog.h" // IWYU pragma: keep
#include "kernel_compat.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0) ||                           \
	defined(CONFIG_IS_HW_HISI)
#include <linux/key.h>
#include <linux/errno.h>
#include <linux/cred.h>
#include <linux/lsm_hooks.h>

extern int install_session_keyring_to_cred(struct cred *, struct key *);
struct key *init_session_keyring = NULL;

static int install_session_keyring(struct key *keyring)
{
	struct cred *new;
	int ret;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;

	ret = install_session_keyring_to_cred(new, keyring);
	if (ret < 0) {
		abort_creds(new);
		return ret;
	}

	return commit_creds(new);
}
#endif


#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0) && !defined(KSU_HAS_PATH_MOUNT))
extern long do_mount(const char *dev_name, const char __user *dir_name,
		     const char *type_page, unsigned long flags,
		     void *data_page);

int path_mount(const char *dev_name, struct path *path, const char *type_page,
	       unsigned long flags, void *data_page)
{
	mm_segment_t old_fs;
	long ret = 0;
	char buf[384];

	char *realpath = d_path(path, buf, 384);
	if (IS_ERR(realpath)) {
		pr_err("ksu_mount: d_path failed, err: %lu\n",
		       PTR_ERR(realpath));
		return PTR_ERR(realpath);
	}

	// https://github.com/backslashxx/KernelSU/blob/e02c2771b106c68f0b8a17234b5b1846664852f0/kernel/kernel_compat.c#L123
	// This check is handy.
	if (!(realpath && realpath != buf))
		return -ENOENT;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = do_mount(dev_name, (const char __user *)realpath, type_page,
		       flags, data_page);
	set_fs(old_fs);
	return ret;
}
#endif

long ksu_copy_from_user_nofault(void *dst, const void __user *src, size_t size)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
    return copy_from_user_nofault(dst, src, size);
#else
    // https://elixir.bootlin.com/linux/v5.8/source/mm/maccess.c#L205
    long ret = -EFAULT;
    mm_segment_t old_fs = get_fs();

    set_fs(USER_DS);
    // tweaked to use ksu_access_ok
    if (ksu_access_ok(src, size)) {
        pagefault_disable();
        ret = __copy_from_user_inatomic(dst, src, size);
        pagefault_enable();
    }
    set_fs(old_fs);

    if (ret)
        return -EFAULT;
    return 0;
#endif
}

#ifndef KSU_OPTIONAL_STRNCPY
inline long
strncpy_from_user_nofault(char *dst, const void __user *unsafe_addr,
				   long count)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
	return strncpy_from_unsafe_user(dst, unsafe_addr, count);
#else
	mm_segment_t old_fs = get_fs();
	long ret;

	if (unlikely(count <= 0))
		return 0;

	set_fs(USER_DS);
	pagefault_disable();
	ret = strncpy_from_user(dst, unsafe_addr, count);
	pagefault_enable();
	set_fs(old_fs);

	if (ret >= count) {
		ret = count;
		dst[ret - 1] = '\0';
	} else if (ret > 0) {
		ret++;
	}

	return ret;
#endif
}
#endif // #ifndef KSU_OPTIONAL_STRNCPY