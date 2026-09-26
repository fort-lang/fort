#!/usr/bin/env bash
# The other target on this host: assemble its standard root, lint its
# std/<other>/*.ft under that root, and emit one module for it that the
# verifier accepts. Each root declares one libc, so the compiler's own root
# cannot hold the other target's modules. The C ABI of that target is checked
# on its own host.
set -euo pipefail

if [ "$#" -ne 3 ]; then
    echo "usage: cross_target_test.sh <fort> <opt> <linux|darwin>" >&2
    exit 2
fi

fort=$1
opt=$2
case "$3" in
linux)
    other=darwin
    triple=arm64-apple-macosx11.0.0
    header=$triple
    ;;
darwin)
    other=linux
    triple=x86_64-linux-gnu
    header=x86_64-unknown-linux-gnu
    ;;
*)
    echo "cross_target_test.sh: unknown target '$3'" >&2
    exit 2
    ;;
esac

root=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$root"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
bash tools/assemble_std.sh "$other" std "$work/std"

python3 tools/fort_lint.py --fort "$fort" --std-dir "$work/std" std/"$other"/*.ft

printf 'fn main() i32 { return 0; }\n' >"$work/main.ft"
"$fort" -S --target "$triple" --std-dir "$work/std" -o "$work/other.ll" "$work/main.ft"
grep -F -x -q "target triple = \"$header\"" "$work/other.ll"
if grep -q '^target datalayout' "$work/other.ll"; then
    echo "cross_target_test.sh: $other IR has a datalayout" >&2
    exit 1
fi
# One IR for both targets (D19.1): the stack probe of every definition and
# the fixed form of every extern declaration.
grep -q '"probe-stack"="inline-asm"' "$work/other.ll"
grep -q '^declare i64 @write(i32, ptr, i64)' "$work/other.ll"
"$opt" -passes=verify -disable-output "$work/other.ll"
echo "cross target $other: std linted; one module passed opt verification"
