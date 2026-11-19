#include <linux/err.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/moduleparam.h>
#include <linux/namei.h>
#include <linux/sched.h>
#include <linux/shmem_fs.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#include "klog.h" // IWYU pragma: keep
#include "util.h"
#include "runtime/ksud.h"
#include "hook/syscall_hook.h"
#include "preinit/preinit.h"

#define INIT_PATH "/system/bin/init"

// The runner is a static ELF embedded into .init.rodata by preinit/blob.S.
extern const char ksu_preinit_runner_start[];
extern const char ksu_preinit_runner_end[];

static bool ksu_preinit_enabled = true;
module_param_named(preinit, ksu_preinit_enabled, bool, 0);

// Owned here until PID 1 executes it; the init section holding the blob is
// gone by then, so it lives in an unlinked shmem file.
static struct file *ksu_preinit_file;

void __init ksu_preinit_init(void)
{
#if !defined(CONFIG_KPROBES) && !defined(CONFIG_KSU_SUSFS)
    pr_info("preinit: unsupported under current hook provider\n");
    return;
#else
    size_t size = ksu_preinit_runner_end - ksu_preinit_runner_start;
    struct file *file;
    loff_t pos = 0;
    ssize_t written;

    if (!ksu_preinit_enabled) {
        pr_info("preinit: disabled\n");
        return;
    }
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 19, 0)
    file = shmem_file_setup("ksu_preinit", size, EMPTY_VMA_FLAGS);
#else
    file = shmem_file_setup("ksu_preinit", size, 0);
#endif
    if (IS_ERR(file)) {
        pr_err("preinit: shmem_file_setup failed: %ld\n", PTR_ERR(file));
        return;
    }

    written = kernel_write(file, ksu_preinit_runner_start, size, &pos);
    if (written != size) {
        pr_err("preinit: write runner failed: %zd\n", written);
        fput(file);
        return;
    }

    ksu_preinit_file = file;
    pr_info("preinit: runner staged (%zu bytes)\n", size);
#endif
}

bool ksu_preinit_enabled_param(void)
{
    return ksu_preinit_enabled;
}

void ksu_preinit_release(void)
{
    struct file *file = xchg(&ksu_preinit_file, NULL);

    if (file) {
        pr_info("preinit: selinux_setup not seen, dropping runner\n");
        fput(file);
    }
}

void ksu_preinit_exit(void)
{
    ksu_preinit_release();
}

#ifdef CONFIG_KSU_SUSFS
static bool is_selinux_setup_susfs(struct user_arg_ptr *argv_user)
{
    static const char expected[] = "selinux_setup";
    char buf[sizeof(expected) + 1] = {0};
    const char __user *argv_user_ptr;

    if (!argv_user)
        return false;

    argv_user_ptr = get_user_arg_ptr(*argv_user, 1);
    if (!argv_user_ptr || IS_ERR(argv_user_ptr)) {
        pr_err("!argv_user_ptr || IS_ERR(argv_user_ptr)\n");
        return false;
    }

    if (strncpy_from_user(buf, argv_user_ptr, sizeof(buf) - 1) < 0)
        return false;

    return !strcmp(buf, expected);
}

int ksu_setup_preinit_file(struct filename **filename_ptr, struct user_arg_ptr *argv_user)
{
    char fd_path[32] = {0};
    struct file *file;
    struct path path;
    int fd, len;

    if (likely(!READ_ONCE(ksu_preinit_file)))
        return -EINVAL;

    if (!filename_ptr || !*filename_ptr || IS_ERR(*filename_ptr) || !argv_user)
        return -EINVAL;

    if (strcmp((*filename_ptr)->name, INIT_PATH) || !is_selinux_setup_susfs(argv_user))
        return -EINVAL;

    file = xchg(&ksu_preinit_file, NULL);
    if (!file)
        return -EINVAL;

    fd = get_unused_fd_flags(O_CLOEXEC);
    if (fd < 0) {
        pr_err("preinit: get fd failed: %d\n", fd);
        fput(file);
        return -EINVAL;
    }
    // The fd owns the file from here on; close-on-exec frees it once the
    // runner has been loaded.
    fd_install(fd, file);

    len = snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", fd);
    if (kern_path(fd_path, LOOKUP_FOLLOW, &path)) {
        pr_err("preinit: %s not reachable, booting without preinit\n", fd_path);
        ksu_close_fd(fd);
        return -EINVAL;
    }
    path_put(&path);

    memset((void*)(*filename_ptr)->name, 0, strlen((*filename_ptr)->name));
    memcpy((void*)(*filename_ptr)->name, (void*)fd_path, strlen(fd_path));

    pr_info("preinit: exec runner via %s\n", fd_path);
    return fd;
}

