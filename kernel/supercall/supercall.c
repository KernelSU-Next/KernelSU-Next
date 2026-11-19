#include <linux/anon_inodes.h>
#include <linux/err.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kprobes.h>
#include <linux/pid.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/task_work.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/utsname.h> // utsname() and uts_sem

#include "uapi/supercall.h"
#include "supercall/internal.h"
#include "arch.h"
#include "util.h"
#include "klog.h" // IWYU pragma: keep
#include "manager/manager_identity.h"

#include "sulog/event.h"

#include "linux/jump_label.h"

#if defined(CONFIG_KSU_SUSFS) && defined(CONFIG_KSU_SUSFS_SPOOF_UNAME)
extern struct static_key_false susfs_is_uname_spoof_buffer_set;
#endif
uint32_t ksuver_override = 0;

#define KSU_DRIVER_PERMISSION_SU_SESSION (1UL << 0)

struct ksu_driver_context {
    unsigned long permissions;
};

struct ksu_install_fd_tw {
    struct callback_head cb;
    int __user *outp;
};

static int anon_ksu_release(struct inode *inode, struct file *filp)
{
    kfree(filp->private_data);
    pr_info("ksu fd released\n");
    return 0;
}

static long anon_ksu_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    return ksu_supercall_handle_ioctl(filp, cmd, (void __user *)arg);
}

static const struct file_operations anon_ksu_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = anon_ksu_ioctl,
    .compat_ioctl = anon_ksu_ioctl,
    .release = anon_ksu_release,
};

static int ksu_install_fd_with_permissions(unsigned int fd_flags, unsigned long permissions)
{
    struct ksu_driver_context *context;
    struct file *filp;
    const char *name;
    int fd;

    context = kzalloc(sizeof(*context), GFP_KERNEL);
    if (!context)
        return -ENOMEM;

    context->permissions = permissions;
    name = permissions & KSU_DRIVER_PERMISSION_SU_SESSION ? "[ksu_driver_su]" : "[ksu_driver]";

    fd = get_unused_fd_flags(fd_flags);
    if (fd < 0) {
        pr_err("ksu_install_fd: failed to get unused fd for %s\n", name);
        kfree(context);
        return fd;
    }

    filp = anon_inode_getfile(name, &anon_ksu_fops, context, O_RDWR);
    if (IS_ERR(filp)) {
        pr_err("ksu_install_fd: failed to create anon inode file for %s\n", name);
        put_unused_fd(fd);
        kfree(context);
        return PTR_ERR(filp);
    }

    fd_install(fd, filp);
    pr_info("%s fd installed: %d for pid %d\n", name, fd, current->pid);
    return fd;
}

int ksu_install_fd(void)
{
    return ksu_install_fd_with_permissions(O_CLOEXEC, 0);
}

int ksu_install_su_fd(void)
{
    // This descriptor must be installed after the exec into ksud.
    return ksu_install_fd_with_permissions(O_CLOEXEC, KSU_DRIVER_PERMISSION_SU_SESSION);
}

bool ksu_is_su_session_fd(const struct file *filp)
{
    const struct ksu_driver_context *context = filp->private_data;

    return context && (context->permissions & KSU_DRIVER_PERMISSION_SU_SESSION);
}

static void ksu_install_fd_tw_func(struct callback_head *cb)
{
    struct ksu_install_fd_tw *tw = container_of(cb, struct ksu_install_fd_tw, cb);
    int fd = ksu_install_fd();

    pr_info("[%d] install ksu fd: %d\n", current->pid, fd);
    if (copy_to_user(tw->outp, &fd, sizeof(fd))) {
        pr_err("install ksu fd reply err\n");
        ksu_close_fd(fd);
    }

    kfree(tw);
}

static char toolkit_orig_release[65] = {0};
static char toolkit_orig_version[65] = {0};

void ksu_toolkit_uname_reset(void)
{
    if (toolkit_orig_release[0] != '\0') {
        struct new_utsname *u = utsname();
        pr_info("ksu: resetting toolkit uname memory to stock\n");
        down_write(&uts_sem);
        strscpy(u->release, toolkit_orig_release, sizeof(u->release));
        strscpy(u->version, toolkit_orig_version, sizeof(u->version));
        up_write(&uts_sem);
    }
}

