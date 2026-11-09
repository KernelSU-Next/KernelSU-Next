// SPDX-License-Identifier: GPL-2.0
/*
 * susfs_abi.h - SUSFS kernel<->userspace ABI (supercall via reboot(2)).  Mirrors upstream
 * susfs_def.h + susfs.h layouts exactly, so the prebuilt ksu_susfs tool (and SukiSU's ksud
 * bindings) can drive this LKM unmodified; field order/types/sizes MUST match [repr(C)] there.
 * Wire protocol: syscall(SYS_reboot, 0xDEADBEEF, 0xFAFAFAFA, cmd_id, &mut payload); the
 * kernel writes payload.err back (0 = ok, else errno-style).
 */
#ifndef __SUSFS_ABI_H
#define __SUSFS_ABI_H

#include <linux/types.h>
#include <linux/utsname.h>	/* __NEW_UTS_LEN (struct st_susfs_uname) */

#define KSU_INSTALL_MAGIC1 0xDEADBEEF
#define SUSFS_MAGIC        0xFAFAFAFA

/* command IDs (shared with ksu_susfs / ksud; identical to upstream susfs_def.h, including the
 * ids upstream marks *deprecated* - defined for ABI completeness only, no handler wired,
 * exactly like upstream kernels, which no longer dispatch them either. */
#define CMD_SUSFS_ADD_SUS_PATH                  0x55550
#define CMD_SUSFS_SET_ANDROID_DATA_ROOT_PATH    0x55551 /* deprecated */
#define CMD_SUSFS_SET_SDCARD_ROOT_PATH          0x55552 /* deprecated */
#define CMD_SUSFS_ADD_SUS_PATH_LOOP             0x55553
#define CMD_SUSFS_ADD_SUS_MOUNT                 0x55560 /* deprecated */
#define CMD_SUSFS_HIDE_SUS_MNTS_FOR_NON_SU_PROCS 0x55561
#define CMD_SUSFS_UMOUNT_FOR_ZYGOTE_ISO_SERVICE 0x55562 /* deprecated */
#define CMD_SUSFS_ADD_SUS_KSTAT                 0x55570
#define CMD_SUSFS_UPDATE_SUS_KSTAT              0x55571
#define CMD_SUSFS_ADD_SUS_KSTAT_STATICALLY      0x55572
#define CMD_SUSFS_ADD_TRY_UMOUNT                0x55580 /* deprecated */
#define CMD_SUSFS_SET_UNAME                     0x55590
#define CMD_SUSFS_ENABLE_LOG                    0x555a0
#define CMD_SUSFS_SET_CMDLINE_OR_BOOTCONFIG     0x555b0
#define CMD_SUSFS_ADD_OPEN_REDIRECT             0x555c0
#define CMD_SUSFS_SHOW_VERSION                  0x555e1
#define CMD_SUSFS_SHOW_ENABLED_FEATURES         0x555e2
#define CMD_SUSFS_SHOW_VARIANT                  0x555e3
#define CMD_SUSFS_SHOW_SUS_SU_WORKING_MODE      0x555e4 /* deprecated */
#define CMD_SUSFS_IS_SUS_SU_READY               0x555f0 /* deprecated */
#define CMD_SUSFS_SUS_SU                        0x60000 /* deprecated */
#define CMD_SUSFS_ENABLE_AVC_LOG_SPOOFING       0x60010
#define CMD_SUSFS_ADD_SUS_MAP                   0x60020

/* 126 is a USERSPACE-side sentinel the kernel never produces: the ksu_susfs C tool pre-seeds payload.err with it and
 * prints "SUSFS operation not supported, please enable it in kernel" when the field is STILL 126 after the syscall,
 * i.e. when the kernel never wrote err back -
 *   ksu_susfs/jni/includes/susfs_defs.h:16   #define ERR_CMD_NOT_SUPPORTED 126
 *   ksu_susfs/jni/includes/susfs_defs.h:18   PRT_MSG_IF_CMD_NOT_SUPPORTED(err, cmd)
 *   ksu_susfs/jni/features/sus_map.c:51-53   info.err = ERR_CMD_NOT_SUPPORTED; syscall(...); PRT
 *   KernelSU/10_enable_susfs_for_ksu.patch:2925-2926  default: return -EINVAL, payload untouched
 * The kernel half of the contract is only "do not write err for a command you do not handle"; susfs_supercall.c mirrors
 * it (the kprobe does not hijack a command susfs_cmd_handled() rejects and writes nothing).  Userspace defines it only. */
#define ERR_CMD_NOT_SUPPORTED 126

#define SUSFS_MAX_LEN_PATHNAME                  256
#define SUSFS_FAKE_CMDLINE_OR_BOOTCONFIG_SIZE   8192
#define SUSFS_ENABLED_FEATURES_SIZE             8192
#define SUSFS_MAX_VERSION_BUFSIZE               16
#define SUSFS_MAX_VARIANT_BUFSIZE               16

