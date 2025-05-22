kernelsu-objs := ksu.o
kernelsu-objs += allowlist.o
kernelsu-objs += apk_sign.o
kernelsu-objs += sucompat.o
kernelsu-objs += throne_tracker.o
kernelsu-objs += core_hook.o
kernelsu-objs += ksud.o
kernelsu-objs += embed_ksud.o
kernelsu-objs += kernel_compat.o

kernelsu-objs += selinux/selinux.o
kernelsu-objs += selinux/sepolicy.o
kernelsu-objs += selinux/rules.o
ccflags-y += -I$(srctree)/security/selinux -I$(srctree)/security/selinux/include
ccflags-y += -I$(objtree)/security/selinux -include $(srctree)/include/uapi/asm-generic/errno.h

obj-$(CONFIG_KSU) += kernelsu.o

# .git is a text file while the module is imported by 'git submodule add'.
ifeq ($(shell test -e $(srctree)/$(src)/../.git; echo $$?),0)
$(shell cd $(srctree)/$(src); /usr/bin/env PATH="$$PATH":/usr/bin:/usr/local/bin [ -f ../.git/shallow ] && git fetch --unshallow)
KSU_GIT_VERSION := $(shell cd $(srctree)/$(src); /usr/bin/env PATH="$$PATH":/usr/bin:/usr/local/bin git rev-list --count HEAD)
# ksu_version: major * 10000 + git version + 200 for historical reasons
$(eval KSU_VERSION=$(shell expr 10000 + $(KSU_GIT_VERSION) + 198))
$(info -- KernelSU-Next version: $(KSU_VERSION))
ccflags-y += -DKSU_VERSION=$(KSU_VERSION)
else # If there is no .git file, the default version will be passed.
$(warning "KSU_GIT_VERSION not defined! It is better to make KernelSU-Next a git submodule!")
ccflags-y += -DKSU_VERSION=11998
endif

ifeq ($(shell grep -q " current_sid(void)" $(srctree)/security/selinux/include/objsec.h; echo $$?),0)
ccflags-y += -DKSU_COMPAT_HAS_CURRENT_SID
endif

ifeq ($(shell grep -q "struct selinux_state " $(srctree)/security/selinux/include/security.h; echo $$?),0)
ccflags-y += -DKSU_COMPAT_HAS_SELINUX_STATE
endif

ifeq ($(shell grep -q "strncpy_from_user_nofault" $(srctree)/include/linux/uaccess.h; echo $$?),0)
ccflags-y += -DKSU_STRNCPY_FROM_USER_NOFAULT
endif

ifeq ($(shell grep -q "ssize_t kernel_read" $(srctree)/fs/read_write.c; echo $$?),0)
ccflags-y += -DKSU_KERNEL_READ
endif

ifeq ($(shell grep "ssize_t kernel_write" $(srctree)/fs/read_write.c | grep -q "const void" ; echo $$?),0)
ccflags-y += -DKSU_KERNEL_WRITE
endif

ifndef KSU_NEXT_EXPECTED_SIZE
KSU_NEXT_EXPECTED_SIZE := 0x3e6
endif

ifndef KSU_NEXT_EXPECTED_HASH
KSU_NEXT_EXPECTED_HASH := 79e590113c4c4c0c222978e413a5faa801666957b1212a328e46c00c69821bf7
endif

ifdef KSU_MANAGER_PACKAGE
ccflags-y += -DKSU_MANAGER_PACKAGE=\"$(KSU_MANAGER_PACKAGE)\"
$(info -- KernelSU-Next Manager package name: $(KSU_MANAGER_PACKAGE))
endif

$(info -- KernelSU-Next Manager signature size: $(KSU_NEXT_EXPECTED_SIZE))
$(info -- KernelSU-Next Manager signature hash: $(KSU_NEXT_EXPECTED_HASH))

ccflags-y += -DEXPECTED_NEXT_SIZE=$(KSU_NEXT_EXPECTED_SIZE)
ccflags-y += -DEXPECTED_NEXT_HASH=\"$(KSU_NEXT_EXPECTED_HASH)\"

ccflags-y += -DKSU_COMPAT_GET_CRED_RCU

ccflags-y += -DKSU_UMOUNT

# Determine the appropriate atomic function and apply patch accordingly
ifeq ($(shell grep -q "atomic_inc_not_zero" $(srctree)/kernel/cred.c; echo $$?),0)
ATOMIC_INC_FUNC = atomic_inc_not_zero
else ifeq ($(shell grep -q "atomic_long_inc_not_zero" $(srctree)/kernel/cred.c; echo $$?),0)
ATOMIC_INC_FUNC = atomic_long_inc_not_zero
else
$(info -- KSU_NEXT: Neither atomic_inc_not_zero nor atomic_long_inc_not_zero found in kernel/cred.c)
endif

