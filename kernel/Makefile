KDIR := $(KDIR)
MDIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

$(info -- KDIR: $(KDIR))
$(info -- MDIR: $(MDIR))

.PHONY: all compdb clean format check-format

# preinit/runner/nolibc is vendored verbatim, keep it out of formatting
FORMAT_FILES = find . -path ./preinit/runner/nolibc -prune -o \( -name "*.c" -o -name "*.h" \) -print0

all: check_symbol
	make -C $(KDIR) M=$(MDIR) modules
	./check_symbol kernelsu.ko $(KDIR)/vmlinux

compdb:
	python3 $(MDIR)/.vscode/generate_compdb.py -O $(KDIR) $(MDIR)
clean:
	make -C $(KDIR) M=$(MDIR) clean
	rm check_symbol
check_symbol: tools/check_symbol.c
	$(CC) tools/check_symbol.c -o check_symbol
format:
	$(FORMAT_FILES) | xargs -0 clang-format -i
check-format:
	$(FORMAT_FILES) | xargs -0 clang-format --dry-run --Werror

# Keep a new line here!! Because someone may append config