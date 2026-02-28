# Integrate for non-GKI devices

KernelSU Next can be integrated into non-GKI kernels and was backported to 4.14 and earlier versions.

Due to the fragmentation of non-GKI kernels, we don't have a universal way to build them; therefore, we cannot provide a non-GKI boot.img. However, you can build the kernel with KernelSU Next integrated on your own.

First, you should be able to build a bootable kernel from kernel source code. If the kernel isn't open source, then it is difficult to run KernelSU Next for your device.

If you're able to build a bootable kernel, there are two ways to integrate KernelSU Next into the kernel source code:

1. Automatically with `kprobe`
2. Manually

## Integrate with kprobe

KernelSU Next uses kprobe for its kernel hooks. If kprobe runs reliably on your kernel, we recommend integrating KernelSU Next this way.

First, add KernelSU Next to your kernel source tree:

```sh
curl -LSs "https://raw.githubusercontent.com/KernelSU-Next/KernelSU-Next/next/kernel/setup.sh" | bash -s legacy
```

Then, you should check if kprobe is enabled in your kernel config. If it isn't, add these configs to it:

```txt
CONFIG_KPROBES=y
CONFIG_KPROBE_EVENTS=y
CONFIG_KSU_KPROBE_HOOKS=y
CONFIG_KSU=y
```

Now, when you re-build your kernel, KernelSU Next should work correctly.

If you find that KPROBES is still not enabled, you can try enabling `CONFIG_MODULES`. If that doesn't solve the issue, use `make menuconfig` to search for other KPROBES dependencies.

However, if you encounter a bootloop after integrating KernelSU, this may indicate that the **kprobe is broken in your kernel**, which means that you should fix the kprobe bug or use another way.

::: tip HOW TO CHECK IF KPROBE IS BROKEN？
Comment out `ksu_sucompat_init()` and `ksu_ksud_init()` in `KernelSU/kernel/ksu.c`. If the device boots normally, kprobe may be broken.
:::

::: info HOW TO GET MODULE UMOUNT FEATURE WORKING ON PRE-GKI?
If your kernel is older than 5.9, you should backport `path_umount` to `fs/namespace.c`. This is required to get "Umount module" feature work correctly. If you don't backport `path_umount`, "Umount module" feature won't work. You can get more info on how to achieve this at the end of this page.
:::

## Manually modify the kernel source

If kprobe doesn't work on your kernel—either because of an upstream bug or because your kernel is older than 4.8—you can try the following approach:

First, add KernelSU Next to your kernel source tree:

```sh
curl -LSs "https://raw.githubusercontent.com/KernelSU-Next/KernelSU-Next/next/kernel/setup.sh" | bash -s legacy
```

Keep in mind that, on some devices, your defconfig may be located at `arch/arm64/configs` or in other cases, it may be at `arch/arm64/configs/vendor/your_defconfig`. Regardless of the defconfig you're using, make sure to enable `CONFIG_KSU` with `y` to enable or `n` to disable it. For example, if you choose to enable it, your defconfig should contain the following string:

```txt
# KernelSU Next
CONFIG_KSU=y
```

Next, add KernelSU Next calls to the kernel source. Below are some patches for reference:

::: code-group

