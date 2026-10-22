# susfs - SUSFS loadable kernel module (LKM)
#
# Build against DDK kernel headers (same image that builds kernelsu.ko):
#   make -C $KDIR M=$PWD ARCH=arm64 CC=clang LLVM=1 LLVM_IAS=1 modules
#
# Only the shipping module is built, and this objs list is the whole of it: the probe/test
# modules that were used while working the hooks out are not in this tree any more.

obj-m += susfs_guard_lkm.o

susfs_guard_lkm-objs := susfs_main.o patch_memory.o symbol_resolver.o lsm_hook.o \
              susfs_uname.o susfs_kstat.o sus_map.o sus_path.o sus_mount.o \
              spoof_cmdline.o susfs_open_redirect.o susfs_enable_log.o \
              susfs_avc_spoof.o susfs_supercall.o susfs_hide_syms.o imports_guard.o \
              ksu_umount_gate.o ksu_allow_gate.o susfs_fp_hook.o susfs_opath.o susfs_or_entry.o \
              susfs_create_gate.o \
              fsnotify_lifetime.o btf_layout.o sus_dirent.o

# The dcache/icache flush helper is NOT picked at build time any more: which spelling a kernel
# carries is not decided by its version number (a vendor kernel can keep the pre-5.14 name, measured
# on a Samsung 5.x kernel), so kernel/patch_memory.c resolves it by name at run time.  Building with
# 5.14+ headers used to leave an undefined dcache_clean_inval_poc that such a kernel then refused to
# resolve, i.e. the module could not load at all.
#
# `srctree` is set only when kbuild reaches this file (make -C $KDIR M=$PWD ...).  The check for
# it used to be a parse-time $(error), which aborted make before any goal was chosen and so made
# the `all:` entry point below - and KDIR's default - unreachable.  It now lives in the `all:`
# recipe, i.e. it fires when the build is actually attempted.
KSU_SRCTREE_MISSING := $(if $(srctree),0,1)

# Match SukiSU's build flags: upstream code declares after statements and
# casts symbol addresses (unsigned long) to function pointers.
ccflags-y += -Wno-strict-prototypes -Wno-int-conversion -Wno-gcc-compat -Wno-missing-prototypes
ccflags-y += -Wno-declaration-after-statement -Wno-unused-function

# fs/mount.h (struct mount + real_mount) is a private header; its includes are
# all public, so -I$(srctree)/fs is enough to reach it via #include "mount.h".
ccflags-y += -I$(srctree)/fs

# Stack protector: arm64 got its per-task canary (read from sp_el0, no symbol involved) in 5.11,
# and the module inherits the kernel's mode.  Below that the kernel uses the global
# __stack_chk_guard, which the module then references - and that is the name `ksud insmod` cannot
# resolve (its kallsyms walk stops at the first record that is not three fields, while the bundled
# susfs_insmod reads the file to the end and does find it), so the load dies with
# "Unknown symbol __stack_chk_guard".  Nothing is lost where the kernel predates the canary: the
# guard would only protect this module's own stack.
KSU_KERNEL_HAS_PER_TASK_GUARD := $(shell [ "$(VERSION)" -gt 5 ] && echo yes)
ifeq ($(VERSION),5)
KSU_KERNEL_HAS_PER_TASK_GUARD := $(shell [ "$(PATCHLEVEL)" -ge 11 ] && echo yes)
endif
ifneq ($(KSU_KERNEL_HAS_PER_TASK_GUARD),yes)
ccflags-y += -fno-stack-protector
endif

# SELinux private headers are not needed: the LSM layer reaches SELinux through the live
# security_hook_heads / static_calls_table (see kernel/lsm_hook.c), not through ss/ or flask.h,
# so the -I lines that used to sit here "for when the feature lands" are gone with it.

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
ifeq ($(KSU_SRCTREE_MISSING),1)
	@echo "srctree is not set - build this module through kbuild: make -C \$${KDIR} M=\$${PWD} modules" >&2
	@exit 1
else
	$(MAKE) -C $(KDIR) M=$(PWD) modules
endif

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
