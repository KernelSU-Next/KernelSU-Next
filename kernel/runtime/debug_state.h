#ifndef __KSU_H_DEBUG_STATE
#define __KSU_H_DEBUG_STATE

#include <linux/types.h>

/*
 * Driver diagnostics for locked-down ROMs.
 *
 * Everything here is compiled only with CONFIG_KSU_DEBUG and exists because
 * on a production ROM there is no way for an adb shell to see what the driver
 * decided:
 *
 *   - dmesg, /proc/kmsg and /sys/fs/pstore are denied to the shell uid;
 *   - /sys/module/kernelsu/parameters/* is labelled sysfs and is not readable
 *     by the shell either (not even unrelated module parameters are);
 *   - a /proc entry created by the driver inherits the creating task's
 *     SELinux label (kernel/init), which the shell may not read.
 *
 * So the driver keeps a small in-memory ring of events and hands it to
 * userspace through a path the shell opens itself:
 *
 *   1. ksu_dbg_log() records a line in the ring (safe from any context, it
 *      only takes a spinlock and formats into a fixed slot);
 *   2. the next time a uid 2000 (shell) process calls execve - i.e. the next
 *      adb command - ksu_dbg_try_flush() dumps the ring to
 *      /data/local/tmp/ksu_debug.log using that process's own credentials.
 *
 * Writing with the shell's credentials is the whole trick: the file lands in
 * shell_data_file, which the shell is allowed to create and read, so SELinux
 * never has to be relaxed or bypassed.
 *
 * Usage, from a host:
 *
 *   adb shell true                              # trigger the flush
 *   adb shell cat /data/local/tmp/ksu_debug.log
 *
 * The dump starts with a one-line summary (manager appid, whether it is valid,
 * boot_completed, ksud presence, whether the driver's SELinux rules were
 * applied) followed by the recorded events, oldest first.
 */

/*
 * Record one event. The format string is limited to a single line; long
 * output is truncated to fit the slot.
 */
#ifdef CONFIG_KSU_DEBUG

void ksu_dbg_log(const char *fmt, ...);

/*
 * Dump the ring to /data/local/tmp/ksu_debug.log if there is anything
 * pending and the caller is the shell. Cheap enough for the execve path:
 * it returns immediately for every other uid and when nothing is pending.
 */
void ksu_dbg_try_flush(void);

/*
 * True for the first few manager scans. Scans repeat every few seconds while
 * the manager is not crowned yet, and their detail would otherwise overwrite
 * the interesting part of the ring.
 */
bool ksu_dbg_scan_verbose(void);

void ksu_debug_state_init(void);
void ksu_debug_state_exit(void);

/* Set by apply_kernelsu_rules() so the dump can show whether it ever ran. */
extern int ksu_dbg_rules_applied;

#else

static inline void ksu_dbg_log(const char *fmt, ...)
{
}

static inline void ksu_dbg_try_flush(void)
{
}

static inline bool ksu_dbg_scan_verbose(void)
{
	return false;
}

static inline void ksu_debug_state_init(void)
{
}

static inline void ksu_debug_state_exit(void)
{
}

#endif /* CONFIG_KSU_DEBUG */

#endif