# Inform which function is being patched
$(info -- KSU_NEXT: Using $(ATOMIC_INC_FUNC) in get_cred_rcu patch.)

# Add the get_cred_rcu function to cred.h if not already present
ifneq ($(shell grep -Eq "^static inline const struct cred \*get_cred_rcu" $(srctree)/include/linux/cred.h; echo $$?),0)
$(info -- KSU_NEXT: adding function 'static inline const struct cred *get_cred_rcu(const struct cred *cred);' to $(srctree)/include/linux/cred.h)
GET_CRED_RCU = static inline const struct cred *get_cred_rcu(const struct cred *cred)\n\
{\n\t\
        struct cred *nonconst_cred = (struct cred *) cred;\n\t\
        if (!cred)\n\t\t\
                return NULL;\n\t\
        if (!$(ATOMIC_INC_FUNC)(&nonconst_cred->usage))\n\t\t\
                return NULL;\n\t\
        validate_creds(cred);\n\t\
        return cred;\n\
}\n
$(shell grep -qF "$(GET_CRED_RCU)" $(srctree)/include/linux/cred.h || sed -i '/^static inline void put_cred/i $(GET_CRED_RCU)' $(srctree)/include/linux/cred.h)

# Modify get_task_cred in cred.c
$(info -- KSU_NEXT: modifying 'get_task_cred' function in $(srctree)/kernel/cred.c)
$(shell sed -i "s/!$(ATOMIC_INC_FUNC)(&((struct cred \*)cred)->usage)/!get_cred_rcu(cred)/g" $(srctree)/kernel/cred.c)
endif

ifneq ($(shell grep -Eq "^static int can_umount" $(srctree)/fs/namespace.c; echo $$?),0)
$(info -- KSU_NEXT: adding function 'static int can_umount(const struct path *path, int flags);' to $(srctree)/fs/namespace.c)
CAN_UMOUNT = static int can_umount(const struct path *path, int flags)\n\
{\n\t\
        struct mount *mnt = real_mount(path->mnt);\n\t\
        if (flags & ~(MNT_FORCE | MNT_DETACH | MNT_EXPIRE | UMOUNT_NOFOLLOW))\n\t\t\
                return -EINVAL;\n\t\
        if (!may_mount())\n\t\t\
                return -EPERM;\n\t\
        if (path->dentry != path->mnt->mnt_root)\n\t\t\
                return -EINVAL;\n\t\
        if (!check_mnt(mnt))\n\t\t\
                return -EINVAL;\n\t\
        if (mnt->mnt.mnt_flags & MNT_LOCKED)\n\t\t\
                return -EINVAL;\n\t\
        if (flags & MNT_FORCE && !capable(CAP_SYS_ADMIN))\n\t\t\
                return -EPERM;\n\t\
        return 0;\n\
}\n
$(shell sed -i '/^static bool is_mnt_ns_file/i $(CAN_UMOUNT)' $(srctree)/fs/namespace.c;)
endif

ifneq ($(shell grep -Eq "^int path_umount" $(srctree)/fs/namespace.c; echo $$?),0)
$(info -- KSU_NEXT: adding function 'int path_umount(struct path *path, int flags);' to $(srctree)/fs/namespace.c)
PATH_UMOUNT = int path_umount(struct path *path, int flags)\n\
{\n\t\
        struct mount *mnt = real_mount(path->mnt);\n\t\
        int ret;\n\t\
        ret = can_umount(path, flags);\n\t\
        if (!ret)\n\t\t\
                ret = do_umount(mnt, flags);\n\t\
        dput(path->dentry);\n\t\
        mntput_no_expire(mnt);\n\t\
        return ret;\n\
}\n
$(shell sed -i '/^static bool is_mnt_ns_file/i $(PATH_UMOUNT)' $(srctree)/fs/namespace.c;)
endif

ifneq ($(shell grep -Eq "^int path_umount" $(srctree)/fs/internal.h; echo $$?),0)
$(shell sed -i '/^extern void __init mnt_init/a int path_umount(struct path *path, int flags);' $(srctree)/fs/internal.h;)
$(info -- KSU_NEXT: adding 'int path_umount(struct path *path, int flags);' to $(srctree)/fs/internal.h)
endif

ccflags-y += -Wno-implicit-function-declaration -Wno-strict-prototypes -Wno-int-conversion -Wno-gcc-compat
ccflags-y += -Wno-declaration-after-statement -Wno-unused-function