```diff[exec.c]
diff --git a/fs/exec.c b/fs/exec.c
--- a/fs/exec.c
+++ b/fs/exec.c
--- a/fs/exec.c
+++ b/fs/exec.c
/*
 * sys_execve() executes a new program.
 */
+#ifdef CONFIG_KSU
+__attribute__((hot))
+extern int ksu_handle_execveat(int *fd,
+			struct filename **filename_ptr,
+			void *argv, void *envp, int *flags);
+#endif
+
static int do_execve_common(struct filename *filename,
				struct user_arg_ptr argv,
				struct user_arg_ptr envp)
{
	struct linux_binprm *bprm;
	struct file *file;
	struct files_struct *displaced;
	int retval;

	if (IS_ERR(filename))
		return PTR_ERR(filename);

+#ifdef CONFIG_KSU
+	ksu_handle_execveat((int *)AT_FDCWD, &filename, &argv, &envp, 0);
+#endif
	/*
	 * We move the actual failure in case of RLIMIT_NPROC excess from
	 * set*uid() to execve() because too many poorly written programs
```
```diff[open.c]
diff --git a/fs/open.c b/fs/open.c
--- a/fs/open.c
+++ b/fs/open.c
+#ifdef CONFIG_KSU
+__attribute__((hot)) 
+extern int ksu_handle_faccessat(int *dfd, const char __user **filename_user,
+				int *mode, int *flags);
+#endif
+
/*
 * access() needs to use the real uid/gid, not the effective uid/gid.
 * We do this by temporarily clearing all FS-related capabilities and
 * switching the fsuid/fsgid around to the real ones.
 */
SYSCALL_DEFINE3(faccessat, int, dfd, const char __user *, filename, int, mode)
{
	const struct cred *old_cred;
	struct cred *override_cred;
	struct path path;
	struct inode *inode;
	int res;
	unsigned int lookup_flags = LOOKUP_FOLLOW;
 
+#ifdef CONFIG_KSU
+	ksu_handle_faccessat(&dfd, &filename, &mode, NULL);
+#endif
+
 	if (mode & ~S_IRWXO)	/* where's F_OK, X_OK, W_OK, R_OK? */
 		return -EINVAL;
```
```diff[read_write.c]
--- a/fs/read_write.c
+++ b/fs/read_write.c
@@ -429,10 +429,19 @@ ssize_t kernel_read(struct file *file, void *buf, size_t count, loff_t *pos)
 }
 EXPORT_SYMBOL(kernel_read);
 
+#ifdef CONFIG_KSU
+extern bool ksu_vfs_read_hook __read_mostly;
+extern int ksu_handle_vfs_read(struct file **file_ptr, char __user **buf_ptr,
+			size_t *count_ptr, loff_t **pos);
+#endif
 ssize_t vfs_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
 {
 	ssize_t ret;
 
+#ifdef CONFIG_KSU 
+	if (unlikely(ksu_vfs_read_hook))
+		ksu_handle_vfs_read(&file, &buf, &count, &pos);
+#endif
 	if (!(file->f_mode & FMODE_READ))
 		return -EBADF;
 	if (!(file->f_mode & FMODE_CAN_READ)))
```
```diff[stat.c]
diff --git a/fs/stat.c b/fs/stat.c
--- a/fs/stat.c
+++ b/fs/stat.c
@@ -364,X +364,XX @@  
+#ifdef CONFIG_KSU
+extern void ksu_handle_newfstat_ret(unsigned int *fd, struct stat __user **statbuf_ptr);
+#if defined(__ARCH_WANT_STAT64) || defined(__ARCH_WANT_COMPAT_STAT64)
+extern void ksu_handle_fstat64_ret(unsigned long *fd, struct stat64 __user **statbuf_ptr); // optional
+#endif
+#endif
+
SYSCALL_DEFINE2(newfstat, unsigned int, fd, struct stat __user *, statbuf)
{
	struct kstat stat;
	int error = vfs_fstat(fd, &stat);

	if (!error)
		error = cp_new_stat(&stat, statbuf);

+#ifdef CONFIG_KSU
+	ksu_handle_newfstat_ret(&fd, &statbuf);
+#endif
	return error;

 
@@ -490,X +497,X @@
SYSCALL_DEFINE2(fstat64, unsigned long, fd, struct stat64 __user *, statbuf)
{
	struct kstat stat;
	int error = vfs_fstat(fd, &stat);

	if (!error)
		error = cp_new_stat64(&stat, statbuf);

+#ifdef CONFIG_KSU // for 32-bit
+	ksu_handle_fstat64_ret(&fd, &statbuf);
+#endif
	return error;
}
```
```diff[reboot.c]
--- a/kernel/reboot.c
+++ b/kernel/reboot.c
@@ -277,6 +277,11 @@ 
  *
  * reboot doesn't sync: do that yourself before calling this.
  */
+
+#ifdef CONFIG_KSU
+extern int ksu_handle_sys_reboot(int magic1, int magic2, unsigned int cmd, void __user **arg);
+#endif
+
SYSCALL_DEFINE4(reboot, int, magic1, int, magic2, unsigned int, cmd,
		void __user *, arg)
{
	struct pid_namespace *pid_ns = task_active_pid_ns(current);
	char buffer[256];
	int ret = 0;
 
+#ifdef CONFIG_KSU 
+	ksu_handle_sys_reboot(magic1, magic2, cmd, &arg);
+#endif
 	/* We only trust the superuser with rebooting the system. */
 	if (!ns_capable(pid_ns->user_ns, CAP_SYS_BOOT))
 		return -EPERM;
```
:::

