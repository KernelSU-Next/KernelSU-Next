/*
 * Driver diagnostics for locked-down ROMs. Only built with CONFIG_KSU_DEBUG.
 *
 * Why this exists
 * ---------------
 * On a production ROM an adb shell cannot observe the driver at all:
 *
 *   - dmesg, /proc/kmsg and /sys/fs/pstore are denied to the shell uid;
 *   - /sys/module/kernelsu/parameters/* carries the generic sysfs label and
 *     is not readable by the shell (unrelated module parameters are not
 *     either, so this is not specific to the driver);
 *   - a /proc entry created by the driver inherits the SELinux label of the
 *     task that created it, i.e. kernel/init, which the shell may not read.
 *
 * So instead of a kernel log, the driver keeps a ring of events and hands it
 * to userspace through a file the shell creates itself:
 *
 *   ksu_dbg_log()          any context, any IRQ state: formats one line into
 *                          a fixed ring slot under a spinlock;
 *   ksu_dbg_try_flush()    called from the execve path. When the caller is
 *                          the shell (uid 2000), it copies the ring into a
 *                          heap buffer under the same spinlock and writes it
 *                          out *outside* the lock. Every shell command thus
 *                          refreshes the snapshot (rate limited), so the file
 *                          always reflects the current state rather than
 *                          whatever a previous dump happened to contain.
 *
 * The write is done with the shell's own credentials, so the file lands in
 * shell_data_file and SELinux allows it without any rule being relaxed. The
 * kernel never needs to write to /data on its own behalf.
 *
 * Reading the log
 * ---------------
 *   adb shell true                              # any command triggers the dump
 *   adb shell cat /data/local/tmp/ksu_debug.log
 *
 * The first line is a state summary, then the recorded events, oldest first.
 * A reboot clears the ring; the file keeps the previous boot's contents until
 * the next flush overwrites it (the dump truncates).
 */

#include <linux/atomic.h>
#include <linux/fs.h>
#include <linux/jiffies.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uidgid.h>
#include <linux/version.h>
#include <linux/vmalloc.h>

#include "compat/kernel_compat.h"
#include "klog.h" // IWYU pragma: keep
#include "ksu.h"
#include "manager/manager_identity.h"
#include "runtime/debug_state.h"
#include "runtime/ksud_boot.h"

#ifdef CONFIG_KSU_DEBUG

/* Ring is a few hundred KiB at most, sized for a couple of boot-time scans. */
#define KSU_DBG_LINES 256
#define KSU_DBG_LINE_LEN 128
#define KSU_DBG_BUF_LEN (KSU_DBG_LINES * (KSU_DBG_LINE_LEN + 1) + 256)

#define KSU_DBG_PATH "/data/local/tmp/ksu_debug.log"

/* adb shell runs as uid 2000 and may create files in /data/local/tmp. */
#define KSU_DBG_SHELL_UID 2000

/* Only the first few scans are recorded in detail, see ksu_dbg_scan_verbose(). */
#define KSU_DBG_VERBOSE_SCANS 3

#ifdef KSU_KPROBES_HOOK
#define KSU_DBG_HOOK_MODE "kprobes"
#else
#define KSU_DBG_HOOK_MODE "manual"
#endif

static char ksu_dbg_ring[KSU_DBG_LINES][KSU_DBG_LINE_LEN];
static unsigned int ksu_dbg_head;
static DEFINE_SPINLOCK(ksu_dbg_lock);

/* Serialises dumpers; a concurrent shell execve just skips this round. */
static atomic_t ksu_dbg_flushing = ATOMIC_INIT(0);

/* Rate limit, so a burst of shell commands does not rewrite the file each time. */
static unsigned long ksu_dbg_last_dump;

#define KSU_DBG_DUMP_INTERVAL (1 * HZ)

/* Set by apply_kernelsu_rules(); exported through the header. */
int ksu_dbg_rules_applied;

