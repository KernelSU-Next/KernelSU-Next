/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_dirent_phys.c - getdents record layout and syscall execution (P layer)
 *
 * fs/readdir.c's getdents/getdents64 record layout, reimplemented with the
 * filtering filldir in place of filldir64/compat_filldir.
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/limits.h>
#include <linux/mutex.h>
#include <linux/sched/signal.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "susfs_dirent_data.h"
#include "susfs_dirent_phys.h"
#include "susfs_dirent_policy.h"

/* 6.1 turned filldir_t's "keep going" from 0 into true. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
#define SD_RET	bool
#define SD_GO	true
#define SD_STOP	false
#else
#define SD_RET	int
#define SD_GO	0
#define SD_STOP	-EINVAL
#endif

struct sd_dirent64 {
	u64		d_ino;
	s64		d_off;
	unsigned short	d_reclen;
	unsigned char	d_type;
	char		d_name[];
};

struct sd_dirent_compat {
	u32		d_ino;
	u32		d_off;
	unsigned short	d_reclen;
	char		d_name[];
};

struct sd_buf {
	struct dir_context		ctx;
	void __user			*current_dir;
	int				prev_reclen;
	unsigned int			count;
	int				error;
	bool				compat;
};

static SD_RET sd_filldir(struct dir_context *ctx, const char *name, int namlen,
			 loff_t offset, u64 ino, unsigned int d_type)
{
	struct sd_buf *buf = container_of(ctx, struct sd_buf, ctx);
	void __user *dirent, *prev;
	int reclen;
	int prev_reclen;

	if (susfs_dirent_should_hide(name, namlen, ino))
		return SD_GO;

	if (!susfs_dirent_name_ok(name, namlen)) {
		buf->error = -EIO;
		return SD_STOP;
	}
	if (buf->compat)
		reclen = ALIGN(offsetof(struct sd_dirent_compat, d_name) + namlen + 2,
				       sizeof(u32));
	else
		reclen = ALIGN(offsetof(struct sd_dirent64, d_name) + namlen + 1,
				       sizeof(u64));
	buf->error = -EINVAL;
	if ((unsigned int)reclen > buf->count)
		return SD_STOP;
	/* After the room check, where compat_filldir() has it: a first entry that both overflows
	 * the 32-bit d_ino and does not fit answers EINVAL there, and testing the ino first
	 * answered EOVERFLOW instead.  Any later entry hides the difference anyway, because the
	 * tail fixup overwrites buf->error with the byte count. */
	if (buf->compat && ino > (u64)U32_MAX) {
		buf->error = -EOVERFLOW;
		return SD_STOP;
	}
	prev_reclen = buf->prev_reclen;
	if (prev_reclen && signal_pending(current))
		return SD_STOP;

	dirent = buf->current_dir;
	prev = (void __user *)dirent - prev_reclen;

	/* Same order as filldir64(): the previous record learns this entry's position.  With
	 * prev_reclen 0 that write lands on this record and is overwritten by the next entry,
	 * or by the tail fixup below. */
	if (buf->compat) {
		if (put_user((u32)offset, &((struct sd_dirent_compat __user *)prev)->d_off) ||
		    put_user((u32)ino, &((struct sd_dirent_compat __user *)dirent)->d_ino) ||
		    put_user((unsigned short)reclen,
			     &((struct sd_dirent_compat __user *)dirent)->d_reclen) ||
		    copy_to_user((char __user *)dirent + offsetof(struct sd_dirent_compat, d_name),
				 name, namlen) ||
		    put_user((char)0,
			     (char __user *)dirent + offsetof(struct sd_dirent_compat, d_name) + namlen) ||
		    put_user((unsigned char)d_type, (char __user *)dirent + reclen - 1)) {
			buf->error = -EFAULT;
			return SD_STOP;
		}
	} else if (put_user(offset, &((struct sd_dirent64 __user *)prev)->d_off) ||
		   put_user(ino, &((struct sd_dirent64 __user *)dirent)->d_ino) ||
		   put_user((unsigned short)reclen,
			    &((struct sd_dirent64 __user *)dirent)->d_reclen) ||
		   put_user((unsigned char)d_type, &((struct sd_dirent64 __user *)dirent)->d_type) ||
		   copy_to_user((char __user *)dirent + offsetof(struct sd_dirent64, d_name),
				name, namlen) ||
		   put_user((char)0,
			    (char __user *)dirent + offsetof(struct sd_dirent64, d_name) + namlen)) {
		buf->error = -EFAULT;
		return SD_STOP;
	}

	buf->error = 0;
	buf->prev_reclen = reclen;
	buf->current_dir = (void __user *)dirent + reclen;
	buf->count -= reclen;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 16, 0)
	ctx->count -= reclen;		/* the field the lower layer reads; see the entry point */
#endif
	return SD_GO;
}

__nocfi long susfs_dirent_run(struct file *f, void __user *dirp,
			      unsigned int count, bool compat)
{
	struct sd_buf buf = {};
	bool pos_locked = false;
	long error;

	buf.ctx.actor = sd_filldir;
	buf.ctx.pos = 0;
	buf.current_dir = dirp;
	buf.count = count;
	buf.compat = compat;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 16, 0)
	/* 6.16 gave dir_context a `count` field: the kernel keeps the remaining room there (its own
	 * filldir64 decrements it) and the filesystems read it as the buffer-size hint - fuse does
	 * `clamp((unsigned int)ctx->count, PAGE_SIZE, max)`, overlayfs hands it to the lower layer.
	 * Left at 0 it means "no room": fuse silently clamps back to one page per call (the pre-6.16
	 * behaviour, measurably different from stock) and overlayfs passes 0 down.  Filled here and
	 * kept in step in sd_filldir(). */
	buf.ctx.count = (int)count;
#endif

	/* What fdget_pos() does for the real syscall.  Without it two threads reading one
	 * shared fd both start from the same f_pos and the listing repeats itself - measured
	 * at 414 duplicates in 846 records before this was here.  The kernel only takes the
	 * lock when file_count() > 1; taking it whenever the mode bit is set is the same lock
	 * in the same order (f_pos_lock, then i_rwsem inside iterate_dir), minus the fast path. */
	if (f->f_mode & FMODE_ATOMIC_POS) {
		mutex_lock(&f->f_pos_lock);
		pos_locked = true;
	}

	error = susfs_dirent_iterate_dir(f, &buf.ctx);
	if (error >= 0)
		error = buf.error;
	if (buf.prev_reclen) {
		void __user *last = (void __user *)buf.current_dir - buf.prev_reclen;
		int tail_error;

		if (buf.compat)
			tail_error = put_user((u32)buf.ctx.pos,
					      &((struct sd_dirent_compat __user *)last)->d_off);
		else
			tail_error = put_user((s64)buf.ctx.pos,
					      &((struct sd_dirent64 __user *)last)->d_off);
		if (tail_error)
			error = -EFAULT;
		else
			error = (long)count - (long)buf.count;
	}

	if (pos_locked)
		mutex_unlock(&f->f_pos_lock);
	return error;
}
