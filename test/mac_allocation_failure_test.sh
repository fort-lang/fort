#!/bin/bash
# mac_allocation_failure_test.sh: test allocation failures in native Mac code.
#
# Apple clang -O1 must keep a failed allocation even when code reads no storage.
# The test also covers three OOM, three size-overflow, and one successful fixture.
# D10.2, D11.4, D19.1
set -eu

if [ "$#" -ne 2 ] || [ "$(uname -sm)" != "Darwin arm64" ]; then
    echo "usage: mac_allocation_failure_test.sh <Mac-compiler> <Mac-std-root>" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd -P)
compiler=$(cd "$(dirname "$1")" && pwd -P)/$(basename "$1")
std=$(cd "$2" && pwd -P)
if [ ! -x "$compiler" ] || [ ! -f "$std/rt.ft" ] || [ ! -f "$std/libc.ft" ]; then
    echo "mac allocation: compiler or Mac standard root is missing" >&2
    exit 2
fi

version=$(sw_vers -productVersion)
if [[ "$version" =~ ^[0-9]+\.[0-9]+$ ]]; then
    version="$version.0"
fi
if ! [[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "mac allocation: unsupported Mac version '$version'" >&2
    exit 2
fi
target="arm64-apple-macosx$version"
cc=$(xcrun --sdk macosx --find clang)
sdk=$(xcrun --sdk macosx --show-sdk-path)

work=$(mktemp -d "${TMPDIR:-/tmp}/fort-t149.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
ulimit -c 0
cd "$root"

fixtures=(
    test/mac/allocation_unused.ft
    test/mac/allocation_unused.ft
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
    if [ "$source" = test/mac/allocation_unused.ft ]; then
        unused_count=$((unused_count + 1))
        if [ "$unused_count" -eq 2 ]; then
            name=allocation_unused_release
            mode=--release
        else
            name=allocation_unused_checked
        fi
    fi
    if [ "$mode" = --release ]; then
        "$compiler" --release -S --target "$target" --std-dir "$std" \
            -o "$work/$name.ll" "$source"
    else
        "$compiler" -S --target "$target" --std-dir "$std" \
            -o "$work/$name.ll" "$source"
    fi
    "$cc" --target="$target" -isysroot "$sdk" -O1 -fPIE -Wl,-pie \
        -Wno-override-module -o "$work/$name" "$work/$name.ll"
    status=0
    bash -c '"$1" > "$2" 2> "$3"' bash "$work/$name" \
        "$work/$name.out" "$work/$name.err" 2>/dev/null || status=$?
    if [ "$status" -ne 134 ]; then
        echo "mac allocation: $name exited $status, expected 134" >&2
        exit 1
    fi
    if [[ "$name" == allocation_unused* ]]; then
        if [ -s "$work/$name.out" ] || \
            ! printf '%s\n' \
                'test/mac/allocation_unused.ft:7:27: runtime error: out of memory' |
                cmp -s - "$work/$name.err"; then
            echo "mac allocation: unused storage did not report its source position" >&2
            exit 1
        fi
    else
        sed -n 's@^//| @@p' "$source" > "$work/$name.expected"
        if ! cmp -s "$work/$name.expected" "$work/$name.out"; then
            echo "mac allocation: $name stdout differs" >&2
            exit 1
        fi
        expected=$(sed -n 's@^//! stderr: @@p' "$source")
        if ! rg -F -q -- "$expected" "$work/$name.err" || \
            [ "$(wc -l < "$work/$name.err")" -ne 1 ]; then
            echo "mac allocation: $name stderr differs" >&2
            exit 1
        fi
    fi
    foreign_count=$(rg '^declare ' "$work/$name.ll" | rg -v '@llvm\.' | wc -l | tr -d ' ')
    protected_count=$(rg -c '^declare .* nobuiltin$' "$work/$name.ll" || true)
    protected_count=${protected_count:-0}
    if [ "$foreign_count" -ne "$protected_count" ] || \
        ! rg -q '^declare ptr @calloc\(i64, i64\) nobuiltin$' "$work/$name.ll"; then
        echo "mac allocation: $name protects $protected_count of $foreign_count externs" >&2
        exit 1
    fi
    foreign_total=$((foreign_total + foreign_count))
    case "$name" in
    *out_of_memory*|allocation_unused*)
        "$cc" --target="$target" -isysroot "$sdk" -O1 -S -emit-llvm \
            -Wno-override-module -o "$work/$name.optimized.ll" "$work/$name.ll"
        module=$(basename "$source" .ft)
        if ! awk -v target="$module.main" \
            '/^define / && (index($0, "@\"" target "\"(") ||
            index($0, "@" target "(")) { inside=1 }
            inside { print } inside && /^}/ { exit }' "$work/$name.optimized.ll" |
            rg -q 'call ptr @calloc\('; then
            echo "mac allocation: $name optimized fort main removed calloc" >&2
            exit 1
        fi
        oom_count=$((oom_count + 1))
        ;;
    *size_overflow*) overflow_count=$((overflow_count + 1)) ;;
    esac
done
if [ "$unused_count" -ne 2 ] || [ "$oom_count" -ne 5 ] || \
    [ "$overflow_count" -ne 3 ]; then
    echo "mac allocation: fixture count differs" >&2
    exit 1
fi
source=test/lang/run/stdlib/070_rt_alloc_free.ft
name=070_rt_alloc_free
"$compiler" -S --target "$target" --std-dir "$std" \
    -o "$work/$name.ll" "$source"
foreign_count=$(rg '^declare ' "$work/$name.ll" | rg -v '@llvm\.' | wc -l | tr -d ' ')
protected_count=$(rg -c '^declare .* nobuiltin$' "$work/$name.ll" || true)
protected_count=${protected_count:-0}
if [ "$foreign_count" -ne "$protected_count" ]; then
    echo "mac allocation: $name protects $protected_count of $foreign_count externs" >&2
    exit 1
fi
foreign_total=$((foreign_total + foreign_count))
"$cc" --target="$target" -isysroot "$sdk" -O1 -fPIE -Wl,-pie \
    -Wno-override-module -o "$work/$name" "$work/$name.ll"
"$work/$name" > "$work/$name.out" 2> "$work/$name.err"
sed -n 's@^//| @@p' "$source" > "$work/$name.expected"
if ! cmp -s "$work/$name.expected" "$work/$name.out" || \
    [ -s "$work/$name.err" ]; then
    echo "mac allocation: $name output differs" >&2
    exit 1
fi
echo "mac allocation: $unused_count unused modes, $oom_count OOM runs, "\
"$overflow_count overflow fixtures, 1 successful fixture, and "\
"$foreign_total protected extern lines pass at Apple -O1"
