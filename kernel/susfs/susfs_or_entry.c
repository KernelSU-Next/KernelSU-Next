// SPDX-License-Identifier: GPL-2.0
/*
 * susfs_or_entry.c - open_redirect's forward direction, done at the NAME layer.
 *
 * Why this exists: the vfs_open kprobe has to keep a cached struct path per rule, because by the
 * time vfs_open runs the name is already resolved and nothing can sleep there.  Cached dentry and
 * mount references are what stop a filesystem teardown from finishing while a rule points into it.
 * Upstream has no such problem because it patches do_sys_openat2, where it simply builds a new
 * struct filename from the stored string; those symbols are LTO-inlined here and cannot be probed,
 * but the syscall entry can be replaced - susfs_opath.c already replaces exactly the five entries
 * this needs - so the same thing can be done at the same layer as upstream.
 *
 * What it does per matching open, in process context before the syscall runs:
 *   getname_flags() + filename_lookup()  -> which inode is the caller opening (relative paths too)
 *   susfs_open_redirect_match_path()     -> is that the target of a live rule, and where does it go
 *   filp_open()                          -> open the target ourselves
 *   get_unused_fd_flags() + fd_install() -> hand the caller a fd, exactly like the syscall would
 *   and return the fd instead of running the original syscall.
 *
 * Nothing is cached, so a rule holds no reference of any kind: this is what makes open_redirect
 * stop mattering to umount.  The functions are resolved by name (filp_open, get_unused_fd_flags and
 * fd_install are exported, getname_flags, filename_lookup and putname are not) and the layer stays
 * off if any of them is missing.
 *
 * Skipping the original syscall means whatever do_sys_openat2() would have done around the open has
 * to be done here as well.  Two things are: the arm64 O_LARGEFILE bit the syscall adds on the entries
 * that set it (otherwise every redirected fd differs from every other fd in one bit that
 * fcntl(F_GETFL) and the fdinfo `flags:` line both read), and the IN_OPEN notification where the
 * kernel happens to have an out-of-line fsnotify_open() - it is a static inline on every tree, and
 * the log says so where it is missing.  What is deliberately NOT reproduced, and is therefore handed
 * back to the kernel instead:
 *   - the flags openat2 would refuse (it validates its structure field by field, where the older
 *     entries clean invalid values up and carry on), along with its `resolve`/`usize` (EINVAL/E2BIG,
 *     and RESOLVE_* is applied during the walk) - finishing such an open would answer success where
 *     the kernel answers an error, or answer with weaker semantics;
 *   - O_TMPFILE, which is not a path open in the sense filp_open() models.
 * Not covered at all, by construction: this is a syscall entry, so covers openat/openat2 and the
 * three compat variants.  io_uring's OPENAT/OPENAT2, AArch32 `creat`, open_by_handle_at, execve and
 * in-kernel filp_open() callers never come through here (upstream's do_sys_openat2 patch has the same
 * boundary except for compat `creat`, which upstream also misses).
 *
 * On by default (module parameter or_entry).  It used to be opt-in, back when the vfs_open probe was
 * still the default forward direction; that probe is gone, so this layer IS the forward direction -
 * which is also why or_register() refuses a rule while it is not armed, and why an unarmed layer
 * says so in the log instead of failing silently.
 */
#include <linux/kernel.h>
#include <linux/version.h>	/* LINUX_VERSION_CODE: fsnotify_open() moved into the open path at 6.6 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/fcntl.h>	/* AT_FDCWD, O_CLOEXEC, __O_TMPFILE */
#include <linux/path.h>		/* path_put() */
#include <linux/namei.h>	/* getname_flags/filename_lookup prototypes, LOOKUP_FOLLOW */
#include <linux/dcache.h>
#include <linux/mount.h>
#include <linux/uaccess.h>
#include <linux/err.h>
#include <linux/atomic.h>
#include <linux/limits.h>
#include "susfs.h"
#include "susfs_log.h"
#include "symbol_resolver.h"
#include "susfs_kprobe.h"	/* susfs_sc_arg */
#include "susfs_or_entry.h"

