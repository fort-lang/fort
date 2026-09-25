#!/bin/bash
# Test std.net of the target against the C socket headers.
#
# test/net/net_probe.c prints the five socket constants, the size and the
# field offsets of `struct sockaddr_in`, and three errno values from the C
# headers. The fort programs under test/net print what std.net says, and a C
# helper reads a fort address through the C fields. The test also checks a
# corpus program against the root of the other target.
set -eu

if [ "$#" -ne 4 ]; then
    echo "usage: net_layout_test.sh <fort> <std-dir> <cc> <target-triple>" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd -P)
compiler=$(cd "$(dirname "$1")" && pwd -P)/$(basename "$1")
std=$(cd "$2" && pwd -P)
cc=$3
triple=$4
if [ ! -x "$compiler" ] || [ ! -f "$std/net.ft" ]; then
    echo "net: compiler or standard root is missing" >&2
    exit 2
fi
case "$triple" in
arm64-apple-macosx*)
    target=darwin
    other=linux
    sdk=$(xcrun --sdk macosx --show-sdk-path)
    cc_args=(--target="$triple" -isysroot "$sdk")
    ;;
*)
    target=linux
    other=darwin
    cc_args=(--target="$triple")
    export QEMU_LD_PREFIX=${QEMU_LD_PREFIX:-/usr/x86_64-linux-gnu}
    ;;
esac

cd "$root"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
mkdir "$work/other"
bash tools/assemble_std.sh "$other" std "$work/other"

"$cc" "${cc_args[@]}" -std=c11 -Wall -Wextra -Werror \
    -o "$work/net-probe" test/net/net_probe.c
"$work/net-probe" > "$work/probe.out"
if [ "$(wc -l < "$work/probe.out" | tr -d ' ')" -ne 14 ]; then
    echo "net: the probe printed $(wc -l < "$work/probe.out") of 14 lines" >&2
    exit 1
fi

python3 tools/fort_lint.py --fort "$compiler" --std-dir "$std" \
    test/net/net_layout.ft test/net/net_errors.ft
# The corpus program of the other target's root must still pass `--check`:
# both roots declare the same std.net names.
"$compiler" --check --std-dir "$work/other" \
    test/lang/run/stdlib/117_net_loopback.ft
count=0
for name in net_layout net_errors; do
    source="test/net/$name.ft"
    "$compiler" -S --std-dir "$std" -o "$work/$name.ll" "$source"
    if [ "$name" = net_layout ]; then
        "$cc" "${cc_args[@]}" -O1 -fPIE -Wno-override-module \
            -DFORT_NET_LAYOUT_HELPER -o "$work/$name" "$work/$name.ll" \
            test/net/net_probe.c
    else
        "$cc" "${cc_args[@]}" -O1 -fPIE -Wno-override-module \
            -o "$work/$name" "$work/$name.ll"
    fi
    "$work/$name" < /dev/null > "$work/$name.actual"
    case "$name" in
    net_layout)
        sed -n '1,6p' "$work/probe.out" > "$work/$name.expected"
        printf 'field_values true\n' >> "$work/$name.expected"
        ;;
    net_errors)
        invalid=$(sed -n 's/^EINVAL //p' "$work/probe.out")
        refused=$(sed -n 's/^ECONNREFUSED //p' "$work/probe.out")
        not_socket=$(sed -n 's/^ENOTSOCK //p' "$work/probe.out")
        printf 'bad -1 %s\nrefused -1 %s\naccept -1 %s\nlocal_port true 7\nbind true\nrelease true\n' \
            "$invalid" "$refused" "$not_socket" > "$work/$name.expected"
        ;;
    esac
    if ! cmp -s "$work/$name.expected" "$work/$name.actual"; then
        echo "net: $name output differs" >&2
        diff -u "$work/$name.expected" "$work/$name.actual" >&2 || true
        exit 1
    fi
    count=$((count + 1))
done
echo "net: $target; $count programs pass; 5 constants; 1 size and 3 offsets; 3 errno values; 2 target roots"
