/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUSFS_DIRENT_H
#define __SUSFS_DIRENT_H

#include <linux/types.h>
#include <linux/compiler_types.h>
struct pt_regs;

/* The two AArch32 listing numbers, and they live here rather than in sus_path.c because the
 * sys_call_table entries that drive the filter (susfs_kstat.c) have to name them as well: a
 * second copy of "141" in another file is a number nobody would notice drifting.  Native
 * getdents64 is 61 in the native table; AArch32 numbers it 217, and 141 is AArch32's own
 * getdents - which is why the number alone never identifies the call and the ABI is always
 * read with it. */
#define SUS_NR_GETDENTS64_COMPAT 217
#define SUS_NR_GETDENTS_COMPAT   141

/* The listing filter: which layout a syscall number denotes for a task running the given ABI
 * (-1 when it is not a listing call), and the rewrite itself.  There is no "armed" flag any
 * more: the filter self-gates on no_extra, on the syscall's own return value and on whether any
 * rule exists, so what the caller has to get right is the arguments, not the state.  The stat
 * family in susfs_kstat.c has its own gate of the same shape; it does not call these. */
int sus_path_dirent_layout_id(long syscall_nr, bool compat);

long sus_path_dirent_filter(int lay_id, unsigned long buf, long ret);
/* Pre-emission dirent filter (sus_dirent.c): a hidden name is never written to the caller. */
bool sus_path_dirent_hidden(u64 ino, const char *name);
__nocfi bool susfs_dirent_prefilter(const struct pt_regs *regs, long *out,
					bool compat);
int susfs_sus_dirent_init(void);
void susfs_sus_dirent_exit(void);

/* One line of per-ABI dirent counters for /proc/susfs_kstat: "the callback ran and filtered
 * nothing" has to be distinguishable from "the callback never ran", and the same line names the
 * ABI each count belongs to.  Returns the length written, 0 when the buffer is too small. */
int sus_path_dirent_stat_line(char *buf, size_t size);

#endif /* __SUSFS_DIRENT_H */