/* include/uapi/linux/openat2.h: three u64 fields, identical on both ABIs we build for. */
struct susfs_open_how {
	__u64 flags;
	__u64 mode;
	__u64 resolve;
};

/* The flag set the kernel's own build_open_flags() accepts (fs/open.c VALID_OPEN_FLAGS, which is
 * built from exactly these uapi bits).  A flag outside it makes the kernel return EINVAL, so such a
 * call is handed back rather than answered: filp_open() masks unknown bits away and would turn that
 * EINVAL into a successful open.  This is deliberately the conservative direction - a bit that is
 * legal on some kernel but missing here only means that open is not redirected. */
#define SUSFS_OR_KNOWN_FLAGS							\
	(O_RDONLY | O_WRONLY | O_RDWR | O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC |	\
	 O_APPEND | O_NONBLOCK | O_NDELAY | __O_SYNC | O_DSYNC | FASYNC |	\
	 O_DIRECT | O_LARGEFILE | O_DIRECTORY | O_NOFOLLOW | O_NOATIME |		\
	 O_CLOEXEC | O_PATH | __O_TMPFILE)

/* fs/open.c's own O_PATH_FLAGS, and what build_open_how() narrows the flags to whenever O_PATH is
 * present ("O_PATH beats everything else.").  Only these four bits survive that narrowing, so
 * everything a caller sends alongside O_PATH - O_CREAT, O_EXCL, O_TRUNC - is gone before the kernel
 * works out the lookup flags of the call.  It lives as a #define inside fs/open.c rather than in a
 * header, hence spelled out here; O_CLOEXEC being in the set is also why a descriptor's own flag
 * survives the narrowing. */
#define SUSFS_OR_PATH_FLAGS	(O_DIRECTORY | O_NOFOLLOW | O_PATH | O_CLOEXEC)

/* openat2's resolve bits (uapi/linux/openat2.h) and the lookup flags the kernel turns them into. */
#define SUSFS_RESOLVE_NO_XDEV		0x01
#define SUSFS_RESOLVE_NO_MAGICLINKS	0x02
#define SUSFS_RESOLVE_NO_SYMLINKS	0x04
#define SUSFS_RESOLVE_BENEATH		0x08
#define SUSFS_RESOLVE_IN_ROOT		0x10
#define SUSFS_RESOLVE_CACHED		0x20
#define SUSFS_OR_KNOWN_RESOLVE							\
	(SUSFS_RESOLVE_NO_XDEV | SUSFS_RESOLVE_NO_MAGICLINKS |			\
	 SUSFS_RESOLVE_NO_SYMLINKS | SUSFS_RESOLVE_BENEATH |			\
	 SUSFS_RESOLVE_IN_ROOT | SUSFS_RESOLVE_CACHED)

/* The lookup flags for the source path, translated the way the kernel translates them for the open
 * it would have done (fs/open.c, before do_filp_open): O_NOFOLLOW stops at the last component and
 * O_CREAT|O_EXCL implies it, O_DIRECTORY demands a directory, and openat2's resolve bits map onto
 * the matching LOOKUP_* bits.  A resolve bit this kernel has no lookup flag for (LOOKUP_CACHED
 * arrived later than the others) is not mapped - the caller then gets a lookup with weaker
 * semantics, which is why or_entry_redirect() rejects those calls instead of answering them. */
