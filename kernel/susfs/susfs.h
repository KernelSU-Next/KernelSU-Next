/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUSFS_H
#define __SUSFS_H

#include <linux/string.h>
#include <linux/err.h>	/* IS_ERR */
#include <linux/mm.h>	/* PAGE_SIZE */

/* A kernel pointer taken out of a kprobe register, checked before dereference: single_open()'s fake inode is (void *)1
 * and a kretprobe hands that sentinel over as "the argument" - the sus_map vma probe panicked on exactly that (fault
 * address 0xa1) until this test existed.  Every kernel object lives above the first page, so "< PAGE_SIZE" covers a
 * sentinel and a garbage register too, and IS_ERR() covers error pointers; user pointers must NOT go through it. */
static inline bool susfs_ptr_plausible(const void *p)
{
	return (unsigned long)p >= PAGE_SIZE && !IS_ERR(p);
}

/* Bind a fixed-size ABI pathname field to a C string safely: char[N] pathname fields a caller need not NUL-terminate, and
 * strlen()/strcmp()/kern_path() on one walks off the end of the struct, which lives on OUR kernel stack.  Every consumer of
 * an ABI pathname goes through this first. */
static inline bool susfs_abi_path_ok(const char *field, size_t size)
{
	return strnlen(field, size) < size;
}

/* Refuse to load when this image's imported symbol addresses are not kernel addresses, i.e. when
 * the loader that put it in did not absolutize them.  Returns 0 when they are all filled in, -EINVAL
 * otherwise (and prints which ones are missing).  Called first in susfs_init().  How much of the
 * table that really covers depends on the variant (see imports_guard.c), and for param_ops_* a
 * refusal cannot prevent the panic that follows - it only puts the reason in the log first. */
int susfs_imports_guard(void);

/* Second half of the same check, called once the symbol resolver is up: every import flagged
 * compare - the ten data imports, see imports_guard.c - whose name kallsyms has exactly once must
 * hold that address, so an image absolutized from the wrong symbol table (a stale kallsyms, or the
 * wrong occurrence of a duplicate name) is refused as well.  The function imports are not compared:
 * in the LLVM-CFI builds &name is a module-local stub there.
 * The definition carries __nocfi (it calls into the kernel through the resolver's function
 * pointers); the attribute belongs on the definition, so it is not repeated here. */
int susfs_imports_crosscheck(void);

/* Wait until no task can still be inside a window it entered before this call - a kprobe
 * handler, or boot_config_proc_show()'s seq_puts().  synchronize_rcu() is NOT enough for
 * either (neither runs in an RCU read-side section); see lsm_hook.c.  Sleeps. */
void susfs_wait_for_readers(void);

/* Add a path to sus_path's hidden set from kernel code (no supercall needed); returns 0 or negative errno. */
int sus_path_add_hidden(const char *path);

/* Same, for one of this module's own control nodes: flagged so the gate hides it from EVERY non-root caller, not merely
 * from app processes - otherwise a probe running as system (1000) or shell (2000) reads the node name out of /proc. */
int sus_path_add_self_hidden(const char *path);

/* Undo sus_path_add_self_hidden(): drops the rule for @path and restores whatever it changed (the relaxed mode, the inode
 * reference); returns the number of rules removed, so 0 means "was not registered".  Process context only (iput). */
int sus_path_del_path(const char *path);

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

/* The module's own name: the /sys/module directory and the modinfo name are both the module file name, and the self-hide
 * rules and the /proc/modules filter have to agree with it. */
#define SUSFS_LKM_MODULE_NAME "susfs_guard_lkm"
#define SUSFS_LKM_SYSFS_DIR   "/sys/module/" SUSFS_LKM_MODULE_NAME

/* The build's version string.  It lives here rather than in susfs_main.c because imports_guard.c
 * reports it when it refuses a load - and that happens before susfs_main.c prints its banner. */
#define SUSFS_LKM_VERSION "2.3.0-gki"

/* hide_modules (susfs_hide_syms.c): filters module NAMES out of /proc/modules (plus /sys/module and /proc/kallsyms for
 * non-root callers).  Node and same-named parameter are root-only; the node answers ENOENT through sus_path's hidden set. */
#define SUSFS_HIDE_MODULES_NODE "/proc/susfs_hide_modules"
bool susfs_hide_modules_active(void);