## For non-gki compatiblity ##
ifeq ($(shell grep -q " current_sid(void)" $(srctree)/security/selinux/include/objsec.h; echo $$?),0)
ccflags-y += -DKSU_COMPAT_HAS_CURRENT_SID
endif

ifeq ($(shell grep -q "struct selinux_state " $(srctree)/security/selinux/include/security.h; echo $$?),0)
ccflags-y += -DKSU_COMPAT_HAS_SELINUX_STATE
endif

ccflags-y += -DKSU_UMOUNT
ifneq ($(shell grep -Eq "get_cred_rcu" $(srctree)/include/linux/cred.h; echo $$?),0)
$(info -- KSU_SUSFS: adding function 'static inline const struct cred *get_cred_rcu();' to $(srctree)/include/linux/cred.h)
GET_CRED_RCU = static inline const struct cred *get_cred_rcu(const struct cred *cred)\n\
{\n\t\
	struct cred *nonconst_cred = (struct cred *) cred;\n\t\
	if (!cred)\n\t\t\
		return NULL;\n\t\
	if (!atomic_inc_not_zero(&nonconst_cred->usage))\n\t\t\
		return NULL;\n\t\
	validate_creds(cred);\n\t\
	return cred;\n\
}\n
$(shell sed -i '/^static inline void put_cred/i $(GET_CRED_RCU)' $(srctree)/include/linux/cred.h;)
endif

ifneq ($(shell grep -Eq "^static int can_umount" $(srctree)/fs/namespace.c; echo $$?),0)
$(info -- KSU_SUSFS: adding function 'static int can_umount(const struct path *path, int flags);' to $(srctree)/fs/namespace.c)
CAN_UMOUNT = static int can_umount(const struct path *path, int flags)\n\
{\n\t\
        struct mount *mnt = real_mount(path->mnt);\n\t\
        if (flags & ~(MNT_FORCE | MNT_DETACH | MNT_EXPIRE | UMOUNT_NOFOLLOW))\n\t\t\
                return -EINVAL;\n\t\
        if (!may_mount())\n\t\t\
                return -EPERM;\n\t\
        if (path->dentry != path->mnt->mnt_root)\n\t\t\
                return -EINVAL;\n\t\
        if (!check_mnt(mnt))\n\t\t\
                return -EINVAL;\n\t\
        if (mnt->mnt.mnt_flags & MNT_LOCKED)\n\t\t\
                return -EINVAL;\n\t\
        if (flags & MNT_FORCE && !capable(CAP_SYS_ADMIN))\n\t\t\
                return -EPERM;\n\t\
        return 0;\n\
}\n
$(shell sed -i '/^static bool is_mnt_ns_file/i $(CAN_UMOUNT)' $(srctree)/fs/namespace.c;)
endif

ifneq ($(shell grep -Eq "^int path_umount" $(srctree)/fs/namespace.c; echo $$?),0)
$(info -- KSU_SUSFS: adding function 'int path_umount(struct path *path, int flags);' to $(srctree)/fs/namespace.c)
PATH_UMOUNT = int path_umount(struct path *path, int flags)\n\
{\n\t\
        struct mount *mnt = real_mount(path->mnt);\n\t\
        int ret;\n\t\
        ret = can_umount(path, flags);\n\t\
        if (!ret)\n\t\t\
                ret = do_umount(mnt, flags);\n\t\
        dput(path->dentry);\n\t\
        mntput_no_expire(mnt);\n\t\
        return ret;\n\
}\n
$(shell sed -i '/^static bool is_mnt_ns_file/i $(PATH_UMOUNT)' $(srctree)/fs/namespace.c;)
endif

ifneq ($(shell grep -Eq "^int path_umount" $(srctree)/fs/internal.h; echo $$?),0)
$(shell sed -i '/^extern void __init mnt_init/a int path_umount(struct path *path, int flags);' $(srctree)/fs/internal.h;)
$(info -- KSU_SUSFS: adding 'int path_umount(struct path *path, int flags);' to $(srctree)/fs/internal.h)
endif

## For susfs stuff ##
ifeq ($(shell test -e $(srctree)/fs/susfs.c; echo $$?),0)
$(eval SUSFS_VERSION=$(shell cat $(srctree)/include/linux/susfs.h | grep -E '^#define SUSFS_VERSION' | cut -d' ' -f3 | sed 's/"//g'))
$(info )
$(info -- SUSFS_VERSION: $(SUSFS_VERSION))
else
$(info -- You have not integrate susfs in your kernel.)
$(info -- Read: https://gitlab.com/simonpunk/susfs4ksu)
endif
# Keep a new line here!! Because someone may append config