static unsigned int or_lookup_flags(unsigned long flags, unsigned long resolve)
{
	unsigned int lflags = 0;

	if (!(flags & O_NOFOLLOW) &&
	    (flags & (O_CREAT | O_EXCL)) != (O_CREAT | O_EXCL))
		lflags |= LOOKUP_FOLLOW;
	if (flags & O_DIRECTORY)
		lflags |= LOOKUP_DIRECTORY;
	if (resolve & SUSFS_RESOLVE_NO_XDEV)
		lflags |= LOOKUP_NO_XDEV;
	if (resolve & SUSFS_RESOLVE_NO_MAGICLINKS)
		lflags |= LOOKUP_NO_MAGICLINKS;
	if (resolve & SUSFS_RESOLVE_NO_SYMLINKS)
		lflags |= LOOKUP_NO_SYMLINKS;
	if (resolve & SUSFS_RESOLVE_BENEATH)
		lflags |= LOOKUP_BENEATH;
	if (resolve & SUSFS_RESOLVE_IN_ROOT)
		lflags |= LOOKUP_IN_ROOT;
#ifdef LOOKUP_CACHED
	if (resolve & SUSFS_RESOLVE_CACHED)
		lflags |= LOOKUP_CACHED;
#endif
	return lflags;
}

/* Every one of these is resolved by name at load; see the header comment for which are exported. */
static struct filename *(*or_getname_flags_fn)(const char __user *name, int flags, int *empty);
static int (*or_filename_lookup_fn)(int dfd, struct filename *name, unsigned flags,
				    struct path *path, struct path *root);
static void (*or_putname_fn)(struct filename *name);
static struct file *(*or_filp_open_fn)(const char *name, int flags, umode_t mode);
static int (*or_get_unused_fd_flags_fn)(unsigned flags);
static void (*or_fd_install_fn)(unsigned int fd, struct file *file);
static void (*or_put_unused_fd_fn)(unsigned int fd);
static void (*or_fsnotify_open_fn)(struct file *file);
static int (*or_check_zeroed_user_fn)(const void __user *from, unsigned long count);

static bool or_entry_ready;
static atomic_t or_entry_hits = ATOMIC_INIT(0);

/* On by default: this IS the forward direction now (the vfs_open probe is gone from
 * susfs_open_redirect.c).  Off means the layer is not armed at all - see
 * susfs_or_entry_init() - so a rule is refused rather than accepted and inert. */
static bool or_entry_param = true;
module_param_named(or_entry, or_entry_param, bool, 0600);

susfs_or_entry_fn_t susfs_or_entry_hook;

/* Indirect call into the kernel for the one notification the skipped syscall would have sent, so it
 * sits in a __nocfi noinline function like every other one in this module (tools/cfi_sites.py fails
 * the build on an instrumented call site).  A kernel without the symbol only loses the notification,
 * which is why this one may be NULL. */
static __nocfi noinline void or_fsnotify_open(struct file *file)
{
	if (or_fsnotify_open_fn)
		or_fsnotify_open_fn(file);
}

/* Same shape for copy_struct_from_user()'s zero check: it is the helper openat2 uses to accept a
 * structure larger than this kernel's, and it is a real exported symbol even though the macro that
 * calls it is not. */
static __nocfi noinline int or_check_zeroed_user(const void __user *from, unsigned long count)
{
	return or_check_zeroed_user_fn(from, count);
}

/* The undo of get_unused_fd_flags(): needed to keep the kernel's own order (descriptor first, file
 * second), where a failed open leaves nothing behind. */
static __nocfi noinline void or_put_unused_fd(unsigned int fd)
{
	or_put_unused_fd_fn(fd);
}

/* The original syscall is not called at all when this redirects, so nothing has to be undone if the
 * process is killed mid-way; the fd is installed last, which is the same order the kernel uses.
 *
 * __nocfi noinline is mandatory for every call through a runtime-resolved pointer (the rule
 * tools/cfi_sites.py enforces on the artifacts): without it, CFI instrumentation lands in whichever
 * function this ends up in - the opath wrapper - and a checked indirect call into the kernel is a
 * panic rather than an error return.  This is also why every kernel function above is held in a
 * plain pointer and called from here, never from the wrapper itself. */
