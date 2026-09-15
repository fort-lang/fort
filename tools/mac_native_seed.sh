#!/bin/bash
# mac_native_seed.sh: build native Mac stage2 from the Linux VM seed.
#
# The Linux pinned chain builds the seed in slot 2. The seed emits Mac IR with
# the explicit Mac standard root and target. Apple clang links that IR.
# D14.1, D14.3, D19.1
set -eu

if [ "$#" -ne 1 ] || [ "$(uname -sm)" != "Darwin arm64" ]; then
    echo "usage: mac_native_seed.sh <Mac-build-dir> on a Mac arm64 host" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd -P)
build=$(cd "$1" && pwd -P)
case "$build" in
"$root"/*) ;;
*) echo "mac_native_seed.sh: build directory is outside $root" >&2; exit 2 ;;
esac
relative=${build#"$root"/}
cd "$root"

platform="$build/mac-platform/platform.ft"
target=$(sed -n 's/^string BUILT_TARGET = "\(.*\)";$/\1/p' "$platform")
if ! [[ "$target" =~ ^arm64-apple-macosx[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "mac_native_seed.sh: invalid Mac target '$target'" >&2
    exit 2
fi

seed=build/debug/stage2/fort
module="$build/stage2.ll"
binary="$build/fort"
vm_slot=${FORT_VM_SLOT:-2}
if [ ! -f build/debug/CMakeCache.txt ]; then
    FORT_VM_SLOT="$vm_slot" tools/vm configure debug
fi
FORT_VM_SLOT="$vm_slot" tools/vm build debug fort_stage2
if [ ! -s "$seed" ]; then
    echo "mac_native_seed.sh: the VM wrote no Linux stage2 seed" >&2
    exit 1
fi

rm -f -- "$module" "$binary"
FORT_VM_SLOT="$vm_slot" tools/vm run "$seed" -S --target "$target" \
    --std-dir "$relative/std" -I src/fort -o "$relative/stage2.ll" \
    "$relative/mac-platform/main.ft"
if [ ! -s "$module" ] || ! rg -q "^target triple = \"$target\"$" "$module"; then
    echo "mac_native_seed.sh: the seed wrote no Apple target module" >&2
    exit 1
fi
FORT_VM_SLOT="$vm_slot" tools/vm run opt-18 -passes=verify -disable-output \
    "$relative/stage2.ll"

cc=$(xcrun --sdk macosx --find clang)
sdk=$(xcrun --sdk macosx --show-sdk-path)
"$cc" -isysroot "$sdk" --target="$target" -O1 -fPIE -Wl,-pie \
    -Wno-override-module \
    -o "$binary" "$module"
if [ ! -x "$binary" ]; then
    echo "mac_native_seed.sh: Apple clang wrote no Mac stage2 binary" >&2
    exit 1
fi
echo "Mac stage2: Linux seed built; Apple IR verified; native binary linked"