You should find the five functions in kernel source:

1. `do_faccessat`, usually in `fs/open.c`
2. `do_execveat_common`, usually in `fs/exec.c`
3. `vfs_read`, usually in `fs/read_write.c`
4. `vfs_statx`, usually in `fs/stat.c`
5. `sys_reboot`, usually in `kernel/reboot.c`

If your kernel doesn't have the `vfs_statx` function, use `vfs_fstatat` instead:

```diff
diff --git a/fs/stat.c b/fs/stat.c
index 068fdbcc9e26..5348b7bb9db2 100644
--- a/fs/stat.c
+++ b/fs/stat.c
@@ -87,6 +87,8 @@ int vfs_fstat(unsigned int fd, struct kstat *stat)
 }
 EXPORT_SYMBOL(vfs_fstat);

+#ifdef CONFIG_KSU
+extern int ksu_handle_stat(int *dfd, const char __user **filename_user, int *flags);
+#endif
 int vfs_fstatat(int dfd, const char __user *filename, struct kstat *stat,
 		int flag)
 {
@@ -94,6 +96,8 @@ int vfs_fstatat(int dfd, const char __user *filename, struct kstat *stat,
 	int error = -EINVAL;
 	unsigned int lookup_flags = 0;
+   #ifdef CONFIG_KSU 
+	ksu_handle_stat(&dfd, &filename, &flag);
+   #endif
+
 	if ((flag & ~(AT_SYMLINK_NOFOLLOW | AT_NO_AUTOMOUNT |
 		      AT_EMPTY_PATH)) != 0)
 		goto out;
```

For kernels eariler than 4.17, if you cannot find `do_faccessat`, just go to the definition of the `faccessat` syscall and place the call there:

```diff
diff --git a/fs/open.c b/fs/open.c
index 2ff887661237..e758d7db7663 100644
--- a/fs/open.c
+++ b/fs/open.c
@@ -355,6 +355,9 @@ SYSCALL_DEFINE4(fallocate, int, fd, int, mode, loff_t, offset, loff_t, len)
 	return error;
 }

+#ifdef CONFIG_KSU
+extern int ksu_handle_faccessat(int *dfd, const char __user **filename_user, int *mode,
+			        int *flags);
+#endif
+
 /*
  * access() needs to use the real uid/gid, not the effective uid/gid.
  * We do this by temporarily clearing all FS-related capabilities and
@@ -370,6 +373,8 @@ SYSCALL_DEFINE3(faccessat, int, dfd, const char __user *, filename, int, mode)
 	int res;
 	unsigned int lookup_flags = LOOKUP_FOLLOW;
+   #ifdef CONFIG_KSU
+	ksu_handle_faccessat(&dfd, &filename, &mode, NULL);
+   #endif
+
 	if (mode & ~S_IRWXO)	/* where's F_OK, X_OK, W_OK, R_OK? */
 		return -EINVAL;
```

### Safe Mode

To enable KernelSU Next's built-in Safe Mode, you should modify the `input_handle_event` function in `drivers/input/input.c`:

::: tip
It's strongly recommended to enable this feature, it's very useful for preventing bootloops!
:::

