#!/bin/bash
# Test allocation failures at the -O1 of the target clang.
#
# clang -O1 must keep a failed allocation even when code reads no storage.
# The test covers three OOM, three size-overflow, and one successful fixture.
# It also verifies runtime errors and emitted modules: every extern
# declaration carries `nobuiltin`, so the optimizer cannot fold `calloc` away.
set -eu

if [ "$#" -ne 4 ]; then
    echo "usage: allocation_failure_test.sh <fort> <std-dir> <cc> <target-triple>" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd -P)
compiler=$(cd "$(dirname "$1")" && pwd -P)/$(basename "$1")
std=$(cd "$2" && pwd -P)
cc=$3
triple=$4
if [ ! -x "$compiler" ] || [ ! -f "$std/rt.ft" ] || [ ! -f "$std/libc.ft" ]; then
    echo "allocation: compiler or standard root is missing" >&2
    exit 2
fi
case "$triple" in
arm64-apple-macosx*)
    target=darwin
    sdk=$(xcrun --sdk macosx --show-sdk-path)
    cc_args=(--target="$triple" -isysroot "$sdk")
    ;;
*)
    target=linux
    cc_args=(--target="$triple")
    export QEMU_LD_PREFIX=${QEMU_LD_PREFIX:-/usr/x86_64-linux-gnu}
    ;;
esac

work=$(mktemp -d "${TMPDIR:-/tmp}/fort-t149.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
ulimit -c 0
cd "$root"

# drop_qemu_notice <file>: qemu-user adds one line to stderr when a signal
# kills the program. Native execution prints nothing there.
drop_qemu_notice() {
    grep -v '^qemu: uncaught target signal' "$1" >"$1.clean" || true
    mv "$1.clean" "$1"
}

unused_fixture=test/core/allocation_unused.ft
fixtures=(
    "$unused_fixture"
    "$unused_fixture"
    test/lang/run/errors/012_out_of_memory.ft
    test/lang/run/modes/027_rt_alloc_out_of_memory_release.ft
    test/lang/run/stdlib/087_rt_alloc_out_of_memory.ft
    test/lang/run/errors/011_new_size_overflow.ft
    test/lang/run/modes/026_rt_alloc_size_overflow_release.ft
    test/lang/run/stdlib/086_rt_alloc_size_overflow.ft
)
oom_count=0
overflow_count=0
unused_count=0
foreign_total=0
for source in "${fixtures[@]}"; do
    name=$(basename "$source" .ft)
    mode=$(sed -n 's@^//! flags: @@p' "$source")
    if [ "$source" = "$unused_fixture" ]; then
        unused_count=$((unused_count + 1))
        if [ "$unused_count" -eq 2 ]; then
            name=allocation_unused_release
            mode=--release
        else
            name=allocation_unused_checked
        fi
    fi
    if [ "$mode" = --release ]; then
        "$compiler" --release -S --std-dir "$std" -o "$work/$name.ll" "$source"
    else
        "$compiler" -S --std-dir "$std" -o "$work/$name.ll" "$source"
    fi
    "$cc" "${cc_args[@]}" -O1 -fPIE \
        -Wno-override-module -o "$work/$name" "$work/$name.ll"
    status=0
    bash -c '"$1" > "$2" 2> "$3"' bash "$work/$name" \
        "$work/$name.out" "$work/$name.err" 2>/dev/null || status=$?
    if [ "$status" -ne 134 ]; then
        echo "allocation: $name exited $status, expected 134" >&2
        exit 1
    fi
    drop_qemu_notice "$work/$name.err"
    if [[ "$name" == allocation_unused* ]]; then
        if [ -s "$work/$name.out" ] || \
            ! printf '%s\n' \
                "$unused_fixture:6:27: runtime error: out of memory" |
                cmp -s - "$work/$name.err"; then
            echo "allocation: unused storage did not report its source position" >&2
            exit 1
        fi
    else
        sed -n 's@^//| @@p' "$source" > "$work/$name.expected"
        if ! cmp -s "$work/$name.expected" "$work/$name.out"; then
            echo "allocation: $name stdout differs" >&2
            exit 1
        fi
        expected=$(sed -n 's@^//! stderr: @@p' "$source")
        if ! grep -F -q -- "$expected" "$work/$name.err" || \
            [ "$(wc -l < "$work/$name.err")" -ne 1 ]; then
            echo "allocation: $name stderr differs" >&2
            exit 1
        fi
    fi
    foreign_count=$(grep '^declare ' "$work/$name.ll" | grep -v '@llvm\.' | wc -l | tr -d ' ')
    protected_count=$(grep -c '^declare .* nobuiltin$' "$work/$name.ll" || true)
    protected_count=${protected_count:-0}
    if [ "$foreign_count" -ne "$protected_count" ] || \
        ! grep -Eq '^declare ptr @calloc\(i64, i64\) nobuiltin$' "$work/$name.ll"; then
        echo "allocation: $name protects $protected_count of $foreign_count externs" >&2
        exit 1
    fi
    foreign_total=$((foreign_total + foreign_count))
    case "$name" in
    *out_of_memory*|allocation_unused*)
        "$cc" "${cc_args[@]}" -O1 -S -emit-llvm \
            -Wno-override-module -o "$work/$name.optimized.ll" "$work/$name.ll"
        module=$(basename "$source" .ft)
        if ! awk -v target="$module.main" \
            '/^define / && (index($0, "@\"" target "\"(") ||
            index($0, "@" target "(")) { inside=1 }
            inside { print } inside && /^}/ { exit }' "$work/$name.optimized.ll" |
            grep -Eq 'call ptr @calloc\('; then
            echo "allocation: $name optimized fort main removed calloc" >&2
            exit 1
        fi
        oom_count=$((oom_count + 1))
        ;;
    *size_overflow*) overflow_count=$((overflow_count + 1)) ;;
    esac
done
if [ "$unused_count" -ne 2 ] || [ "$oom_count" -ne 5 ] || \
    [ "$overflow_count" -ne 3 ]; then
    echo "allocation: fixture count differs" >&2
    exit 1
fi
source=test/lang/run/stdlib/070_rt_alloc_free.ft
name=070_rt_alloc_free
"$compiler" -S --std-dir "$std" -o "$work/$name.ll" "$source"
foreign_count=$(grep '^declare ' "$work/$name.ll" | grep -v '@llvm\.' | wc -l | tr -d ' ')
protected_count=$(grep -c '^declare .* nobuiltin$' "$work/$name.ll" || true)
protected_count=${protected_count:-0}
if [ "$foreign_count" -ne "$protected_count" ]; then
    echo "allocation: $name protects $protected_count of $foreign_count externs" >&2
    exit 1
fi
foreign_total=$((foreign_total + foreign_count))
"$cc" "${cc_args[@]}" -O1 -fPIE \
    -Wno-override-module -o "$work/$name" "$work/$name.ll"
"$work/$name" > "$work/$name.out" 2> "$work/$name.err"
sed -n 's@^//| @@p' "$source" > "$work/$name.expected"
if ! cmp -s "$work/$name.expected" "$work/$name.out" || \
    [ -s "$work/$name.err" ]; then
    echo "allocation: $name output differs" >&2
    exit 1
fi
echo "allocation: $target; $unused_count unused modes, $oom_count OOM runs," \
    "$overflow_count overflow fixtures, 1 successful fixture, and" \
    "$foreign_total protected extern lines pass at -O1"
