#!/bin/bash
# Test the Mac IPv4 module against Darwin C and native sockets.
# D3.8, D9.8, D13.2
set -eu

if [ "$#" -ne 0 ] || [ "$(uname -sm)" != "Darwin arm64" ]; then
    echo "mac net: requires a Mac arm64 host and no arguments" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$root"
vm_slot=${FORT_VM_SLOT:-2}
version=$(sw_vers -productVersion)
if [[ "$version" =~ ^[0-9]+\.[0-9]+$ ]]; then
    version="$version.0"
fi
if ! [[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "mac net: unsupported Mac version '$version'" >&2
    exit 2
fi
target="arm64-apple-macosx$version"

mkdir -p build
work=$(mktemp -d "$root/build/mac-net.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
guest_work="build/$(basename "$work")"
mkdir "$work/linux" "$work/mac"
cp std/*.ft "$work/linux/"
cp std/*.ft "$work/mac/"
cp std/mac/libc.ft "$work/mac/libc.ft"
cp std/mac/net.ft "$work/mac/net.ft"
cmp std/libc.ft "$work/linux/libc.ft"
cmp std/net.ft "$work/linux/net.ft"
cmp std/mac/libc.ft "$work/mac/libc.ft"
cmp std/mac/net.ft "$work/mac/net.ft"

cc=$(xcrun --sdk macosx --find clang)
sdk=$(xcrun --sdk macosx --show-sdk-path)
"$cc" -isysroot "$sdk" -std=c11 -Wall -Wextra -Werror \
    -o "$work/net-probe" test/mac/net_probe.c
"$work/net-probe" > "$work/probe.out"
printf '%s\n' 'AF_INET 2' 'SOCK_STREAM 1' 'SOL_SOCKET 65535' \
    'SO_REUSEADDR 4' 'INADDR_ANY 0' 'sizeof_sockaddr_in 16' \
    'sizeof_socklen_t 4' 'offset_len 0' 'offset_family 1' \
    'offset_port 2' 'offset_addr 4' > "$work/layout.expected"
sed -n '1,11p' "$work/probe.out" > "$work/layout.actual"
cmp "$work/layout.expected" "$work/layout.actual"

FORT_VM_SLOT="$vm_slot" tools/vm configure debug
FORT_VM_SLOT="$vm_slot" tools/vm build debug fort_stage2
FORT_VM_SLOT="$vm_slot" tools/vm run python3 tools/fort_lint.py \
    --fort build/debug/stage2/fort --std-dir "$guest_work/mac" \
    std/mac/net.ft test/mac/net_layout.ft test/mac/net_errors.ft
FORT_VM_SLOT="$vm_slot" tools/vm run build/debug/stage2/fort --check \
    --std-dir "$guest_work/linux" test/lang/run/stdlib/117_net_loopback.ft
tests=(net_layout net_errors 117_net_loopback 119_net_stream)
count=0
for name in "${tests[@]}"; do
    case "$name" in
    net_*) source="test/mac/$name.ft" ;;
    *) source="test/lang/run/stdlib/$name.ft" ;;
    esac
    FORT_VM_SLOT="$vm_slot" tools/vm run build/debug/stage2/fort -S --target "$target" \
        --std-dir "$guest_work/mac" -o "$guest_work/$name.ll" "$source"
    if [ "$name" = net_layout ]; then
        "$cc" -isysroot "$sdk" -O1 -fPIE -Wl,-pie -Wno-override-module \
            -DFORT_NET_LAYOUT_HELPER -o "$work/$name" "$work/$name.ll" \
            test/mac/net_probe.c
    else
        "$cc" -isysroot "$sdk" -O1 -fPIE -Wl,-pie -Wno-override-module \
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
    *)
        sed -n 's@^//| @@p' "$source" > "$work/$name.expected"
        ;;
    esac
    if ! cmp -s "$work/$name.expected" "$work/$name.actual"; then
        echo "mac net: $name output differs" >&2
        diff -u "$work/$name.expected" "$work/$name.actual" >&2 || true
        exit 1
    fi
    count=$((count + 1))
done
echo "mac net: $count programs pass; 5 constants; 4 offsets; 2 target roots"
