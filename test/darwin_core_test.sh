#!/bin/bash
# Test core fort library programs on the darwin arm64 host.
# T-144: std/darwin/libc.ft supplies the target C calls and constants.
set -eu

if [ "$#" -ne 0 ] || [ "$(uname -sm)" != "Darwin arm64" ]; then
    echo "darwin core: requires a darwin arm64 host and no arguments" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$root"
mkdir -p "$root/build"
work=$(mktemp -d "$root/build/darwin-core.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
compiler=build/darwin/stage2/fort
std=build/darwin/std
if [ ! -x "$compiler" ] || [ ! -f "$std/libc.ft" ]; then
    echo "darwin core: darwin compiler or standard root is missing" >&2
    exit 2
fi
ln -s "$root/$std" "$work/std"

cc=$(xcrun --sdk macosx --find clang)
sdk=$(xcrun --sdk macosx --show-sdk-path)
"$cc" -isysroot "$sdk" -S -O0 -o "$work/open_tail.s" test/darwin/open_tail_probe.c
if ! rg -q 'str[[:space:]]+x[0-9]+, \[x[0-9]+\]' "$work/open_tail.s"; then
    echo "darwin core: C open mode has no stack store" >&2
    exit 1
fi
"$cc" -isysroot "$sdk" -E -dM -x c \
    -include fcntl.h -include errno.h -include unistd.h \
    /dev/null > "$work/macros.txt"
macro_pattern='^#define (O_RDONLY|O_WRONLY|O_RDWR|O_CREAT|O_TRUNC|O_APPEND|'
macro_pattern+='SEEK_SET|SEEK_CUR|SEEK_END|ENOENT|EINTR|EACCES|EINVAL) '
macro_count=$(rg -c "$macro_pattern" "$work/macros.txt")
if [ "$macro_count" -ne 13 ]; then
    echo "darwin core: found $macro_count of 13 C constants" >&2
    exit 1
fi

tests=(004_libc_alloc 005_libc_file 019_sys_args_env 051_io_write_read_file \
       053_io_open_errors 070_rt_alloc_free 071_rt_args \
       074_rt_errno_preserved 094_rt_print_floats 095_rt_round_trip_floats)
count=0
for name in "${tests[@]}"; do
    source="test/lang/run/stdlib/$name.ft"
    "$compiler" -S --std-dir "$std" -o "$work/$name.ll" "$source"
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
        echo "darwin core: $name output differs" >&2
        diff -u "$work/$name.expected" "$work/$name.actual" >&2 || true
        exit 1
    fi
    count=$((count + 1))
done

"$compiler" -S --std-dir "$std" -I src/fort -o "$work/fort.ll" \
    build/darwin/bootstrap-head/entry/main.ft
"$cc" -isysroot "$sdk" -S -O0 -o "$work/fort.s" "$work/fort.ll"
if ! rg --pcre2 -U -q \
    'mov[ \t]+x([0-9]+), sp\n[ \t]*str[ \t]+x[0-9]+, \[x\1\]\n[ \t]*bl[ \t]+_fcntl' \
    "$work/fort.s"; then
    echo "darwin core: compiler fcntl tail has no stack store" >&2
    exit 1
fi
"$cc" -isysroot "$sdk" -O1 -fPIE -Wl,-pie -Wno-override-module \
    -o "$work/fort" "$work/fort.ll"
mkdir "$work/alias"
ln -s ../fort "$work/alias/fort"
unset FORT_STD_DIR
"$work/alias/fort" --check test/lang/run/stdlib/053_io_open_errors.ft
"$work/alias/fort" --cc "$cc" -Xcc '-isysroot' -Xcc "$sdk" \
    -o "$work/cc_host" test/lang/run/stdlib/004_libc_alloc.ft
"$work/cc_host" > "$work/cc_host.actual"
if ! cmp -s "$work/004_libc_alloc.expected" "$work/cc_host.actual"; then
    echo "darwin core: host compiler --cc output differs" >&2
    exit 1
fi
if [ "$("$work/alias/fort" --version)" != "fort 0.1.0" ]; then
    echo "darwin core: host compiler version differs" >&2
    exit 1
fi
echo "darwin core: $count programs pass; 13 C constants; open and fcntl tail stack slots; host compiler path and --cc"