/* The sus_mount control node: decides WHICH mounts count as ours (the on/off switch stays the hide_sus_mnts_for_non_su_procs supercall); root-only, ENOENT for everyone else through sus_path. */
#define SUSFS_HIDE_MOUNTS_NODE "/proc/susfs_hide_mounts"
bool susfs_hide_modules_node_ready(void);

/* sus_path's own control node: `cat` is the rule listing (the hide_list parameter view), writing takes add/del/clear.  An
 * operator who cannot see the rule table cannot tell "registered" from "hiding"; root-only, everyone else gets ENOENT. */
#define SUSFS_PATH_NODE "/proc/susfs_path"

/* sus_map's rule node: add/del/clear the mappings the maps/smaps/map_files walk hides.  Root-only
 * through the same su-side gate as the other control nodes. */
#define SUSFS_MAP_NODE "/proc/susfs_map"

/* Whether the /proc/susfs_* control nodes are created at all.  Defaults to TRUE; they are created only when the LSM layer
 * that hides them is installed, so nobody ends up with an unprotected control node, and expose_proc=0 removes them. */
extern bool susfs_expose_proc;

/* feature init/exit (each feature is its own translation unit) */
int susfs_uname_init(void);
void susfs_uname_exit(void);

int susfs_kstat_init(void);
void susfs_kstat_exit(void);

int susfs_sus_map_init(void);
void susfs_sus_map_exit(void);

int sus_path_init(void);
void sus_path_exit(void);

int susfs_sus_mount_init(void);
void susfs_sus_mount_exit(void);

int susfs_spoof_cmdline_init(void);
void susfs_spoof_cmdline_exit(void);

int susfs_open_redirect_init(void);
void susfs_open_redirect_exit(void);

int susfs_enable_log_init(void);
void susfs_enable_log_exit(void);
bool susfs_log_enabled(void);

int susfs_avc_spoof_init(void);
void susfs_avc_spoof_exit(void);

int susfs_supercall_init(void);
void susfs_supercall_exit(void);
/* True when the sys_call_table hook could not arm and the supercall is delivered
 * through the kprobe fallback (see susfs_supercall_init()). */
bool susfs_supercall_kprobe_active(void);

/* Feature registry for the reboot(2) supercall.  A layer arms its feature only after its init
 * returned 0, so a non-fatal layer that failed is neither advertised by SHOW_ENABLED_FEATURES
 * nor allowed to have its commands hijacked (userspace then keeps its ERR_CMD_NOT_SUPPORTED
 * sentinel).  The reboot kprobe reads the bitmap with test_bit(); the arm happens once, in
 * process context, before the module can serve a command. */
enum susfs_feature {
	SUSFS_FEAT_SUS_PATH = 0,
	SUSFS_FEAT_SUS_MOUNT,
	SUSFS_FEAT_SUS_KSTAT,
	SUSFS_FEAT_SPOOF_UNAME,
	SUSFS_FEAT_ENABLE_LOG,
	SUSFS_FEAT_HIDE_SYMS,
	SUSFS_FEAT_SPOOF_CMDLINE,
	SUSFS_FEAT_OPEN_REDIRECT,
	SUSFS_FEAT_SUS_MAP,
	SUSFS_FEAT_AVC_SPOOF,	/* commands only: no upstream CONFIG_* line for it */
	SUSFS_FEAT_COUNT,
};

void susfs_supercall_arm_feature(enum susfs_feature feature);
bool susfs_supercall_feature_armed(enum susfs_feature feature);

/* feature supercall handlers (upstream signature: void xxx(void __user **arg)) */
void susfs_uname_supercall(void __user **arg);
void susfs_enable_log_supercall(void __user **arg);
void susfs_avc_spoof_supercall(void __user **arg);
void susfs_spoof_cmdline_supercall(void __user **arg);
void susfs_sus_map_supercall(void __user **arg);
void sus_path_supercall(unsigned int cmd, void __user **arg);
void susfs_kstat_supercall(unsigned int cmd, void __user **arg);
void susfs_open_redirect_supercall(void __user **arg);
void susfs_sus_mount_supercall(void __user **arg);

int susfs_hide_syms_init(void);
void susfs_hide_syms_exit(void);

/* Actual install state: enabled_features must not advertise a feature whose registration failed - hide_syms used to fail
 * silently and still be reported as active, the kind of inconsistency a detector looks for. */
