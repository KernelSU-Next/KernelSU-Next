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

struct file *ksu_filp_open_compat(const char *filename, int flags, umode_t mode)
{
	return filp_open(filename, flags, mode);
}

ssize_t ksu_kernel_read_compat(struct file *p, void *buf, size_t count,
			       loff_t *pos)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0) ||                          \
	defined(KSU_OPTIONAL_KERNEL_READ)
	return kernel_read(p, buf, count, pos);
#else
	loff_t offset = pos ? *pos : 0;
	ssize_t result = kernel_read(p, offset, (char *)buf, count);
	if (pos && result > 0) {
		*pos = offset + result;
	}
	return result;
#endif
}

ssize_t ksu_kernel_write_compat(struct file *p, const void *buf, size_t count,
				loff_t *pos)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0) ||                          \
	defined(KSU_OPTIONAL_KERNEL_WRITE)
	return kernel_write(p, buf, count, pos);
#else
	loff_t offset = pos ? *pos : 0;
	ssize_t result = kernel_write(p, buf, count, offset);
	if (pos && result > 0) {
		*pos = offset + result;
	}
	return result;
#endif
}


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

	char *realpath = d_path(path, buf, sizeof(buf));
	if (IS_ERR(realpath)) {
		pr_err("ksu_mount: d_path failed, err: %lu\n",
		       PTR_ERR(realpath));
		return PTR_ERR(realpath);
	}

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
long strncpy_from_user_nofault(char *dst, const void __user *unsafe_addr,
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

static void *__kvmalloc(size_t size, gfp_t flags)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0)
// https://elixir.bootlin.com/linux/v4.4.302/source/security/apparmor/lib.c#L79
	void *buffer = NULL;

	if (size == 0)
		return NULL;

	/* do not attempt kmalloc if we need more than 16 pages at once */
	if (size <= (16 * PAGE_SIZE))
		buffer = kmalloc(size, flags | GFP_NOIO | __GFP_NOWARN);
	if (!buffer) {
		if (flags & __GFP_ZERO)
			buffer = vzalloc(size);
		else
			buffer = vmalloc(size);
	}
	return buffer;
#else
	return kvmalloc(size, flags);
#endif
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
// https://elixir.bootlin.com/linux/v5.10.247/source/mm/util.c#L664
void *ksu_compat_kvrealloc(const void *p, size_t oldsize, size_t newsize,
			   gfp_t flags)
{
	void *newp;

	if (oldsize >= newsize)
		return (void *)p;
	newp = __kvmalloc(newsize, flags);
	if (!newp)
		return NULL;
	memcpy(newp, p, oldsize);
	kvfree(p);
	return newp;
}
#endif
