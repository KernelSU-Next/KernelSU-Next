// SPDX-License-Identifier: GPL-2.0
/*
 * sus_dirent.c - answer getdents64 without ever writing a hidden name.
 *
 * sus_path_dirent_filter() runs after the syscall has filled the caller's buffer, and two
 * public detectors read that directly:
 *
 *   - a chunk whose records are all hidden has no honest answer, so the rewrite leaves one
 *     record with d_ino = 0 and an empty name.  A real filesystem never emits d_ino 0, and
 *     a 64-byte buffer puts one record per chunk, so the placeholder is handed over alone.
 *   - the rewrite is copy_to_user()/clear_user() over memory the caller still owns.  A second
 *     thread flipping it PROT_NONE and back faults the scrub, and the "filtering would be
 *     worse than not filtering" fallback then returns the chunk UNFILTERED.
 *
 * Both follow from rewriting the caller's buffer, so this layer does not rewrite it.  It runs
 * iterate_dir() with its own filldir, which simply does not emit a hidden name: nothing is
 * written, so there is nothing to scrub, nothing to fault, and an all-hidden stretch just
 * continues to the next entry inside the same call - which is what the kernel does anyway.
 *
 * iterate_dir() is EXPORT_SYMBOL and carries i_rwsem, f_pos, fsnotify and the LSM check, so
 * only filldir64()'s record layout is reimplemented here (fs/readdir.c).
 *
 * Both native and compat directory layouts are handled here.  This is deliberately shared by
 * native getdents64, compat getdents64 (which uses the same linux_dirent64 layout), and compat
 * getdents, so no ABI falls back to the post-hoc user-buffer scrub.
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/sched/signal.h>
#include <linux/string.h>
#include <linux/limits.h>	/* NAME_MAX/PATH_MAX: the name a filldir callback hands over */
#include <asm/ptrace.h>

#include "susfs.h"
#include "susfs_log.h"
#include "symbol_resolver.h"

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
	int				count;
	int				error;
	bool				compat;
};

static int (*sd_iterate_dir)(struct file *, struct dir_context *);
static struct file *(*sd_fget)(unsigned int);
static bool sd_armed;

static bool prefilter = true;
module_param(prefilter, bool, 0644);

static atomic_t n_sd_calls = ATOMIC_INIT(0);
static atomic_t n_sd_hidden = ATOMIC_INIT(0);

/* fs/readdir.c's verify_dirent_name(), which is static there - both halves of it: the length bound
 * is the one this used to drop, and the kernel applies the whole check before it writes anything,
 * which is why it is called before the copy below and not after. */
static bool sd_name_ok(const char *name, int len)
{
	return len > 0 && len < PATH_MAX && !memchr(name, '/', len);
}