bool susfs_hide_syms_active(void);
bool sus_path_lsm_active(void);

/* The O_PATH layer's own state, for the name layer that borrows its five open entries: true only when
 * at least one of those entries really carries its wrapper (opath_dump=1 and opath_test=N both leave
 * entries unwrapped while reporting success - see susfs_opath.c). */
bool susfs_opath_armed(void);

/* The same layer's post-check, called by open_redirect's name layer with the fd it produced itself, so
 * an O_PATH answer is judged by one implementation whichever of the two produced it.  A negative
 * argument (an errno) passes straight through. */
long susfs_opath_post_fd(long ret);

/* sus_path's hidden decision, for the layers that hold the inode the kernel resolved rather than a path they would have to
 * resolve themselves (susfs_opath.c post-checks the file an open(..., O_PATH) produced).  Same question the LSM hooks ask,
 * same answer: identity first, then (dev, i_ino), never a path string.  Not a new judgement - it is the static
 * sus_path_inode_hidden() with a name other translation units can call, so O_PATH and the ordinary open path cannot drift
 * apart.  NULL inode means "not hidden" (a negative dentry). */
struct inode;
bool sus_path_inode_hidden_public(struct inode *inode);

/* Whether any sus_path rule is live right now: the create-family entries test this before
 * they resolve a path, so an empty table costs one load and nothing else. */
bool sus_path_rules_present(void);

/* Whether a 0777 /proc/susfs_* control node may be created.  Two independent conditions, neither optional: susfs_expose_proc
 * (the operator opted in) and sus_path_lsm_active() (with 0777 DAC passes every caller, so only the LSM layer can still
 * answer ENOENT for an app).  This gates node CREATION ONLY, never hook registration: an earlier revision returned early from
 * the feature inits on !susfs_expose_proc, which silently disabled sus_kstat's tracepoint and kretprobe. */
static inline bool susfs_control_node_allowed(void)
{
	return susfs_expose_proc && sus_path_lsm_active();
}

/* open_redirect, reverse direction, for callers that only have an inode NUMBER: fdinfo prints "ino:\t<i>" with no device, so
 * this is a lookup by ino alone and returns false when the number is ambiguous (two rules, same redirected ino) or the caller is
 * not one the reverse disguise applies to.  On success *out_ino is the target inode to show, *out_mnt_id the target mount id. */
bool susfs_open_redirect_spoof_ids(unsigned long ino, unsigned long *out_ino, unsigned long *out_mnt_id);

/* The width of a rule's stored pathnames (the ABI fields are char[256], and matching that width stops
 * a legal long path from being truncated into a rule for a different path). */
#define SUSFS_OR_PATH_MAX 256

/* Asked by the name-layer entry hook (susfs_or_entry.c) for the path a caller is about to open.
 * The redirect target is COPIED into the caller's buffer (buflen >= SUSFS_OR_PATH_MAX) while the
 * lookup runs under rcu_read_lock, and the return value is false when no live rule matches (or the
 * name does not fit).  A pointer into the rule could not be handed out any more: rule slots are
 * recycled now (a deleted rule's slot is handed out again once a grace period has passed), so
 * nothing inside an entry may be read after the lookup returns. */
bool susfs_open_redirect_match_path(const struct path *path, char *buf, size_t buflen);

/* How many rules are live right now.  The name-layer hook runs for every open() on the system, so it
 * tests this before it resolves anything - a table with no live rule costs one load instead of a path
 * lookup, and deleting the last rule takes that cost away again.  Written under the rule lock. */
extern unsigned int susfs_open_redirect_rules;

static inline bool susfs_open_redirect_active(void)
{
	return READ_ONCE(susfs_open_redirect_rules) != 0;
}

/* The name-layer redirection of the same feature: no cached path, no inode held.  Optional - it
 * stays unarmed when a symbol it needs is missing, and such a layer makes open_redirect refuse
 * rules - and on by default (the or_entry parameter can turn it off). */
int susfs_or_entry_init(void);
void susfs_or_entry_exit(void);

/* The create family's entry layer (susfs_create_gate.c): answers EACCES for a hidden name
 * where filename_create() would have answered EEXIST before any LSM hook could run. */
int susfs_create_gate_init(void);
void susfs_create_gate_exit(void);

#endif
