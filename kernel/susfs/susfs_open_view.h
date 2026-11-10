/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_open_view.h - value-only view for the open entry
 *
 * L decides only whether the syscall shape can be taken over by the name
 * layer.  Rule lookup and the actual redirect stay in P.
 */

#ifndef __SUSFS_OPEN_VIEW_H
#define __SUSFS_OPEN_VIEW_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/fcntl.h>
#else
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
typedef uint64_t u64;
typedef uint32_t u32;
#ifndef O_DIRECTORY
#define O_DIRECTORY 00200000
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 00400000
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 02000000
#endif
#ifndef O_PATH
#define O_PATH 010000000
#endif
#ifndef __O_TMPFILE
#define __O_TMPFILE 020000000
#endif
#endif

enum susfs_open_kind {
	SUSFS_OPEN_OPENAT,
	SUSFS_OPEN_OPENAT2,
	SUSFS_OPEN_COMPAT_OPEN,
	SUSFS_OPEN_COMPAT_OPENAT,
	SUSFS_OPEN_COMPAT_OPENAT2,
};

enum susfs_open_action {
	SUSFS_OPEN_PASS,
	SUSFS_OPEN_TAKE,
};

#define SUSFS_OPEN_PATH_FLAGS (O_DIRECTORY | O_NOFOLLOW | O_PATH | O_CLOEXEC)
#define SUSFS_OPEN_RESOLVE_NO_XDEV	0x01
#define SUSFS_OPEN_RESOLVE_NO_MAGICLINKS	0x02
#define SUSFS_OPEN_RESOLVE_NO_SYMLINKS	0x04
#define SUSFS_OPEN_RESOLVE_BENEATH	0x08
#define SUSFS_OPEN_RESOLVE_IN_ROOT	0x10
#define SUSFS_OPEN_RESOLVE_CACHED	0x20
#define SUSFS_OPEN_REDIRECT_MAX 256
#define SUSFS_OPEN_KNOWN_RESOLVE					 \
	(SUSFS_OPEN_RESOLVE_NO_XDEV | SUSFS_OPEN_RESOLVE_NO_MAGICLINKS | \
	 SUSFS_OPEN_RESOLVE_NO_SYMLINKS | SUSFS_OPEN_RESOLVE_BENEATH |	 \
	 SUSFS_OPEN_RESOLVE_IN_ROOT | SUSFS_OPEN_RESOLVE_CACHED)

struct susfs_open_view {
	enum susfs_open_kind kind;
	unsigned long flags;
	u64 resolve;
	unsigned int mode;
	bool valid;
	bool mode_valid;
	bool resolve_supported;

	bool rule_matched;
	int uid_scheme;
	char redirected[SUSFS_OPEN_REDIRECT_MAX];
	u32 caller_uid;
	bool caller_in_su_domain;
	bool caller_is_umounted_app;
	bool caller_is_umounted;
};

static inline enum susfs_open_action
susfs_open_decide_core(const struct susfs_open_view *v)
{
	unsigned long flags;

	if (!v || !v->valid)
		return SUSFS_OPEN_PASS;

	flags = v->flags;
	if (flags & O_PATH)
		flags &= SUSFS_OPEN_PATH_FLAGS;

	if (flags & __O_TMPFILE)
		return SUSFS_OPEN_PASS;

	if (v->kind == SUSFS_OPEN_OPENAT2 ||
	    v->kind == SUSFS_OPEN_COMPAT_OPENAT2) {
		if (!v->resolve_supported)
			return SUSFS_OPEN_PASS;
		if (v->resolve & ~SUSFS_OPEN_KNOWN_RESOLVE)
			return SUSFS_OPEN_PASS;
		if ((v->resolve & SUSFS_OPEN_RESOLVE_BENEATH) &&
		    (v->resolve & SUSFS_OPEN_RESOLVE_IN_ROOT))
			return SUSFS_OPEN_PASS;
		if ((v->resolve & SUSFS_OPEN_RESOLVE_CACHED) &&
		    (flags & (O_TRUNC | O_CREAT | __O_TMPFILE)))
			return SUSFS_OPEN_PASS;
		if (!v->mode_valid)
			return SUSFS_OPEN_PASS;
	}

	if (!v->rule_matched)
		return SUSFS_OPEN_TAKE;

	switch (v->uid_scheme) {
	case 0:	/* UID_NON_APP_PROC */
		if (v->caller_uid % 100000 >= 10000)
			return SUSFS_OPEN_PASS;
		break;
	case 1:	/* UID_ROOT_PROC_EXCEPT_SU_PROC */
		if (v->caller_uid != 0 || v->caller_in_su_domain)
			return SUSFS_OPEN_PASS;
		break;
	case 2:	/* UID_NON_SU_PROC */
		if (v->caller_in_su_domain)
			return SUSFS_OPEN_PASS;
		break;
	case 3:	/* UID_UMOUNTED_APP_PROC */
		if (!v->caller_is_umounted_app)
			return SUSFS_OPEN_PASS;
		break;
	case 4:	/* UID_UMOUNTED_PROC */
		if (!v->caller_is_umounted)
			return SUSFS_OPEN_PASS;
		break;
	default:
		return SUSFS_OPEN_PASS;
	}

	return SUSFS_OPEN_TAKE;
}

#endif /* __SUSFS_OPEN_VIEW_H */
