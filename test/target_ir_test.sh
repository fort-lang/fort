#!/usr/bin/env bash
# Verify the IR form that stage2 emits for the target it does not build for,
# with a caller standard root. This test does not check the C ABI of that
# target: its programs run on the other host.
set -euo pipefail

if [ "$#" -ne 3 ]; then
    echo "usage: target_ir_test.sh <stage2> <opt> <linux|darwin>" >&2
    exit 2
fi

stage2=$1
opt=$2
case "$3" in
linux)
    other=darwin
    triple=arm64-apple-macosx15.0.0
    header=$triple
    ;;
darwin)
    other=linux
    triple=x86_64-linux-gnu
    header=x86_64-unknown-linux-gnu
    ;;
*)
    echo "target_ir_test.sh: unknown target '$3'" >&2
    exit 2
    ;;
esac

root=$(cd "$(dirname "$0")/.." && pwd -P)
scratch=$(mktemp -d "$PWD/target-ir-test.XXXXXX")
trap 'rm -rf "$scratch"' EXIT
printf 'fn main() i32 { return 0; }\n' >"$scratch/main.ft"

# The other target's standard root, so that its libc is the one declared.
bash "$root/tools/assemble_std.sh" "$other" "$root/std" "$scratch/std"

"$stage2" -S --target "$triple" --std-dir "$scratch/std" \
    -o "$scratch/other.ll" "$scratch/main.ft"
grep -F -x -q "target triple = \"$header\"" "$scratch/other.ll"
if grep -q '^target datalayout' "$scratch/other.ll"; then
    echo "target_ir_test.sh: $other IR has a datalayout" >&2
    exit 1
fi
# One IR for both targets (D19.1): the stack probe of every definition and
# the fixed form of every extern declaration.
grep -q '"probe-stack"="inline-asm"' "$scratch/other.ll"
grep -q '^declare i64 @write(i32, ptr, i64)' "$scratch/other.ll"
"$opt" -passes=verify -disable-output "$scratch/other.ll"
echo "$other IR form: one module passed opt verification"
