check() {
local dir="$1"
local args="$2"
[ -d "$dir" ] && git -C "$dir" pull || git clone --depth=1 $args
}

abort() {
echo "error $*"
exit 1
}

_patch() {
local a
patch $@
a=$(find . -type f -name '*.orig' -o -name '*.rej')
rm -f $a
}

check ".susfs" "https://gitlab.com/simonpunk/susfs4ksu -b gki-android12-5.10 .susfs"
check ".kp" "https://github.com/WildKernels/kernel_patches .kp"

sus_dir="$PWD/.susfs"
sus_ver=$(grep -E '^#define SUSFS_VERSION' $sus_dir/kernel_patches/include/linux/susfs.h | cut -d' ' -f3 | sed 's/"//g')
kp="$PWD/.kp/next/susfs_fix_patches/$sus_ver"
sus_patch="${sus_dir}/kernel_patches/KernelSU/10_enable_susfs_for_ksu.patch"

[ -d "kernel" ] || abort "kernel directory not found."
[ -d "$kp" ] || abort "susfs fix patches directory not found"

# start patching
_patch -p1 <$sus_patch

for p in $kp/*.patch; do
_patch -p1 --fuzz=3 <$p
done
