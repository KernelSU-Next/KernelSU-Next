#!/bin/sh

TMPDIR=/dev/tmp
rm -rf $TMPDIR
mkdir -p $TMPDIR 2>/dev/null

ARCH="$(getprop ro.product.cpu.abi)"
OUTFD=/proc/self/fd/$2

ui_print() {
  echo -e "ui_print $1\nui_print" >> $OUTFD
}

if [ ! "$ARCH" = "arm64-v8a" ]; then
    ui_print "- Unsupported architecture: $ARCH"
    exit 1
fi

# Extract ksud
unzip -o "$3" "lib/$ARCH/libksud.so" -d $TMPDIR >&2

KSUD="$TMPDIR/lib/$ARCH/libksud.so"

chmod 755 "$KSUD"

# use ksud to install or uninstall
case "$3" in
  *uninstall*|*Uninstall*)
    ui_print "- Uninstalling KernelSU-Next..."
    "$KSUD" uninstall 2>&1 | while read -r line; do
      ui_print "$line"
    done
    ;;
  *)
    ui_print "- Installing KernelSU-Next..."
    "$KSUD" boot-patch --flash 2>&1 | while read -r line; do
      ui_print "$line"
    done
    ;;
esac

true
