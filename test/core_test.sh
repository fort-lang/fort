#!/bin/bash
# Test the core library against the C headers and the toolchain of the target.
#
# The test compares 13 constants of std/<target>/libc.ft with the C headers,
# checks the variable-tail call form of `open` and `fcntl` in the assembly of
# the target, runs ten core library programs, and builds the compiler itself
# from its emitted module: that binary resolves its standard root through an
# executable symlink, accepts `--cc` with `-Xcc`, and reports its version.
set -eu

if [ "$#" -ne 4 ]; then
    echo "usage: core_test.sh <fort> <std-dir> <cc> <target-triple>" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd -P)
compiler=$(cd "$(dirname "$1")" && pwd -P)/$(basename "$1")
std=$(cd "$2" && pwd -P)
cc=$3
triple=$4
if [ ! -x "$compiler" ] || [ ! -f "$std/libc.ft" ]; then
    echo "core: compiler or standard root is missing" >&2
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

cd "$root"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
ln -s "$std" "$work/std"
# 053 prints the number of the first descriptor it opens, which is 3 only when
# the program inherits no other. ctest leaves one open on linux; close them.
for fd in $(seq 3 20); do
    eval "exec $fd>&-"
done

# The variable tail of a C call takes a form the fixed part does not. On
# arm64 darwin a variadic argument goes to the stack: `str` into the frame
# before `bl`. On x86-64 `%al` carries the vector register count: `movb $0,
# %al` before the call. `open_tail_probe.c` shows the form of the C compiler,
# and the compiler's own `fcntl` call must take the same form (D9.8).
case "$target" in
darwin)
    open_form='str[[:space:]]+x[0-9]+, \[x[0-9]+\]'
    fcntl_form='mov[ \t]+x([0-9]+), sp\n(?:[^\n]*\n){0,4}?[ \t]*str[ \t]+x[0-9]+, \[x\1\]\n(?:[^\n]*\n){0,4}?[ \t]*bl[ \t]+_fcntl'
    ;;
linux)
    open_form='movb[[:space:]]+\$0, %al'
    fcntl_form='movb[ \t]+\$0, %al\n(?:[^\n]*\n){0,4}?[ \t]*callq[ \t]+fcntl@PLT'
    ;;
esac
# matches_lines <pattern> <file>: a multi-line regular expression over the file.
matches_lines() {
    python3 -c 'import re, sys; sys.exit(0 if re.search(sys.argv[1], open(sys.argv[2]).read()) else 1)' "$1" "$2"
}
"$cc" "${cc_args[@]}" -S -O0 -o "$work/open_tail.s" test/core/open_tail_probe.c
if ! grep -Eq "$open_form" "$work/open_tail.s"; then
    echo "core: the C open mode does not take the variable-tail form of $target" >&2
    exit 1
fi

# The 13 constants: each C macro value must equal the fort constant of the
# assembled root. Both sides spell octal and hexadecimal differently, so the
# shell evaluates each as a number.
"$cc" "${cc_args[@]}" -E -dM -x c \
    -include fcntl.h -include errno.h -include unistd.h \
    /dev/null > "$work/macros.txt"
# c_number <literal>: a C integer literal as a bash arithmetic expression.
c_number() {
    local v=${1%[uUlL]}
    case "$v" in
    0x*|0X*) printf '%s' "$v" ;;
    0[0-7]*) printf '8#%s' "${v#0}" ;;
    *) printf '%s' "$v" ;;
    esac
}
# fort_number <literal>: a fort integer literal as a bash arithmetic expression.
fort_number() {
    case "$1" in
    0o*) printf '8#%s' "${1#0o}" ;;
    *) printf '%s' "$1" ;;
    esac
}
constant_count=0
for name in O_RDONLY O_WRONLY O_RDWR O_CREAT O_TRUNC O_APPEND \
            SEEK_SET SEEK_CUR SEEK_END ENOENT EINTR EACCES EINVAL; do
    c_value=$(sed -n "s/^#define $name \([^ ]*\).*/\1/p" "$work/macros.txt")
    fort_value=$(sed -n "s/^i32 $name = \([^;]*\);.*/\1/p" "$std/libc.ft")
    if [ -z "$c_value" ] || [ -z "$fort_value" ]; then
        echo "core: $name: C '$c_value', fort '$fort_value'" >&2
        exit 1
    fi
    if [ $(( $(c_number "$c_value") )) -ne $(( $(fort_number "$fort_value") )) ]; then
        echo "core: $name: C $c_value differs from fort $fort_value" >&2
        exit 1
    fi
    constant_count=$((constant_count + 1))
done

tests=(004_libc_alloc 005_libc_file 019_sys_args_env 051_io_write_read_file \
       053_io_open_errors 070_rt_alloc_free 071_rt_args \
       074_rt_errno_preserved 094_rt_print_floats 095_rt_round_trip_floats)
count=0
for name in "${tests[@]}"; do
    source="test/lang/run/stdlib/$name.ft"
    "$compiler" -S --std-dir "$std" -o "$work/$name.ll" "$source"
    link=$(sed -n 's@^//! link: @@p' "$source")
    if [ -n "$link" ]; then
        "$cc" "${cc_args[@]}" -O1 -fPIE -Wno-override-module \
            -o "$work/$name" "$work/$name.ll" "test/lang/$link"
    else
        "$cc" "${cc_args[@]}" -O1 -fPIE -Wno-override-module \
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
        echo "core: $name output differs" >&2
        diff -u "$work/$name.expected" "$work/$name.actual" >&2 || true
        exit 1
    fi
    count=$((count + 1))
done

"$compiler" -S --std-dir "$std" -I src/fort -o "$work/fort.ll" \
    src/fort/main.ft
"$cc" "${cc_args[@]}" -S -O0 -o "$work/fort.s" "$work/fort.ll"
if ! matches_lines "$fcntl_form" "$work/fort.s"; then
    echo "core: the compiler's fcntl tail does not take the variable-tail form of $target" >&2
    exit 1
fi
"$cc" "${cc_args[@]}" -O1 -fPIE -Wno-override-module \
    -o "$work/fort" "$work/fort.ll"
mkdir "$work/alias"
ln -s ../fort "$work/alias/fort"
unset FORT_STD_DIR
"$work/alias/fort" --check test/lang/run/stdlib/053_io_open_errors.ft
"$work/alias/fort" --cc "$cc" -Xcc -Wno-override-module \
    -o "$work/cc_host" test/lang/run/stdlib/004_libc_alloc.ft
"$work/cc_host" > "$work/cc_host.actual"
if ! cmp -s "$work/004_libc_alloc.expected" "$work/cc_host.actual"; then
    echo "core: the rebuilt compiler's --cc output differs" >&2
    exit 1
fi
if [ "$("$work/alias/fort" --version)" != "fort 0.1.0" ]; then
    echo "core: the rebuilt compiler's version differs" >&2
    exit 1
fi
echo "core: $target; $count programs pass; $constant_count C constants; open and fcntl variable tails; standard root through a symlink; --cc"
