#!/bin/bash
# mac_native_fixpoint_test.sh: prove the native Mac compiler fixed point.
#
# Each mode compares stage2 and stage3 IR, then stage3 and stage4 binaries.
# Both native links use one output path. The test copies stage3 before stage4.
# D19.1, D19.5
set -eu

if [ "$#" -ne 1 ] || [ "$(uname -sm)" != "Darwin arm64" ]; then
    echo "usage: mac_native_fixpoint_test.sh <Mac-build-dir> on Mac arm64" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd -P)
build=$(cd "$1" && pwd -P)
case "$build" in
"$root"/*) ;;
*) echo "mac_native_fixpoint_test.sh: build directory is outside $root" >&2; exit 2 ;;
esac
relative=${build#"$root"/}
cd "$root"

stage2="$relative/fort"
entry="$relative/mac-platform/main.ft"
std="$relative/std"
target=$(sed -n 's/^string BUILT_TARGET = "\(.*\)";$/\1/p' \
    "$relative/mac-platform/platform.ft")
if ! [[ "$target" =~ ^arm64-apple-macosx[0-9]+\.[0-9]+\.[0-9]+$ ]] || \
   [ ! -x "$stage2" ]; then
    echo "mac_native_fixpoint_test.sh: Mac stage2 or target is missing" >&2
    exit 2
fi
vm_slot=${FORT_VM_SLOT:-2}

verify_module() {
    local module=$1
    if [ ! -s "$module" ] || ! rg -q "^target triple = \"$target\"$" "$module"; then
        echo "mac native fixpoint: $module is no Apple target module" >&2
        return 1
    fi
    FORT_VM_SLOT="$vm_slot" tools/vm run opt-18 -passes=verify -disable-output \
        "$module"
}

check_mode() {
    local mode=$1
    shift
    local work="$relative/fixpoint/$mode"
    local link="$work/fort"
    local copy="$work/stage3.copy"
    local stage2_ll="$work/stage2.ll"
    local stage3_ll="$work/stage3.ll"
    mkdir -p "$work"
    rm -f -- "$link" "$copy" "$stage2_ll" "$stage3_ll"

    "$stage2" "$@" --std-dir "$std" -I src/fort -o "$link" "$entry"
    if [ ! -x "$link" ]; then
        echo "mac native fixpoint: $mode stage3 is missing" >&2
        return 1
    fi

    "$stage2" "$@" -S --std-dir "$std" -I src/fort \
        -o "$stage2_ll" "$entry"
    "$link" "$@" -S --std-dir "$std" -I src/fort \
        -o "$stage3_ll" "$entry"
    verify_module "$stage2_ll"
    verify_module "$stage3_ll"
    if ! cmp -s "$stage2_ll" "$stage3_ll"; then
        echo "mac native fixpoint: $mode stage2 and stage3 IR differ" >&2
        diff -u "$stage2_ll" "$stage3_ll" > "$work/module.diff" || true
        head -n 40 "$work/module.diff" >&2
        return 1
    fi

    cp "$link" "$copy"
    rm -f -- "$link"
    "$copy" "$@" --std-dir "$std" -I src/fort -o "$link" "$entry"
    if [ ! -x "$link" ] || ! cmp -s "$copy" "$link"; then
        echo "mac native fixpoint: $mode stage3 and stage4 binaries differ" >&2
        return 1
    fi
    echo "$mode: two Apple IR modules verify and match; two Mach-O binaries match"
}

check_mode checked
check_mode release --release
