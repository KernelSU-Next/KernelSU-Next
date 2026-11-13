/* SPDX-License-Identifier: GPL-2.0 */
/*
 * susfs_hook_map.h - the inline-hook capability table shared by the module and
 * the user-space loader.
 *
 * The loader probes only symbols that are unique in the kernel kallsyms, then
 * writes 1 for a confirmed out-of-line entry into the module's .data map.  A
 * default-0 map is what ksud leaves behind; a map that was not written by the
 * loader has ready == 0 and the module keeps the current fallback for every slot.
 *
 * Duplicate names are deliberately absent: s_show/m_show are resolved through
 * their seq_operations identity, and seq_show is per-filesystem, so none of them
 * can be described by a single symbol bit.
 */
#ifndef _SUSFS_HOOK_MAP_H
#define _SUSFS_HOOK_MAP_H

#include <linux/types.h>

#define SUSFS_HOOK_MAP_ABI	1u
#define SUSFS_HOOK_MAP_READY	0x53484650u	/* "SHFP": written last by the loader */

/*
 * One bit per unique out-of-line function.  The order is ABI: do not renumber,
 * only append, and bump SUSFS_HOOK_MAP_ABI when the meaning changes.
 *
 * X(enum_name, kallsyms_name)
 */
#define SUSFS_HOOK_SLOTS(X)					\
	X(VFS_OPEN,			vfs_open)		\
	X(PATH_OPENAT,			path_openat)		\
	X(SLOW_AVC_AUDIT,		slow_avc_audit)		\
	X(ARM64_SYS_NEWUNAME,		__arm64_sys_newuname)	\
	X(SHOW_MAP_VMA,			show_map_vma)		\
	X(SHOW_SMAP,			show_smap)		\
	X(SHOW_MAP,			show_map)		\
	X(WALK_PAGE_RANGE,		walk_page_range)	\
	X(WALK_PAGE_VMA,		walk_page_vma)		\
	X(GET_USER_PAGES_REMOTE,	get_user_pages_remote)	\
	X(__GET_USER_PAGES_REMOTE,	__get_user_pages_remote)\
	X(MAP_FILES_GET_LINK,		map_files_get_link)	\
	X(D_PATH,			d_path)			\
	X(VFS_STATFS,			vfs_statfs)		\
	X(SHOW_VFSSTAT,			show_vfsstat)		\
	X(SHOW_MOUNTINFO,		show_mountinfo)		\
	X(SHOW_VFSMNT,			show_vfsmnt)		\
	X(ARM64_SYS_STATX,		__arm64_sys_statx)	\
	X(DO_STATX,			do_statx)		\
	X(GENERIC_SHUTDOWN_SUPER,	generic_shutdown_super)	\
	X(COPY_MNT_NS,			copy_mnt_ns)		\
	X(ATTACH_RECURSIVE_MNT,		attach_recursive_mnt)

enum susfs_hook_slot {
#define SUSFS_HOOK_ENUM(name, sym)	SUSFS_HOOK_##name,
	SUSFS_HOOK_SLOTS(SUSFS_HOOK_ENUM)
#undef SUSFS_HOOK_ENUM
	SUSFS_HOOK_SLOT_COUNT
};

#define SUSFS_HOOK_WORDS	((SUSFS_HOOK_SLOT_COUNT + 31) / 32)

struct susfs_hook_map {
	u32 ready;		/* SUSFS_HOOK_MAP_READY only after a complete probe */
	u32 abi;
	u32 nbits;
	u32 bits[SUSFS_HOOK_WORDS];	/* default 0; 1 means out-of-line observed */
};

#ifndef __KERNEL__
#define SUSFS_HOOK_MAP_SECTION	".data.susfs_hook_map"
#else
#define SUSFS_HOOK_MAP_SECTION	".data.susfs_hook_map"
#endif

#endif /* _SUSFS_HOOK_MAP_H */
