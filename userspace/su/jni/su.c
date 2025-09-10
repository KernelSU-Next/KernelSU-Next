#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/ioctl.h>
#include <sys/xattr.h>
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <termios.h>

#define KERNEL_SU_OPTION 0xDEADBEEF
#define CMD_GRANT_ROOT 0
#define CMD_ENABLE_SU 15

int main(int argc, char **argv, char **envp) {
    unsigned long result = 0;

    if (argc >= 2 && strcmp(argv[1], "--disable-sucompat") == 0) {
        prctl(KERNEL_SU_OPTION, CMD_ENABLE_SU, 0L, 0L, (unsigned long)&result);
        return 0;
    }

    prctl(KERNEL_SU_OPTION, CMD_GRANT_ROOT, 0L, 0L, (unsigned long)&result);
    if (result != KERNEL_SU_OPTION) {
        const char *error = "Access Denied: sucompat not permitted\n";
        write(STDERR_FILENO, error, strlen(error));
        return 1;
    }

    struct termios term;
    if (ioctl(STDIN_FILENO, TCGETS, &term) == 0) {
        char tty_path[PATH_MAX];
        ssize_t len = readlink("/proc/self/fd/0", tty_path, sizeof(tty_path) - 1);
        if (len > 0) {
            tty_path[len] = '\0';
            const char *selinux_ctx = "u:object_r:devpts:s0";
            setxattr(tty_path, "security.selinux", selinux_ctx, strlen(selinux_ctx) + 1, 0);
        }
    }

    const char *default_args[] = { "/system/bin/su", NULL };
    if (argc < 1 || !argv) {
        argv = (char **)default_args;
    } else {
        argv[0] = "/system/bin/su";
    }

    execve("/data/adb/ksud", argv, envp);

    const char *error = "Error: Failed to execve /data/adb/ksud\n";
    write(STDERR_FILENO, error, strlen(error));
    return 1;
}