#define SUSFS_VERSION_STR "v2.3.0"
#define SUSFS_VARIANT_STR "GKI"

/* uid_scheme values of struct st_susfs_open_redirect (upstream enum UID_SCHEME, declared in
 * susfs.h next to the structs) */
enum UID_SCHEME {
	UID_NON_APP_PROC = 0,
	UID_ROOT_PROC_EXCEPT_SU_PROC,
	UID_NON_SU_PROC,
	UID_UMOUNTED_APP_PROC,
	UID_UMOUNTED_PROC,
};

/* ---- payload structs (must match userspace #[repr(C)] layouts) ---- */

struct st_susfs_sus_path {
	char target_pathname[SUSFS_MAX_LEN_PATHNAME];
	int err;
};

struct st_susfs_sus_map {
	char target_pathname[SUSFS_MAX_LEN_PATHNAME];
	int err;
};

/* KSTAT_SPOOF_* bits of struct st_susfs_sus_kstat's `flags` field (upstream declares these in
 * susfs.h right above that struct).  Intentional deviation from upstream, correcting an earlier
 * note here that got this wrong:
 *   upstream KERNEL    kernel_patches/include/linux/susfs.h:71  #define ..._CTIME_TV_SEC (1 < 8) -> 1
 *   upstream USERSPACE ksu_susfs/jni/features/sus_kstat.c:25    #define ..._CTIME_TV_SEC (1 < 8) -> 1
 *   SukiSU ksud (Rust) userspace/ksud/src/susfs/abi/consts.rs:52 uses the intended (1 << 8) -> 256
 * i.e. the typo lives on BOTH upstream sides, so bit 8 is really an alias of bit 0 (INO) there and
 * upstream's ctime spoof never fires from either side.  We keep the corrected value so bit 8 really
 * does spoof ctime.tv_sec, which also matches what ksud sends; a caller that follows the typo simply
 * spoofs ino, so both sides stay self-consistent.  If upstream ever fixes the typo, the two converge. */
#define KSTAT_SPOOF_INO           (1 << 0)
#define KSTAT_SPOOF_DEV           (1 << 1)
#define KSTAT_SPOOF_NLINK         (1 << 2)
#define KSTAT_SPOOF_SIZE          (1 << 3)
#define KSTAT_SPOOF_ATIME_TV_SEC  (1 << 4)
#define KSTAT_SPOOF_ATIME_TV_NSEC (1 << 5)
#define KSTAT_SPOOF_MTIME_TV_SEC  (1 << 6)
#define KSTAT_SPOOF_MTIME_TV_NSEC (1 << 7)
#define KSTAT_SPOOF_CTIME_TV_SEC  (1 << 8)	/* upstream (both sides): (1 < 8) */
#define KSTAT_SPOOF_CTIME_TV_NSEC (1 << 9)
#define KSTAT_SPOOF_BLOCKS        (1 << 10)
#define KSTAT_SPOOF_BLKSIZE       (1 << 11)

struct st_susfs_sus_kstat {
	int is_statically;
	unsigned long target_ino;
	char target_pathname[SUSFS_MAX_LEN_PATHNAME];
	unsigned long spoofed_ino;
	unsigned long spoofed_dev;
	unsigned int spoofed_nlink;
	long long spoofed_size;
	long spoofed_atime_tv_sec;
	unsigned long spoofed_atime_tv_nsec;
	long spoofed_mtime_tv_sec;
	unsigned long spoofed_mtime_tv_nsec;
	long spoofed_ctime_tv_sec;
	unsigned long spoofed_ctime_tv_nsec;
	long long spoofed_blocks;
	long spoofed_blksize;
	int flags;
	int err;
};

struct st_susfs_uname {
	char release[__NEW_UTS_LEN+1];
	char version[__NEW_UTS_LEN+1];
	int err;
};

struct st_susfs_log {
	bool enabled;
	int err;
};

struct st_susfs_avc_log_spoofing {
	bool enabled;
	int err;
};

struct st_susfs_hide_sus_mnts_for_non_su_procs {
	bool enabled;
	int err;
};

struct st_susfs_open_redirect {
	char target_pathname[SUSFS_MAX_LEN_PATHNAME];
	char redirected_pathname[SUSFS_MAX_LEN_PATHNAME];
	int uid_scheme;
	int err;
};

struct st_susfs_spoof_cmdline_or_bootconfig {
	char fake_cmdline_or_bootconfig[SUSFS_FAKE_CMDLINE_OR_BOOTCONFIG_SIZE];
	int err;
};

struct st_susfs_version {
	char susfs_version[SUSFS_MAX_VERSION_BUFSIZE];
	int err;
};

struct st_susfs_variant {
	char susfs_variant[SUSFS_MAX_VARIANT_BUFSIZE];
	int err;
};

struct st_susfs_enabled_features {
	char enabled_features[SUSFS_ENABLED_FEATURES_SIZE];
	int err;
};

#endif
