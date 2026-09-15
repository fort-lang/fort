#!/bin/bash
# Test core fort library programs on the Mac arm64 host.
# T-144: std/mac/libc.ft supplies the target C calls and constants.
set -eu

if [ "$#" -ne 0 ] || [ "$(uname -sm)" != "Darwin arm64" ]; then
    echo "mac core: requires a Mac arm64 host and no arguments" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$root"
version=$(sw_vers -productVersion)
if [[ "$version" =~ ^[0-9]+\.[0-9]+$ ]]; then
    version="$version.0"
fi
if ! [[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "mac core: unsupported Mac version '$version'" >&2
    exit 2
fi
target="arm64-apple-macosx$version"

mkdir -p "$root/build"
work=$(mktemp -d "$root/build/mac-core.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
guest_work="build/$(basename "$work")"
mkdir "$work/std"
for source in std/*.ft; do
    case "$source" in
    std/libc.ft|std/net.ft) ;;
    *) cp "$source" "$work/std/" ;;
    esac
done
cp std/mac/libc.ft "$work/std/libc.ft"

cc=$(xcrun --sdk macosx --find clang)
sdk=$(xcrun --sdk macosx --show-sdk-path)
"$cc" -isysroot "$sdk" -S -O0 -o "$work/open_tail.s" test/mac/open_tail_probe.c
if ! rg -q 'str[[:space:]]+x[0-9]+, \[x[0-9]+\]' "$work/open_tail.s"; then
    echo "mac core: C open mode has no stack store" >&2
    exit 1
fi
"$cc" -isysroot "$sdk" -E -dM -x c \
    -include fcntl.h -include errno.h -include unistd.h \
    /dev/null > "$work/macros.txt"
macro_pattern='^#define (O_RDONLY|O_WRONLY|O_RDWR|O_CREAT|O_TRUNC|O_APPEND|'
macro_pattern+='SEEK_SET|SEEK_CUR|SEEK_END|ENOENT|EINTR|EACCES|EINVAL) '
macro_count=$(rg -c "$macro_pattern" "$work/macros.txt")
if [ "$macro_count" -ne 13 ]; then
    echo "mac core: found $macro_count of 13 C constants" >&2
    exit 1
fi

FORT_VM_SLOT=2 tools/vm configure debug
FORT_VM_SLOT=2 tools/vm build debug fort_stage2
FORT_VM_SLOT=2 tools/vm build debug fort_mac_platform
tests=(004_libc_alloc 005_libc_file 019_sys_args_env 051_io_write_read_file \
       053_io_open_errors 070_rt_alloc_free 071_rt_args \
       074_rt_errno_preserved 094_rt_print_floats 095_rt_round_trip_floats)
count=0
for name in "${tests[@]}"; do
    source="test/lang/run/stdlib/$name.ft"
    FORT_VM_SLOT=2 tools/vm run build/debug/stage2/fort -S --target "$target" \
        --std-dir "$guest_work/std" -o "$guest_work/$name.ll" "$source"
    link=$(sed -n 's@^//! link: @@p' "$source")
    if [ -n "$link" ]; then
        "$cc" -isysroot "$sdk" -O1 -fPIE -Wl,-pie -Wno-override-module \
            -o "$work/$name" "$work/$name.ll" "test/lang/$link"
    else
        "$cc" -isysroot "$sdk" -O1 -fPIE -Wl,-pie -Wno-override-module \
            -o "$work/$name" "$work/$name.ll"
    fi
    sed -n 's@^//| @@p' "$source" > "$work/$name.expected"
    args=$(sed -n 's@^//! args: @@p' "$source")
    if [ -n "$args" ]; then
        read -r -a argv <<< "$args"
        (cd "$work" && "./$name" "${argv[@]}") > "$work/$name.actual"
    else
        (cd "$work" && "./$name") > "$work/$name.actual"
    fi
    if ! cmp -s "$work/$name.expected" "$work/$name.actual"; then
        echo "mac core: $name output differs" >&2
        diff -u "$work/$name.expected" "$work/$name.actual" >&2 || true
        exit 1
    fi
    count=$((count + 1))
done

FORT_VM_SLOT=2 tools/vm run build/debug/stage2/fort -S --target "$target" \
    --std-dir "$guest_work/std" -I src/fort -o "$guest_work/fort.ll" \
    build/debug/mac-platform/main.ft
"$cc" -isysroot "$sdk" -S -O0 -o "$work/fort.s" "$work/fort.ll"
if ! rg --pcre2 -U -q \
    'mov[ \t]+x([0-9]+), sp\n[ \t]*str[ \t]+x[0-9]+, \[x\1\]\n[ \t]*bl[ \t]+_fcntl' \
    "$work/fort.s"; then
    echo "mac core: compiler fcntl tail has no stack store" >&2
    exit 1
fi
"$cc" -isysroot "$sdk" -O1 -fPIE -Wl,-pie -Wno-override-module \
    -o "$work/fort" "$work/fort.ll"
mkdir "$work/alias"
ln -s ../fort "$work/alias/fort"
unset FORT_STD_DIR
"$work/alias/fort" --check test/lang/run/stdlib/053_io_open_errors.ft
"$work/alias/fort" --cc "$cc" -Xcc '-isysroot' -Xcc "$sdk" \
    -o "$work/cc_native" test/lang/run/stdlib/004_libc_alloc.ft
"$work/cc_native" > "$work/cc_native.actual"
if ! cmp -s "$work/004_libc_alloc.expected" "$work/cc_native.actual"; then
    echo "mac core: native compiler --cc output differs" >&2
    exit 1
fi
if [ "$("$work/alias/fort" --version)" != "fort 0.1.0" ]; then
    echo "mac core: native compiler version differs" >&2
    exit 1
fi
echo "mac core: $count programs pass; 13 C constants; open and fcntl tail stack slots; native compiler path and --cc"