static SD_RET sd_filldir(struct dir_context *ctx, const char *name, int namlen,
			 loff_t offset, u64 ino, unsigned int d_type)
{
	struct sd_buf *buf = container_of(ctx, struct sd_buf, ctx);
	void __user *dirent, *prev;
	int reclen;
	char nbuf[NAME_MAX + 1];
	int prev_reclen;

	/* A filldir callback hands over (pointer, length), NOT a C string: the NUL the kernel relies
	 * on is one IT appends in the CALLER's buffer (filldir64 -> unsafe_copy_dirent_name), and a
	 * filesystem that passes on raw reply bytes never had one - fuse does exactly that
	 * (`dir_emit(ctx, dirent->name, dirent->namelen, ...)` straight out of its reply buffer),
	 * which is how Android's /sdcard and /storage/emulated are served.  Handing that pointer to
	 * sus_path_dirent_hidden() means strcmp() reads past namlen, and whenever the byte after the
	 * name is not a NUL the comparison simply misses - i.e. the hidden name IS written to the
	 * caller, which is the one thing this layer exists to prevent.  Copy it out and terminate it
	 * first; a name that cannot be one (too long for NAME_MAX, or rejected by
	 * verify_dirent_name()) is not matched here and falls into the check below. */
	if (namlen > 0 && namlen <= NAME_MAX) {
		memcpy(nbuf, name, namlen);
		nbuf[namlen] = 0;
		if (sus_path_dirent_hidden(ino, nbuf)) {
			atomic_inc(&n_sd_hidden);
			return SD_GO;
		}
	}

	if (!sd_name_ok(name, namlen)) {
		buf->error = -EIO;
		return SD_STOP;
	}
	if (buf->compat) {
		if (ino > (u64)U32_MAX) {
			buf->error = -EOVERFLOW;
			return SD_STOP;
		}
		reclen = ALIGN(offsetof(struct sd_dirent_compat, d_name) + namlen + 2,
				       sizeof(u32));
	} else {
		reclen = ALIGN(offsetof(struct sd_dirent64, d_name) + namlen + 1,
				       sizeof(u64));
	}
	buf->error = -EINVAL;
	if (reclen > buf->count)
		return SD_STOP;
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

/*
 * fs/readdir.c's getdents/getdents64 implementation with sd_filldir in place of filldir64
 * or compat_filldir.
 * Returns false when this layer did not answer, and the caller falls back to the original
 * syscall plus the post-hoc filter - so hiding never stops, it only becomes detectable again.
 */
__nocfi bool susfs_dirent_prefilter(const struct pt_regs *regs, long *out,
					bool compat)
{
	unsigned int fd = (unsigned int)regs->regs[0];
	unsigned int count = (unsigned int)regs->regs[2];
	struct sd_buf buf = {};
	bool pos_locked = false;
	struct file *f;
	long error;

	if (!READ_ONCE(prefilter) || !sd_armed || !sd_iterate_dir || !sd_fget)
		return false;

	atomic_inc(&n_sd_calls);
	buf.ctx.actor = sd_filldir;
	buf.ctx.pos = 0;
	buf.current_dir = (void __user *)regs->regs[1];
	buf.count = (int)count;
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

	f = sd_fget(fd);
	if (!f) {
		*out = -EBADF;
		return true;
	}

	/* What fdget_pos() does for the real syscall.  Without it two threads reading one
	 * shared fd both start from the same f_pos and the listing repeats itself - measured
	 * at 414 duplicates in 846 records before this was here.  The kernel only takes the
	 * lock when file_count() > 1; taking it whenever the mode bit is set is the same lock
	 * in the same order (f_pos_lock, then i_rwsem inside iterate_dir), minus the fast path. */
	if (f->f_mode & FMODE_ATOMIC_POS) {
		mutex_lock(&f->f_pos_lock);
		pos_locked = true;
	}

	error = sd_iterate_dir(f, &buf.ctx);
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
			error = (long)count - buf.count;
	}

	if (pos_locked)
		mutex_unlock(&f->f_pos_lock);
	fput(f);

	*out = error;
	return true;
}

int susfs_sus_dirent_init(void)
{
	sd_iterate_dir = (void *)find_kernel_symbol_exact("iterate_dir");
	sd_fget = (void *)find_kernel_symbol_exact("fget");
	if (!sd_iterate_dir || !sd_fget) {
		pr_warn("sus_dirent: iterate_dir=%px fget=%px - staying off, the post-hoc filter keeps hiding\n",
			(void *)sd_iterate_dir, (void *)sd_fget);
		return 0;
	}
	sd_armed = true;
	SUSFS_LOGI("sus_dirent: armed, iterate_dir=%px fget=%px\n",
		   (void *)sd_iterate_dir, (void *)sd_fget);
	return 0;
}

void susfs_sus_dirent_exit(void)
{
	sd_armed = false;
	SUSFS_LOGI("sus_dirent: calls=%d hidden=%d\n",
		   atomic_read(&n_sd_calls), atomic_read(&n_sd_hidden));
}
