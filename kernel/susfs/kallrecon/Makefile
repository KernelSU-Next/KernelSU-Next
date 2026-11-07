# one anchor object for both architectures, the variant is selected
# inside anchor.S
ANCHOR := lib/anchor.o

kallrecon-objs := src/main.o lib/core.o lib/access.o lib/discover.o lib/symbol.o lib/slide.o lib/fastboot.o lib/hint.o lib/variant.o $(ANCHOR) src/verify.o
test_probe-objs := test/test_main.o test/dbg.o test/variant_diff.o lib/core.o lib/access.o lib/discover.o lib/symbol.o lib/slide.o lib/fastboot.o lib/hint.o lib/variant.o $(ANCHOR)

ifeq ($(TARGET),test)
obj-m := test_probe.o
ccflags-y += -DKALLRECON_DEBUG
else
obj-m := kallrecon.o
endif

ifeq ($(CHECK),1)
ccflags-y += -DKALLRECON_CHECK
endif

ifeq ($(DEBUG),1)
ccflags-y += -DKALLRECON_DEBUG
endif

ifdef KALLRECON_MODULE_LOOKUP
ccflags-y += -DKALLRECON_MODULE_LOOKUP
endif

ifdef KALLRECON_VARIANT_FAST
ccflags-y += -DKALLRECON_VARIANT_FAST
endif

ifdef KALLRECON_VARIANT_BISECT
ccflags-y += -DKALLRECON_VARIANT_BISECT
endif

ifdef KALLRECON_FAST_BOOT
ccflags-y += -DKALLRECON_FAST_BOOT
endif

ifdef KALLRECON_FAST_BOOT_ALL
ccflags-y += -DKALLRECON_FAST_BOOT_ALL=1
endif

ifdef KALLRECON_NO_MARKERS
ccflags-y += -DKALLRECON_NO_MARKERS
endif

ifdef KALLRECON_FIRST_HIT
ccflags-y += -DKALLRECON_FIRST_HIT
endif

ifdef KALLRECON_SCAN_RANGE
ccflags-y += -DKALLRECON_SCAN_RANGE
endif

ifdef KALLRECON_RAANA
RAANA_EXEC ?= multi
ccflags-y += -DKALLRECON_RAANA
RAANA_OBJS := lib/raana/raana.o lib/raana/raana_task.o \
	lib/raana/raana_run_$(RAANA_EXEC).o
kallrecon-objs += $(RAANA_OBJS)
test_probe-objs += $(RAANA_OBJS)
endif

ifdef KALLRECON_RAANA_FALLBACK
ccflags-y += -DKALLRECON_RAANA_FALLBACK
endif

ifdef KALLRECON_RAANA_NUM
ccflags-y += -DKALLRECON_RAANA_NUM
endif

ifdef KALLRECON_NAMES_LOOSE
ccflags-y += -DKALLRECON_NAMES_LOOSE
endif

ccflags-y += -std=gnu11
ccflags-y += -Wno-declaration-after-statement
ccflags-y += -Wno-unused-variable
ccflags-y += -Wno-unused-function
ccflags-y += -Wno-strict-prototypes

KDIR := $(KDIR)
MDIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
ODIR := $(MDIR)/out/$(VER)

$(info -- KDIR: $(KDIR))
$(info -- MDIR: $(MDIR))
$(info -- ODIR: $(ODIR))

all:
	make -C $(KDIR) M=$(ODIR) src=$(MDIR) srcroot=$(MDIR) modules
clean:
	make -C $(KDIR) M=$(ODIR) src=$(MDIR) srcroot=$(MDIR) clean

$(obj)/%.o: $(src)/%.c $(recordmcount_source) FORCE
	$(call if_changed_rule,cc_o_c)
	$(call cmd,force_checksrc)

$(obj)/%.o: $(src)/%.S FORCE
	$(call if_changed_rule,as_o_S)
