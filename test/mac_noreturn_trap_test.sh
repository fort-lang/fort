#!/bin/bash
# D11.4, D19.7: a returning noreturn extern reaches llvm.trap on Mac.
set -eu

if [ "$#" -ne 1 ] || [ "$(uname -sm)" != "Darwin arm64" ]; then
    echo "usage: mac_noreturn_trap_test.sh <native fort> on Mac arm64" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$root"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
ulimit -c 0
cc=$(xcrun --sdk macosx --find clang)
sdk=$(xcrun --sdk macosx --show-sdk-path)
"$1" --std-dir build/mac-native/std --cc "$cc" \
    -Xcc -isysroot -Xcc "$sdk" -Xcc test/lang/ffi/helpers.c \
    -o "$work/trap" test/lang/run/ffi/009_noreturn_returns_anyway.ft
status=0
"$work/trap" > "$work/out" 2> "$work/err" || status=$?
if [ "$status" -ne 133 ] || ! printf 'before\n' | cmp -s - "$work/out" ||
   [ -s "$work/err" ]; then
    echo "mac noreturn trap: expected SIGTRAP 133, 'before', and empty stderr" >&2
    exit 1
fi
echo "mac noreturn trap: SIGTRAP 133; 1 Linux SIGILL fixture has a Mac result"
