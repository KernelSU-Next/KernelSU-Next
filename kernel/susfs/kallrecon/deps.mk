DEPS_LIB_NAME := KallRecon
DEPS_LIB_OBJS := lib/core.o lib/access.o lib/discover.o lib/symbol.o lib/slide.o lib/fastboot.o lib/hint.o lib/variant.o lib/anchor.o
ifdef KALLRECON_RAANA
RAANA_EXEC ?= multi
DEPS_LIB_OBJS += lib/raana/raana.o lib/raana/raana_task.o lib/raana/raana_run_$(RAANA_EXEC).o
endif
DEPS_LIB_INCS := lib
DEPS_LIB_DEPS :=