int ksu_preinit_handle_post_execveat(int *fd, struct filename **filename_ptr,
                 void *argv_user, void *envp_user,
                 int *flags, int *retval, int *preinit_fd)
{
    if (!retval || !preinit_fd)
        return 0;

    if (*retval && *preinit_fd > 0) {
        ksu_close_fd(*preinit_fd);
        if (filename_ptr && *filename_ptr && !IS_ERR(*filename_ptr)) {
            memset((void*)(*filename_ptr)->name, 0, strlen((*filename_ptr)->name));
            memcpy((void*)(*filename_ptr)->name, INIT_PATH, strlen(INIT_PATH));
        }
        return -EINVAL;
    }
    return 0;
}

bool ksu_preinit_handle_execve(const char __user **filename_user, const char __user *const __user *argv_user,
                               int orig_nr, struct pt_regs *regs, long *ret)
{
    return false;
}
#else
#if defined(CONFIG_KPROBES)
static bool is_selinux_setup(const char __user *const __user *argv_user)
{
    static const char expected[] = "selinux_setup";
    const char __user *arg;
    char buf[sizeof(expected) + 1];

    if (!argv_user || get_user(arg, argv_user + 1) || !arg)
        return false;

    memset(buf, 0, sizeof(buf));
    if (strncpy_from_user(buf, arg, sizeof(buf) - 1) < 0)
        return false;

    return !strcmp(buf, expected);
}

bool ksu_preinit_handle_execve(const char __user **filename_user, const char __user *const __user *argv_user,
                               int orig_nr, struct pt_regs *regs, long *ret)
{
    const char __user *orig_filename;
    char path[sizeof(INIT_PATH) + 1];
    char fd_path[32];
    struct file *file;
    struct path p;
    int fd, len;

    if (likely(!READ_ONCE(ksu_preinit_file)))
        return false;

    if (!filename_user || !*filename_user)
        return false;

    memset(path, 0, sizeof(path));
    if (strncpy_from_user(path, (const char __user *)untagged_addr((unsigned long)*filename_user), sizeof(path) - 1) <
        0)
        return false;

    if (strcmp(path, INIT_PATH) || !is_selinux_setup(argv_user))
        return false;

    // One shot: the runner's own exec of the real init must pass through.
    file = xchg(&ksu_preinit_file, NULL);
    if (!file)
        return false;

    fd = get_unused_fd_flags(O_CLOEXEC);
    if (fd < 0) {
        pr_err("preinit: get fd failed: %d\n", fd);
        fput(file);
        return false;
    }
    // The fd owns the file from here on; close-on-exec frees it once the
    // runner has been loaded.
    fd_install(fd, file);

    len = snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", fd);
    if (kern_path(fd_path, LOOKUP_FOLLOW, &p)) {
        pr_err("preinit: %s not reachable, booting without preinit\n", fd_path);
        ksu_close_fd(fd);
        return false;
    }
    path_put(&p);

    orig_filename = *filename_user;
    *filename_user = userspace_stack_buffer(fd_path, len + 1);
    if (!*filename_user) {
        *filename_user = orig_filename;
        ksu_close_fd(fd);
        return false;
    }

    pr_info("preinit: exec runner via %s\n", fd_path);
    *ret = ksu_syscall_table[orig_nr](regs);
    if (*ret < 0) {
        pr_err("preinit: exec runner failed: %ld, booting without preinit\n", *ret);
        *filename_user = orig_filename;
        ksu_close_fd(fd);
        *ret = ksu_syscall_table[orig_nr](regs);
    }
    return true;
}
#else
bool ksu_preinit_handle_execve(const char __user **filename_user, const char __user *const __user *argv_user,
                               int orig_nr, struct pt_regs *regs, long *ret)
{
    return false;
}
#endif
#endif