static __nocfi noinline long or_entry_redirect(const struct pt_regs *regs, int kind)
{
	const char __user *uname = NULL;
	unsigned long flags = 0, mode = 0, resolve = 0, usize = 0, o2flags = 0;
	/* The descriptor's own flag, kept out of the narrowing the file flags go through: the kernel
	 * takes it from the UNSTRIPPED flags when it allocates a descriptor (do_sys_openat2():
	 * get_unused_fd_flags(how->flags), where alloc_fd() reads O_CLOEXEC), and losing it hands back a fd that survives
	 * execve - a difference one fcntl(F_GETFD) reads. */
	unsigned int fd_cloexec = 0;
	struct susfs_open_how how;
	struct filename *name;
	struct path path;
	char target[SUSFS_OR_PATH_MAX];	/* filled by susfs_open_redirect_match_path() under rcu */
	struct file *f;
	int dfd = AT_FDCWD;
	long res;
	int fd;

	if (!or_entry_ready)
		return SUSFS_OR_ENTRY_PASS;

	/* This hook is reached for every open() on the system, so the empty case has to be one load: no
	 * live rules means nothing to resolve a name for, and deleting the last rule takes that cost away
	 * again (susfs_open_redirect_rules counts live rules, not rules ever added). */
	if (!susfs_open_redirect_active())
		return SUSFS_OR_ENTRY_PASS;

	/* The argument number is the register index on arm64; susfs_sc_arg maps it
	 * to the right register per arch (susfs_kprobe.h). */
	switch (kind) {
	case SUSFS_OR_ENTRY_OPENAT:
	case SUSFS_OR_ENTRY_COMPAT_OPENAT:
		dfd = (int)susfs_sc_arg(regs, 0);
		uname = (const char __user *)susfs_sc_arg(regs, 1);
		/* Truncated to what the kernel's own declaration is: these entries take an `int flags`
		 * and the syscall wrapper casts the register to it (__SC_CAST), so the upper half of the
		 * register is ignored by the kernel.  Keeping it here would make the unknown-bit test
		 * below see a "flag the kernel would reject" and hand a call the kernel resolves as a
		 * plain open straight back to it - i.e. the redirect would be bypassable with
		 * `flags | (1UL << 32)`.  openat2 is the exception and stays 64-bit, because the kernel
		 * does not mask how.flags. */
		flags = (unsigned int)susfs_sc_arg(regs, 2);
		mode = susfs_sc_arg(regs, 3);
		break;
	case SUSFS_OR_ENTRY_COMPAT_OPEN:
		/* COMPAT_SYSCALL_DEFINE3: no dirfd, so the arguments shift down by one. */
		uname = (const char __user *)susfs_sc_arg(regs, 0);
		flags = (unsigned int)susfs_sc_arg(regs, 1);
		mode = susfs_sc_arg(regs, 2);
		break;
	case SUSFS_OR_ENTRY_OPENAT2:
	case SUSFS_OR_ENTRY_COMPAT_OPENAT2:
		dfd = (int)susfs_sc_arg(regs, 0);
		uname = (const char __user *)susfs_sc_arg(regs, 1);
		usize = susfs_sc_arg(regs, 3);
		/* The kernel's own rules for this structure: a size below it is EINVAL, a size above it
		 * is accepted as long as the bytes this kernel does not know are zero (that is what
		 * copy_struct_from_user() checks, through check_zeroed_user()), and anything else is
		 * E2BIG.  A call that gets here and is then not redirected still opens the real file, so
		 * "we cannot be bothered" is not an option for a size the kernel would have accepted:
		 * those go through when the helper is resolvable, and only fall back to the kernel where
		 * it is not. */
		if (usize < sizeof(how))
			return SUSFS_OR_ENTRY_PASS;
		if (copy_from_user(&how, (const void __user *)susfs_sc_arg(regs, 2), sizeof(how)))
			return SUSFS_OR_ENTRY_PASS;
		if (usize > sizeof(how) &&
		    (!or_check_zeroed_user_fn ||
		     or_check_zeroed_user((const void __user *)susfs_sc_arg(regs, 2) + sizeof(how),
					  usize - sizeof(how)) <= 0))
			return SUSFS_OR_ENTRY_PASS;
		/* ... and the two bits the kernel removes from these flags before it looks at any of them
		 * (FMODE_NONOTIFY, which the caller is not supposed to see, and O_CLOEXEC, which belongs
		 * to the descriptor).  Everything below is judged on what is left, exactly like the
		 * kernel does: FMODE_NONOTIFY is not one of O_PATH_FLAGS, so judging the raw flags would
		 * turn a legal `O_PATH|FMODE_NONOTIFY` open into "a flag the kernel would refuse" and hand
		 * the call back to a kernel that opens the real file. */
		/* O_CLOEXEC is stripped from the copy everything below judges, because it is not a
		 * property of the file - but it belongs to the DESCRIPTOR, and the kernel keeps it for
		 * exactly the moment it allocates one: do_sys_openat2() calls
		 * get_unused_fd_flags(how->flags), where alloc_fd() reads O_CLOEXEC.  Dropping it here
		 * (this branch assigns flags = o2flags a few lines down, and that copy is what the
		 * descriptor was allocated from) gave every redirected openat2(..., O_CLOEXEC) a fd
		 * without FD_CLOEXEC: it survives execve, and fcntl(F_GETFD) says so in one call. */
		fd_cloexec = (unsigned int)(how.flags & O_CLOEXEC);
		o2flags = how.flags & ~(unsigned long)(FMODE_NONOTIFY | O_CLOEXEC);
		/* A resolve bit the kernel does not know is EINVAL, and EINVAL is an answer we cannot
		 * give: it comes from the kernel.  A bit it does know is mapped onto the same LOOKUP_*
		 * flags it would use (see or_lookup_flags()), so the caller's resolve semantics still
		 * hold for the path this layer looks up itself. */
		if (how.resolve & ~SUSFS_OR_KNOWN_RESOLVE)
			return SUSFS_OR_ENTRY_PASS;
		/* The two combinations the kernel refuses before it looks at anything: RESOLVE_BENEATH
		 * together with RESOLVE_IN_ROOT is EINVAL, and RESOLVE_CACHED with a call that would
		 * create or truncate is EAGAIN (it will not fall back to a normal lookup).  Both are
		 * answered by the kernel rather than by us - the second one is not just a difference in
		 * the answer, it is a rule target that would be created or truncated.  The mask is the
		 * kernel's own spelling, which is EAGAIN for a plain O_DIRECTORY too. */
		if ((how.resolve & SUSFS_RESOLVE_BENEATH) && (how.resolve & SUSFS_RESOLVE_IN_ROOT))
			return SUSFS_OR_ENTRY_PASS;
		if ((how.resolve & SUSFS_RESOLVE_CACHED) &&
		    (o2flags & (O_TRUNC | O_CREAT | O_TMPFILE)))
			return SUSFS_OR_ENTRY_PASS;
#ifndef LOOKUP_CACHED
		/* This kernel cannot honour RESOLVE_CACHED, and answering the call with a normal lookup
		 * would succeed where the kernel answers EAGAIN (it refuses to touch the disk for it). */
		if (how.resolve & SUSFS_RESOLVE_CACHED)
			return SUSFS_OR_ENTRY_PASS;
#endif
		resolve = how.resolve;
		/* openat2 validates every field of the structure, where the older syscalls silently
		 * clean invalid values up (build_open_flags(): mode & ~S_IALLUGO is EINVAL when the
		 * call creates something, non-zero mode otherwise, and filp_open() would strip both
		 * before the kernel could object).  Answering such a call with a successful open is the
		 * one outcome we must not produce, so it goes back too. */
		if ((o2flags & (O_CREAT | __O_TMPFILE)) ? (how.mode & ~S_IALLUGO) : how.mode)
			return SUSFS_OR_ENTRY_PASS;
		/* openat2 does not pass through build_open_how(), so the kernel tests its O_PATH rule
		 * against these same stripped flags: anything besides O_PATH_FLAGS is EINVAL there, while
		 * filp_open() would quietly mask those bits away and open the file. */
		if ((o2flags & O_PATH) &&
		    (o2flags & ~(unsigned long)(O_DIRECTORY | O_NOFOLLOW | O_PATH | O_CLOEXEC)))
			return SUSFS_OR_ENTRY_PASS;
		flags = o2flags;
		mode = how.mode;
		break;
	default:
		return SUSFS_OR_ENTRY_PASS;
	}

	/* The flags are handled the way the kernel handles them per entry, because that
	 * differs: openat2 validates every field and answers EINVAL for a flag it does not know
	 * (fs/open.c: build_open_flags() tests the raw how.flags), while open/openat/compat clean the
	 * invalid bits away first (build_open_how(): `flags & VALID_OPEN_FLAGS`) and then open the file
	 * as if they had not been passed.  Treating an unknown bit as "the kernel will refuse this" on
	 * the older entries would hand the call back to a kernel that happily opens the real file - a
	 * one-bit bypass of the redirect, and a cheaper one than any of the above.  So the older entries
	 * get the same mask the kernel applies, and only openat2 can be declined over a flag. */
	if (kind == SUSFS_OR_ENTRY_OPENAT2 || kind == SUSFS_OR_ENTRY_COMPAT_OPENAT2) {
		/* Those two bits were already removed in the branch above, before its first check - the
		 * kernel removes them before any of its own.  What is left is the gate the kernel applies
		 * to these flags (build_open_flags(): `flags & ~VALID_OPEN_FLAGS` is EINVAL). */
		if (flags & ~SUSFS_OR_KNOWN_FLAGS)
			return SUSFS_OR_ENTRY_PASS;
	} else {
		flags &= SUSFS_OR_KNOWN_FLAGS;
		/* The kernel narrows O_PATH before anything else looks at the flags - "O_PATH beats
		 * everything else." in build_open_how() - so by the time build_open_flags() works out
		 * whether the last component is followed, an O_CREAT/O_EXCL that arrived alongside
		 * O_PATH is already gone.  Judging the flags as they arrived made this layer resolve a
		 * DIFFERENT name than the kernel resolves: it saw O_CREAT|O_EXCL, withheld
		 * LOOKUP_FOLLOW, and matched the rule table against the symlink itself, while the
		 * kernel followed that symlink and opened its target.  So a caller who writes
		 * `ln -s <rule target> <a path of his own>` and then asks for O_PATH|O_CREAT|O_EXCL
		 * was handed the un-redirected file - the redirect stepped around without ever touching
		 * the rule table.  Narrowed here, both layers walk the same name.
		 *
		 * (openat2 does not narrow: it never calls build_open_how(), and build_open_flags()
		 * answers that same combination with EINVAL - which is why the branch above is left
		 * alone.) */
		if (flags & O_PATH)
			flags &= SUSFS_OR_PATH_FLAGS;
		/* From the same function: a call that cannot create carries no mode, and an O_PATH
		 * open never can. */
		if (flags & O_PATH)
			mode = 0;
		/* O_CLOEXEC survives the narrowing above (it is one of O_PATH_FLAGS), so the
		 * descriptor's flag is read from the flags the kernel would allocate it from. */
		fd_cloexec = (unsigned int)(flags & O_CLOEXEC);
	}

	/* O_TMPFILE is not a path open in the sense filp_open() models (it needs the O_DIRECTORY part
	 * handled by the caller's dirfd), so those are left to the kernel untouched.  This check sits
	 * AFTER the narrowing above on purpose: build_open_how() discards __O_TMPFILE whenever O_PATH is
	 * present, so `openat(p, O_PATH|__O_TMPFILE)` is a plain O_PATH open to the kernel - handing
	 * that combination back here would leave one more open the kernel resolves while this layer
	 * never looked at it.  (openat2 answers the same combination with EINVAL instead, and the
	 * branch above already passes such a call through.) */
	if (flags & __O_TMPFILE)
		return SUSFS_OR_ENTRY_PASS;

	/* Which inode is the caller opening?  The lookup flags are the kernel's own translation of the
	 * caller's flags and of openat2's resolve bits (fs/open.c, right before do_filp_open): with
	 * LOOKUP_FOLLOW hard-wired, a caller's O_NOFOLLOW turned its ELOOP into a successful redirect -
	 * a difference from a kernel without this module, and one that also tells an observer which
	 * paths carry a rule (write a symlink to the path and open it with O_NOFOLLOW).  A path that
	 * does not resolve (O_CREAT of a new file) cannot be the target of a rule, so the kernel is
	 * left to answer it. */
	name = or_getname_flags_fn(uname, 0, NULL);
	if (IS_ERR(name))
		return SUSFS_OR_ENTRY_PASS;
	res = or_filename_lookup_fn(dfd, name, or_lookup_flags(flags, resolve), &path, NULL);
	or_putname_fn(name);
	if (res)
		return SUSFS_OR_ENTRY_PASS;

	/* The target name is copied out of the rule while the lookup holds rcu_read_lock (rule slots are
	 * recycled after a grace period, so nothing inside an entry may be used after this call). */
	if (!susfs_open_redirect_match_path(&path, target, sizeof(target))) {
		path_put(&path);
		return SUSFS_OR_ENTRY_PASS;
	}
	path_put(&path);	/* no reference is kept anywhere: the rule owns none either */
	/* Defensive: an empty name is not a path, and filp_open("") resolves to the caller's own
	 * working directory, i.e. it would hand out an unrelated fd. */
	if (!target[0])
		return SUSFS_OR_ENTRY_PASS;

	/* arm64 always opens with O_LARGEFILE (force_o_largefile() is `!IS_ENABLED(CONFIG_ARCH_32BIT_OFF_T)`,
	 * and the syscall adds it before anything else looks at the flags).  Without it the fd we hand
	 * back differs from every other fd in one visible bit: fcntl(F_GETFL) and the `flags:` line of
	 * /proc/self/fdinfo/N both read f_flags directly.  CLOEXEC is the fd table's business, not the
	 * open's - the kernel strips it before build_open_flags() sees it - so it is masked off here.
	 *
	 * The compat entries are the exception: the kernel deliberately does not set the bit there
	 * ("Exactly like sys_openat(), except that it doesn't set the O_LARGEFILE flag"), and compat
	 * openat2 does, because it is the same implementation as the native one.  An O_PATH open does
	 * not carry it either - build_open_how() masks O_PATH opens to O_PATH_FLAGS. */
#ifdef __aarch64__
	if (!(flags & O_PATH) &&
	    kind != SUSFS_OR_ENTRY_COMPAT_OPEN && kind != SUSFS_OR_ENTRY_COMPAT_OPENAT)
		flags |= O_LARGEFILE;
#endif
	/* The descriptor is taken BEFORE the file is opened, which is the order do_sys_openat2() uses:
	 * an EMFILE/ENFILE open then touches nothing at all, where opening first would have created or
	 * truncated the rule's target and then failed anyway. */
	fd = or_get_unused_fd_flags_fn(fd_cloexec);
	if (fd < 0)
		return fd;

	f = or_filp_open_fn(target, (int)(flags & ~O_CLOEXEC), (umode_t)mode);
	if (IS_ERR(f)) {
		or_put_unused_fd((unsigned)fd);
		return PTR_ERR(f);
	}

	/* Same order as do_sys_openat2(): the notification goes out before the fd is visible.
	 *
	 * Only where the kernel does not send it itself.  On 5.10..6.1 the one fsnotify_open() call
	 * sits in do_sys_openat2(), which a filp_open() never reaches - without this line the
	 * redirected open would be the only open on the system with no IN_OPEN, which breaks inotify
	 * users and makes the redirect stand out.  6.6 moved that call into do_dentry_open()
	 * (fs/open.c:985 there) and 6.12 split it out again into vfs_open() (:1095); filp_open() goes
	 * through both, so on 6.6 and later the kernel has already reported this open and a second
	 * call here produced two IN_OPEN events for one open. */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)
	or_fsnotify_open(f);
