/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The ABI slot list shared by the module and tools/susfs_insmod.c.
 * X(enum_name, kallsyms_name): append only, and bump the map ABI when meaning
 * changes.  Only names that are unique in the kernel kallsyms belong here.
 */
#ifndef _SUSFS_HOOK_SLOTS_H
#define _SUSFS_HOOK_SLOTS_H

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
	X(ATTACH_RECURSIVE_MNT,		attach_recursive_mnt)	\
	/* sus_path: the VFS-layer landing pads upstream patches into fs/namei.c, \
	 * fs/stat.c, fs/xattr.c and fs/attr.c.  One slot per operation so a slot \
	 * that cannot be armed only costs that operation its inline path. */ \
	X(VFS_GETATTR,			vfs_getattr)		\
	X(VFS_GETATTR_NOSEC,		vfs_getattr_nosec)	\
	X(INODE_PERMISSION,		inode_permission)	\
	X(VFS_UNLINK,			vfs_unlink)		\
	X(VFS_RMDIR,			vfs_rmdir)		\
	X(VFS_RENAME,			vfs_rename)		\
	X(VFS_LINK,			vfs_link)		\
	X(NOTIFY_CHANGE,		notify_change)		\
	X(VFS_GETXATTR,			vfs_getxattr)		\
	X(VFS_LISTXATTR,		vfs_listxattr)		\
	X(VFS_SETXATTR,			vfs_setxattr)		\
	X(VFS_REMOVEXATTR,		vfs_removexattr)	\
	X(VFS_READLINK,			vfs_readlink)

#endif /* _SUSFS_HOOK_SLOTS_H */
