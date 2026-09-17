#!/bin/bash
# Test the darwin IPv4 module against Darwin C and darwin sockets.
# D3.8, D9.8, D13.2
set -eu

if [ "$#" -ne 0 ] || [ "$(uname -s)" != "Darwin" ]; then
    echo "darwin net: requires a Darwin host and no arguments" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$root"
mkdir -p build
work=$(mktemp -d "$root/build/darwin-net.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
compiler=build/darwin/fort
if [ ! -x "$compiler" ]; then
    echo "darwin net: darwin compiler is missing" >&2
    exit 2
fi
mkdir "$work/linux" "$work/darwin"
bash tools/assemble_std.sh linux std "$work/linux"
bash tools/assemble_std.sh darwin std "$work/darwin"

cc=$(xcrun --sdk macosx --find clang)
sdk=$(xcrun --sdk macosx --show-sdk-path)
"$cc" -isysroot "$sdk" -std=c11 -Wall -Wextra -Werror \
    -o "$work/net-probe" test/darwin/net_probe.c
"$work/net-probe" > "$work/probe.out"
printf '%s\n' 'AF_INET 2' 'SOCK_STREAM 1' 'SOL_SOCKET 65535' \
    'SO_REUSEADDR 4' 'INADDR_ANY 0' 'sizeof_sockaddr_in 16' \
    'sizeof_socklen_t 4' 'offset_len 0' 'offset_family 1' \
    'offset_port 2' 'offset_addr 4' > "$work/layout.expected"
sed -n '1,11p' "$work/probe.out" > "$work/layout.actual"
cmp "$work/layout.expected" "$work/layout.actual"

python3 tools/fort_lint.py --fort "$compiler" --std-dir "$work/darwin" \
    std/darwin/net.ft test/darwin/net_layout.ft test/darwin/net_errors.ft
"$compiler" --check --std-dir "$work/linux" \
    test/lang/run/stdlib/117_net_loopback.ft
tests=(net_layout net_errors 117_net_loopback 119_net_stream)
count=0
for name in "${tests[@]}"; do
    case "$name" in
    net_*) source="test/darwin/$name.ft" ;;
    *) source="test/lang/run/stdlib/$name.ft" ;;
    esac
    "$compiler" -S --std-dir "$work/darwin" -o "$work/$name.ll" "$source"
    if [ "$name" = net_layout ]; then
        "$cc" -isysroot "$sdk" -O1 -fPIE -Wno-override-module \
            -DFORT_NET_LAYOUT_HELPER -o "$work/$name" "$work/$name.ll" \
            test/darwin/net_probe.c
    else
        "$cc" -isysroot "$sdk" -O1 -fPIE -Wno-override-module \
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
        echo "darwin net: $name output differs" >&2
        diff -u "$work/$name.expected" "$work/$name.actual" >&2 || true
        exit 1
    fi
    count=$((count + 1))
done
echo "darwin net: $count programs pass; 5 constants; 4 offsets; 2 target roots"