```diff
diff --git a/drivers/input/input.c b/drivers/input/input.c
index 45306f9ef247..815091ebfca4 100755
--- a/drivers/input/input.c
+++ b/drivers/input/input.c
@@ -367,10 +367,13 @@ static int input_get_disposition(struct input_dev *dev,
 	return disposition;
 }

+#ifdef CONFIG_KSU
+extern bool ksu_input_hook __read_mostly;
+extern int ksu_handle_input_handle_event(unsigned int *type, unsigned int *code, int *value);
+#endif
+
 static void input_handle_event(struct input_dev *dev,
 			       unsigned int type, unsigned int code, int value)
 {
	int disposition = input_get_disposition(dev, type, code, &value);
+   #ifdef CONFIG_KSU
+	if (unlikely(ksu_input_hook))
+		ksu_handle_input_handle_event(&type, &code, &value);
+   #endif
 
 	if (disposition != INPUT_IGNORE_EVENT && type != EV_SYN)
 		add_input_randomness(type, code, value);
```

::: info ENTERING SAFE MODE ACCIDENTALLY?
If you're using manual integration and don't disable `CONFIG_KPROBES`, the user will be able to trigger Safe Mode by pressing the volume down button after booting! Therefore, if you're using manual integration, it's necessary to disable `CONFIG_KPROBES`!
:::

### Failed to execute `pm` in terminal?

You should modify `fs/devpts/inode.c`. Reference:

```diff
diff --git a/fs/devpts/inode.c b/fs/devpts/inode.c
index 32f6f1c68..d69d8eca2 100644
--- a/fs/devpts/inode.c
+++ b/fs/devpts/inode.c
@@ -602,6 +602,8 @@ struct dentry *devpts_pty_new(struct pts_fs_info *fsi, int index, void *priv)
        return dentry;
 }

+#ifdef CONFIG_KSU
+extern int ksu_handle_devpts(struct inode*);
+#endif
+
 /**
  * devpts_get_priv -- get private data for a slave
  * @pts_inode: inode of the slave
@@ -610,6 +612,7 @@ struct dentry *devpts_pty_new(struct pts_fs_info *fsi, int index, void *priv)
  */
 void *devpts_get_priv(struct dentry *dentry)
 {
+       #ifdef CONFIG_KSU
+       ksu_handle_devpts(dentry->d_inode);
+       #endif
        if (dentry->d_sb->s_magic != DEVPTS_SUPER_MAGIC)
                return NULL;
        return dentry->d_fsdata;
```

### How to backport path_umount

You can make the "Umount modules" feature work on pre-GKI kernels by manually backporting `path_umount` from 5.9. You can use this patch as reference:

```diff
--- a/fs/namespace.c
+++ b/fs/namespace.c
@@ -1739,6 +1739,39 @@ static inline bool may_mandlock(void)
 }
 #endif

+static int can_umount(const struct path *path, int flags)
+{
+	struct mount *mnt = real_mount(path->mnt);
+
+	if (flags & ~(MNT_FORCE | MNT_DETACH | MNT_EXPIRE | UMOUNT_NOFOLLOW))
+		return -EINVAL;
+	if (!may_mount())
+		return -EPERM;
+	if (path->dentry != path->mnt->mnt_root)
+		return -EINVAL;
+	if (!check_mnt(mnt))
+		return -EINVAL;
+	if (mnt->mnt.mnt_flags & MNT_LOCKED) /* Check optimistically */
+		return -EINVAL;
+	if (flags & MNT_FORCE && !capable(CAP_SYS_ADMIN))
+		return -EPERM;
+	return 0;
+}
+
+int path_umount(struct path *path, int flags)
+{
+	struct mount *mnt = real_mount(path->mnt);
+	int ret;
+
+	ret = can_umount(path, flags);
+	if (!ret)
+		ret = do_umount(mnt, flags);
+
+	/* we mustn't call path_put() as that would clear mnt_expiry_mark */
+	dput(path->dentry);
+	mntput_no_expire(mnt);
+	return ret;
+}
 /*
  * Now umount can handle mount points as well as block devices.
  * This is important for filesystems which use unnamed block devices.
```

Finally, build your kernel again, and KernelSU should work correctly.


Credits for supporting Legacy devices:
@sidex15 
@maxsteeel
@rifsxd