#!/bin/bash
# T-147: test the Mac C variadic call type and native printf values.
set -eu

if [ "$#" -ne 1 ] || [ "$(uname -sm)" != "Darwin arm64" ]; then
    echo "usage: mac_printf_test.sh <native fort> on Mac arm64" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$root"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
cc=$(xcrun --sdk macosx --find clang)
sdk=$(xcrun --sdk macosx --show-sdk-path)
"$1" -S --std-dir build/mac-native/std -o "$work/printf.ll" \
    test/mac/printf_variadic.ft
if ! rg -q 'call i32 \(ptr, \.\.\.\) @printf' "$work/printf.ll"; then
    echo "mac printf: no variadic LLVM call type" >&2
    exit 1
fi
"$1" --std-dir build/mac-native/std --cc "$cc" \
    -Xcc -isysroot -Xcc "$sdk" -o "$work/printf" test/mac/printf_variadic.ft
"$work/printf" > "$work/out"
printf 'n = 42 and -7\nprintf wrote 14 characters\n' > "$work/want"
if ! cmp -s "$work/out" "$work/want"; then
    echo "mac printf: native output differs" >&2
    diff -u "$work/want" "$work/out" >&2 || true
    exit 1
fi
echo "mac printf: variadic LLVM call and native values pass"