int ksu_handle_toolkit_reboot(int magic2, unsigned int cmd, void __user *arg)
{
    unsigned long reply = (unsigned long)arg;

    if (magic2 == CHANGE_MANAGER_UID) {
        /* only root is allowed for this command */
        if (current_uid().val != 0)
            return -EPERM;
        ksu_set_manager_appid(cmd);
        if (cmd == ksu_get_manager_appid()) {
            if (copy_to_user(arg, &reply, sizeof(reply)))
                return -EFAULT;
        }
        return 0;
    }

    if (magic2 == GET_SULOG_DUMP_V2) {
        /* only root is allowed for this command */
        if (current_uid().val != 0)
            return -EPERM;

        int ret = ksu_sulog_handle_compat_dump(arg);
        if (ret)
            return ret;

        if (copy_to_user(arg, &reply, sizeof(reply)))
            return -EFAULT;
        return 0;
    }

    if (magic2 == CHANGE_KSUVER) {
        /* only root is allowed for this command */
        if (current_uid().val != 0)
            return -EPERM;
        ksuver_override = cmd;
        if (copy_to_user(arg, &reply, sizeof(reply)))
            return -EFAULT;
        return 0;
    }

    // WARNING!!! triple ptr zone! ***
    // https://wiki.c2.com/?ThreeStarProgrammer
    if (magic2 == CHANGE_SPOOF_UNAME) {
        /* only root is allowed for this command */
        if (current_uid().val != 0)
            return -EPERM;

        char release_buf[65];
        char version_buf[65];

        // basically void * void __user * void __user *arg
        void __user **ppptr = (void __user **)arg;
        uint64_t u_pptr = 0;
        uint64_t u_ptr = 0;

        // arg here is ***, pull out user-space ** via copy_from_user
        if (copy_from_user(&u_pptr, ppptr, sizeof(u_pptr)))
            return -EFAULT;

        // now we got the __user **
        // we cannot dereference this as this is __user
        // we just do another copy_from_user to get it
        if (copy_from_user(&u_ptr, (void __user *)u_pptr, sizeof(u_ptr)))
            return -EFAULT;

        // for release
        if (strncpy_from_user(release_buf, (char __user *)u_ptr, sizeof(release_buf)) < 0)
            return -EFAULT;
        release_buf[sizeof(release_buf) - 1] = '\0';

        // for version
        if (strncpy_from_user(version_buf, (char __user *)(u_ptr + strlen(release_buf) + 1), sizeof(version_buf)) < 0)
            return -EFAULT;
        version_buf[sizeof(version_buf) - 1] = '\0';

#if defined(CONFIG_KSU_SUSFS) && defined(CONFIG_KSU_SUSFS_SPOOF_UNAME)
        if (static_branch_unlikely(&susfs_is_uname_spoof_buffer_set) && (strcmp(release_buf, "default") || strcmp(version_buf, "default"))) {
            pr_info("susfs: SuSFS uname active, blocking toolkit apply\n");
            return -EBUSY;
        }
#endif

        if (toolkit_orig_release[0] == '\0') {
            struct new_utsname *u_curr = utsname();
            // we save current version as the original before modifying
            strscpy(toolkit_orig_release, u_curr->release, sizeof(toolkit_orig_release));
            strscpy(toolkit_orig_version, u_curr->version, sizeof(toolkit_orig_version));
        }

        // so user can reset
        if (!strcmp(release_buf, "default") || !strcmp(version_buf, "default")) {
            memcpy(release_buf, toolkit_orig_release, sizeof(release_buf));
            memcpy(version_buf, toolkit_orig_version, sizeof(version_buf));
        }

        struct new_utsname *u = utsname();
        down_write(&uts_sem);
        strscpy(u->release, release_buf, sizeof(u->release));
        strscpy(u->version, version_buf, sizeof(u->version));
        up_write(&uts_sem);

        // we write our confirmation on **
        if (copy_to_user(arg, &reply, sizeof(reply)))
            return -EFAULT;
        return 0;
    }

    return -EINVAL;
}

#ifdef CONFIG_KSU_SUSFS
int ksu_supercall_reboot_handler(void __user **arg)
{
    struct ksu_install_fd_tw *tw;

    tw = kzalloc(sizeof(*tw), GFP_KERNEL);
    if (!tw)
        return 0;

    tw->outp = (int __user *)(*arg);
    tw->cb.func = ksu_install_fd_tw_func;

    if (task_work_add(current, &tw->cb, TWA_RESUME)) {
        kfree(tw);
        pr_warn("install fd add task_work failed\n");
    }

    return 0;
}
#endif

#ifndef CONFIG_KSU_SUSFS
static int reboot_handler_pre(struct kprobe *p, struct pt_regs *regs)
{
    struct pt_regs *real_regs = PT_REAL_REGS(regs);
    int magic1 = (int)PT_REGS_SYSCALL_PARM1(real_regs);
    int magic2 = (int)PT_REGS_PARM2(real_regs);
    unsigned int cmd = (unsigned int)PT_REGS_PARM3(real_regs);
    unsigned long arg4 = (unsigned long)PT_REGS_SYSCALL_PARM4(real_regs);

    /* Check if this is a request to install KSU fd */
    if (magic1 == KSU_INSTALL_MAGIC1 && magic2 == KSU_INSTALL_MAGIC2) {
        struct ksu_install_fd_tw *tw;

        tw = kzalloc(sizeof(*tw), GFP_ATOMIC);
        if (!tw)
            return 0;

        tw->outp = (int __user *)arg4;
        tw->cb.func = ksu_install_fd_tw_func;

        if (task_work_add(current, &tw->cb, TWA_RESUME)) {
            kfree(tw);
            pr_warn("install fd add task_work failed\n");
        }
        return 0;
    }

    // Handle all other toolkit magics via unified handler
    (void)ksu_handle_toolkit_reboot(magic2, cmd, (void __user *)arg4);

    return 0;
}

static struct kprobe reboot_kp = {
    .symbol_name = REBOOT_SYMBOL,
    .pre_handler = reboot_handler_pre,
};
#endif

void __init ksu_supercalls_init(void)
{
#ifndef CONFIG_KSU_SUSFS
    int rc;
#endif

    ksu_supercall_dump_commands();

#ifndef CONFIG_KSU_SUSFS
    rc = register_kprobe(&reboot_kp);
    if (rc) {
        pr_err("reboot kprobe failed: %d\n", rc);
    } else {
        pr_info("reboot kprobe registered successfully\n");
    }
#endif
}

void __exit ksu_supercalls_exit(void)
{
#ifndef CONFIG_KSU_SUSFS
    unregister_kprobe(&reboot_kp);
#endif
    ksu_supercall_cleanup_state();
}