void ksu_dbg_log(const char *fmt, ...)
{
	unsigned long flags;
	va_list args;
	char *slot;

	spin_lock_irqsave(&ksu_dbg_lock, flags);
	slot = ksu_dbg_ring[ksu_dbg_head % KSU_DBG_LINES];
	va_start(args, fmt);
	vsnprintf(slot, KSU_DBG_LINE_LEN, fmt, args);
	va_end(args);
	ksu_dbg_head++;
	spin_unlock_irqrestore(&ksu_dbg_lock, flags);
}

/* Append to the dump buffer, never running past its end. */
static size_t ksu_dbg_append(char *buf, size_t len, const char *fmt, ...)
{
	va_list args;
	int ret;

	if (len + 1 >= KSU_DBG_BUF_LEN)
		return len;

	va_start(args, fmt);
	ret = vsnprintf(buf + len, KSU_DBG_BUF_LEN - len, fmt, args);
	va_end(args);

	if (ret < 0)
		return len;
	if ((size_t)ret >= KSU_DBG_BUF_LEN - len)
		return KSU_DBG_BUF_LEN - 1;

	return len + ret;
}

void ksu_dbg_try_flush(void)
{
	struct file *fp;
	unsigned long flags;
	unsigned int head, count, i;
	size_t len = 0;
	loff_t pos = 0;
	char *buf;

	if (current_uid().val != KSU_DBG_SHELL_UID)
		return;

	/* Keep the snapshot recent, without rewriting it for every command. */
	if (ksu_dbg_last_dump &&
	    !time_after(jiffies, ksu_dbg_last_dump + KSU_DBG_DUMP_INTERVAL))
		return;

	if (atomic_cmpxchg(&ksu_dbg_flushing, 0, 1) != 0)
		return;

	spin_lock_irqsave(&ksu_dbg_lock, flags);
	head = ksu_dbg_head;
	count = head > KSU_DBG_LINES ? KSU_DBG_LINES : head;
	spin_unlock_irqrestore(&ksu_dbg_lock, flags);

	buf = vzalloc(KSU_DBG_BUF_LEN);
	if (!buf)
		goto out_done;

	spin_lock_irqsave(&ksu_dbg_lock, flags);
	len = ksu_dbg_append(buf, len,
			     "manager_appid=%d valid=%d boot_completed=%d ksud=%d rules_applied=%d hook=%s\n",
			     ksu_get_manager_appid(),
			     ksu_is_manager_appid_valid() ? 1 : 0,
			     ksu_boot_completed ? 1 : 0,
			     ksu_ksud_present ? 1 : 0, ksu_dbg_rules_applied,
			     KSU_DBG_HOOK_MODE);
	for (i = head - count; i < head; i++)
		len = ksu_dbg_append(buf, len, "%s\n",
				     ksu_dbg_ring[i % KSU_DBG_LINES]);
	spin_unlock_irqrestore(&ksu_dbg_lock, flags);

	/*
	 * Opening and truncating with the shell's credentials is what makes the
	 * SELinux check pass; the driver must not bypass or relax the policy.
	 */
	fp = filp_open(KSU_DBG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(fp))
		goto out_free;

	if (ksu_kernel_write_compat(fp, buf, len, &pos) < 0)
		pr_err("ksu_debug: writing " KSU_DBG_PATH " failed\n");
	else
		ksu_dbg_last_dump = jiffies;

	filp_close(fp, NULL);

out_free:
	vfree(buf);
out_done:
	atomic_set(&ksu_dbg_flushing, 0);
}

bool ksu_dbg_scan_verbose(void)
{
	static atomic_t scans = ATOMIC_INIT(0);

	return atomic_inc_return(&scans) <= KSU_DBG_VERBOSE_SCANS;
}

void __init ksu_debug_state_init(void)
{
	ksu_dbg_log("driver init, version %u", KERNEL_SU_VERSION);
}

void __exit ksu_debug_state_exit(void)
{
}

#endif /* CONFIG_KSU_DEBUG */
