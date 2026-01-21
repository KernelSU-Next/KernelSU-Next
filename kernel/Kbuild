kernelsu-objs := ksu.o
kernelsu-objs += allowlist.o
kernelsu-objs += app_profile.o
kernelsu-objs += apk_sign.o
kernelsu-objs += sucompat.o
kernelsu-objs += throne_tracker.o
kernelsu-objs += pkg_observer.o
kernelsu-objs += setuid_hook.o
kernelsu-objs += kernel_umount.o
kernelsu-objs += supercalls.o
kernelsu-objs += su_mount_ns.o
kernelsu-objs += feature.o
kernelsu-objs += ksud.o
kernelsu-objs += seccomp_cache.o
kernelsu-objs += file_wrapper.o
kernelsu-objs += util.o
kernelsu-objs += extras.o

kernelsu-objs += extras.o

kernelsu-objs += selinux/selinux.o
kernelsu-objs += selinux/sepolicy.o
kernelsu-objs += selinux/rules.o
ccflags-y += -I$(srctree)/security/selinux -I$(srctree)/security/selinux/include
ccflags-y += -I$(objtree)/security/selinux -include $(srctree)/include/uapi/asm-generic/errno.h

obj-$(CONFIG_KSU) += kernelsu.o

LPATH := /usr/bin/env PATH="$$PATH":/usr/bin:/usr/local/bin
MDIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Check if this is a git repository
# Try to detect Git repo intelligently
GIT_ROOT := $(shell cd $(MDIR) && $(LPATH) git rev-parse --show-toplevel 2>/dev/null)
ifneq ($(GIT_ROOT),)
KERNEL_GIT_ROOT := $(shell cd $(srctree) && $(LPATH) git rev-parse --show-toplevel 2>/dev/null)
ifneq ($(GIT_ROOT),$(KERNEL_GIT_ROOT))
# Only set version if it's a different repo from kernel
$(shell cd $(GIT_ROOT) && [ -f .git/shallow ] && $(LPATH) git fetch --unshallow 2>/dev/null || true)
KSU_GIT_VERSION := $(shell cd $(GIT_ROOT) && $(LPATH) git rev-list --count HEAD 2>/dev/null)
KSU_GIT_TAG := $(shell cd $(GIT_ROOT) && $(LPATH) git describe --tags --abbrev=0 2>/dev/null)
KSU_GIT_VERSION_VALID := 1
$(info -- KernelSU-Next Git repo detected at: $(GIT_ROOT))
endif
endif

# Calculate version if git version is available
ifdef KSU_GIT_VERSION_VALID
# ksu_version: major * 30000 + git version for historical reasons
$(eval KSU_VERSION=$(shell expr 30000 + $(KSU_GIT_VERSION)))
$(info -- KernelSU-Next version: $(KSU_VERSION))
ccflags-y += -DKSU_VERSION=$(KSU_VERSION)
else
# If there is no .git directory, use default version
$(warning "KSU_GIT_VERSION not defined! It is better to make KernelSU-Next a git repository!")
KSU_VERSION_FALLBACK := 1
$(info -- KernelSU-Next version fallback: $(KSU_VERSION_FALLBACK))
ccflags-y += -DKSU_VERSION=$(KSU_VERSION_FALLBACK)
endif

ifdef KSU_GIT_VERSION_VALID
$(eval KSU_VERSION_TAG=$(KSU_GIT_TAG))
$(info -- KernelSU-Next tag: $(KSU_VERSION_TAG))
ccflags-y += -DKSU_VERSION_TAG=\"$(KSU_VERSION_TAG)\"
else
$(warning "KSU_VERSION_TAG not defined! It is better to make KernelSU-Next a git submodule!")
KSU_VERSION_TAG_FALLBACK := v0.0.1
$(info -- KernelSU-Next tag fallback: $(KSU_VERSION_TAG_FALLBACK))
ccflags-y += -DKSU_VERSION_TAG=\"$(KSU_VERSION_TAG_FALLBACK)\"
endif

ifndef KSU_NEXT_MANAGER_SIZE
KSU_NEXT_MANAGER_SIZE := 0x3e6
endif

ifndef KSU_NEXT_MANAGER_HASH
KSU_NEXT_MANAGER_HASH := 79e590113c4c4c0c222978e413a5faa801666957b1212a328e46c00c69821bf7
endif

ifdef KSU_MANAGER_PACKAGE
ccflags-y += -DKSU_MANAGER_PACKAGE=\"$(KSU_MANAGER_PACKAGE)\"
$(info -- KernelSU-Next Manager package name: $(KSU_MANAGER_PACKAGE))
endif

$(info -- KernelSU-Next Manager signature size: $(KSU_NEXT_MANAGER_SIZE))
$(info -- KernelSU-Next Manager signature hash: $(KSU_NEXT_MANAGER_HASH))

ccflags-y += -DEXPECTED_MANAGER_SIZE=$(KSU_NEXT_MANAGER_SIZE)
ccflags-y += -DEXPECTED_MANAGER_HASH=\"$(KSU_NEXT_MANAGER_HASH)\"

ccflags-y += -Wno-strict-prototypes -Wno-int-conversion -Wno-gcc-compat -Wno-missing-prototypes
ccflags-y += -Wno-declaration-after-statement -Wno-unused-function -Wno-unused-variable

## For susfs stuff ##
ifeq ($(shell test -e $(srctree)/fs/susfs.c; echo $$?),0)
$(eval SUSFS_VERSION=$(shell cat $(srctree)/include/linux/susfs.h | grep -E '^#define SUSFS_VERSION' | cut -d' ' -f3 | sed 's/"//g'))
$(info )
$(info -- SUSFS_VERSION: $(SUSFS_VERSION))
else
$(info -- You have not integrated susfs in your kernel yet.)
$(info -- Read: https://gitlab.com/simonpunk/susfs4ksu)
endif

# Keep a new line here!! Because someone may append config
