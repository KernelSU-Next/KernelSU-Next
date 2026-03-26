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

#include "uapi/supercall_internal.h"
#include "arch.h"
#include "klog.h" // IWYU pragma: keep
#include "manager/manager_identity.h"

#include "tiny_sulog.h"

uint32_t ksuver_override = 0;

struct ksu_install_fd_tw {
    struct callback_head cb;
    int __user *outp;
};

static int anon_ksu_release(struct inode *inode, struct file *filp)
{
    pr_info("ksu fd released\n");
    return 0;
}

static long anon_ksu_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    return ksu_supercall_handle_ioctl(cmd, (void __user *)arg);
}

static const struct file_operations anon_ksu_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = anon_ksu_ioctl,
    .compat_ioctl = anon_ksu_ioctl,
    .release = anon_ksu_release,
};

int ksu_install_fd(void)
{
    struct file *filp;
    int fd;

    fd = get_unused_fd_flags(O_CLOEXEC);
    if (fd < 0) {
        pr_err("ksu_install_fd: failed to get unused fd\n");
        return fd;
    }

    filp = anon_inode_getfile("[ksu_driver]", &anon_ksu_fops, NULL, O_RDWR | O_CLOEXEC);
    if (IS_ERR(filp)) {
        pr_err("ksu_install_fd: failed to create anon inode file\n");
        put_unused_fd(fd);
        return PTR_ERR(filp);
    }

    fd_install(fd, filp);
    pr_info("ksu fd installed: %d for pid %d\n", fd, current->pid);
    return fd;
}

static void ksu_install_fd_tw_func(struct callback_head *cb)
{
    struct ksu_install_fd_tw *tw = container_of(cb, struct ksu_install_fd_tw, cb);
    int fd = ksu_install_fd();

    pr_info("[%d] install ksu fd: %d\n", current->pid, fd);
    if (copy_to_user(tw->outp, &fd, sizeof(fd))) {
        pr_err("install ksu fd reply err\n");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
        close_fd(fd);
#else
        ksys_close(fd);
#endif
    }

    kfree(tw);
}

static int reboot_handler_pre(struct kprobe *p, struct pt_regs *regs)
{
    struct pt_regs *real_regs = PT_REAL_REGS(regs);
    int magic1 = (int)PT_REGS_PARM1(real_regs);
    int magic2 = (int)PT_REGS_PARM2(real_regs);
    unsigned int cmd = (unsigned int)PT_REGS_PARM3(real_regs);
    unsigned long arg4 = (unsigned long)PT_REGS_SYSCALL_PARM4(real_regs);
    unsigned long reply = (unsigned long)arg4;

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
    }

    if (magic2 == CHANGE_MANAGER_UID) {
        /* only root is allowed for this command */
        if (current_uid().val != 0)
            return 0;

        pr_info("sys_reboot: ksu_set_manager_appid to: %d\n", cmd);
        ksu_set_manager_appid(cmd);

        if (cmd == ksu_get_manager_appid()) {
            if (copy_to_user((void __user *)arg4, &reply, sizeof(reply)))
                pr_info("sys_reboot: reply fail\n");
        }

        return 0;
    }

    if (magic2 == GET_SULOG_DUMP_V2) {
        if (current_uid().val != 0)
            return 0;

        int ret = send_sulog_dump((void __user *)arg4);
        if (ret)
            return 0;

        if (copy_to_user((void __user *)arg4, &reply, sizeof(reply) ))
            return 0;
    }

    if (magic2 == CHANGE_KSUVER) {
        if (current_uid().val != 0)
            return 0;

        pr_info("sys_reboot: ksu_change_ksuver to: %d\n", cmd);
        ksuver_override = cmd;

        if (copy_to_user((void __user *)arg4, &reply, sizeof(reply) ))
            return 0;
    }

    // WARNING!!! triple ptr zone! ***
    // https://wiki.c2.com/?ThreeStarProgrammer
    if (magic2 == CHANGE_SPOOF_UNAME) {
        // only root is allowed for this command
        if (current_uid().val != 0)
            return 0;

        char release_buf[65];
        char version_buf[65];
        static char original_release_buf[65] = {0};
        static char original_version_buf[65] = {0};

        // basically void * void __user * void __user *arg
        void __user **ppptr = (void __user **)arg4;

        // user pointer storage
        // init this as zero so this works on 32-on-64 compat (LE)
        uint64_t u_pptr = 0;
        uint64_t u_ptr = 0;

        pr_info("sys_reboot: ppptr: 0x%lx \n", (uintptr_t)ppptr);

        // arg here is ***, pull out user-space ** via copy_from_user
        if (copy_from_user(&u_pptr, ppptr, sizeof(u_pptr)))
            return 0;

        pr_info("sys_reboot: u_pptr: 0x%lx \n", (uintptr_t)u_pptr);

        // now we got the __user **
        // we cannot dereference this as this is __user
        // we just do another copy_from_user to get it
        if (copy_from_user(&u_ptr, (void __user *)u_pptr, sizeof(u_ptr)))
            return 0;

        pr_info("sys_reboot: u_ptr: 0x%lx \n", (uintptr_t)u_ptr);

        // for release
        if (strncpy_from_user(release_buf, (char __user *)u_ptr, sizeof(release_buf)) < 0)
            return 0;
        release_buf[sizeof(release_buf) - 1] = '\0';

        // for version
        if (strncpy_from_user(version_buf, (char __user *)(u_ptr + strlen(release_buf) + 1), sizeof(version_buf)) < 0)
            return 0;
        version_buf[sizeof(version_buf) - 1] = '\0';

        if (original_release_buf[0] == '\0') {
            struct new_utsname *u_curr = utsname();
            // we save current version as the original before modifying
            strncpy(original_release_buf, u_curr->release, sizeof(original_release_buf));
            strncpy(original_version_buf, u_curr->version, sizeof(original_version_buf));
            pr_info("sys_reboot: original uname saved: %s %s\n", original_release_buf, original_version_buf);
        }

        // so user can reset
        if (!strcmp(release_buf, "default") || !strcmp(version_buf, "default") ) {
            memcpy(release_buf, original_release_buf, sizeof(release_buf));
            memcpy(version_buf, original_version_buf, sizeof(version_buf));
        }

        pr_info("sys_reboot: spoofing kernel to: %s - %s\n", release_buf, version_buf);

        struct new_utsname *u = utsname();

        down_write(&uts_sem);
        strncpy(u->release, release_buf, sizeof(u->release));
        strncpy(u->version, version_buf, sizeof(u->version));
        up_write(&uts_sem);

        // we write our confirmation on **
        if (copy_to_user((void __user *)arg4, &reply, sizeof(reply)))
            return 0;
    }

    return 0;
}

static struct kprobe reboot_kp = {
    .symbol_name = REBOOT_SYMBOL,
    .pre_handler = reboot_handler_pre,
};

void ksu_supercalls_init(void)
{
    int rc;

    ksu_supercall_dump_commands();

    rc = register_kprobe(&reboot_kp);
    if (rc) {
        pr_err("reboot kprobe failed: %d\n", rc);
    } else {
        pr_info("reboot kprobe registered successfully\n");
    }

    sulog_init_heap(); // grab heap memory
}

void ksu_supercalls_exit(void)
{
    unregister_kprobe(&reboot_kp);
    ksu_supercall_cleanup_state();
}