#endif
	or_fd_install_fn((unsigned)fd, f);

	atomic_inc(&or_entry_hits);
	return fd;
}

int susfs_or_entry_init(void)
{
	/* or_entry=0 means "no redirection at the name layer", and the rule-management path asks this
	 * layer whether it is armed: leaving the hook installed but inert would accept rules that can
	 * never fire, so the parameter is honoured here and the hook simply stays NULL. */
	if (!READ_ONCE(or_entry_param)) {
		SUSFS_LOGI("or_entry=0: name layer off (open_redirect will refuse rules)\n");
		return 0;
	}

	/* The five open entries this layer needs are the ones susfs_opath.c installs: asking whether the
	 * name layer is armed has to mean "at least one of those entries really carries a wrapper", or a
	 * rule would be accepted from a device where opath_dump=1 / opath_test=N / a failed install left
	 * the entries untouched - a rule that can never fire. */
	if (!susfs_opath_armed()) {
		SUSFS_LOGI("or_entry: susfs_opath installed none of the open entries - the name layer stays off (open_redirect will refuse rules)\n");
		return 0;
	}

	or_getname_flags_fn = (void *)find_kernel_symbol_exact("getname_flags");
	or_filename_lookup_fn = (void *)find_kernel_symbol_exact("filename_lookup");
	or_putname_fn = (void *)find_kernel_symbol_exact("putname");
	or_filp_open_fn = (void *)find_kernel_symbol_exact("filp_open");
	or_get_unused_fd_flags_fn = (void *)find_kernel_symbol_exact("get_unused_fd_flags");
	or_fd_install_fn = (void *)find_kernel_symbol_exact("fd_install");
	or_put_unused_fd_fn = (void *)find_kernel_symbol_exact("put_unused_fd");
	/* Required rather than optional: the descriptor is now taken before the file is opened, and
	 * without the undo a failed open would leak the descriptor.  Taking the descriptor first is what
	 * keeps an EMFILE/ENFILE open from having created or truncated the rule's target. */
	/* Optional: only the inotify/fanotify notification depends on it. */
	or_fsnotify_open_fn = (void *)find_kernel_symbol_exact("fsnotify_open");
	if (!or_fsnotify_open_fn)
		SUSFS_LOGI("or_entry: fsnotify_open not found - a redirected open will not produce IN_OPEN\n");
	/* Optional too, and only for openat2 calls whose structure is larger than this kernel's: without
	 * it those go back to the kernel (and are then not redirected). */
	or_check_zeroed_user_fn = (void *)find_kernel_symbol_exact("check_zeroed_user");
	if (!or_check_zeroed_user_fn)
		SUSFS_LOGI("or_entry: check_zeroed_user not found - openat2 with a larger open_how is not redirected\n");

	if (!or_getname_flags_fn || !or_filename_lookup_fn || !or_putname_fn ||
	    !or_filp_open_fn || !or_get_unused_fd_flags_fn || !or_fd_install_fn ||
	    !or_put_unused_fd_fn) {
		SUSFS_LOGI("or_entry: not armed, a symbol is missing (getname_flags=%d filename_lookup=%d putname=%d filp_open=%d get_unused_fd_flags=%d fd_install=%d put_unused_fd=%d) - open_redirect will refuse rules\n",
			   !!or_getname_flags_fn, !!or_filename_lookup_fn, !!or_putname_fn,
			   !!or_filp_open_fn, !!or_get_unused_fd_flags_fn, !!or_fd_install_fn,
			   !!or_put_unused_fd_fn);
		return 0;
	}

	or_entry_ready = true;
	smp_wmb();
	WRITE_ONCE(susfs_or_entry_hook, or_entry_redirect);
	SUSFS_LOGI("or_entry: name-layer redirection armed; open_redirect rules hold no inode while it is on\n");
	return 0;
}

void susfs_or_entry_exit(void)
{
	WRITE_ONCE(susfs_or_entry_hook, NULL);
	smp_mb();
	or_entry_ready = false;
}
