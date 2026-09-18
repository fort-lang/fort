#!/usr/bin/env bash
# Verify the Darwin IR form that a Linux stage2 emits with a caller standard root.
# The VM standard root is Linux. This test does not check the Mac C ABI.
set -euo pipefail

stage2=$1
std_dir=$2
opt=$3
scratch=$(mktemp -d "$PWD/target-ir-test.XXXXXX")
trap 'rm -rf "$scratch"' EXIT
printf 'fn main() i32 { return 0; }\n' >"$scratch/main.ft"

"$stage2" -S --target arm64-apple-macosx15.0.0 --std-dir "$std_dir" \
    -o "$scratch/darwin.ll" "$scratch/main.ft"
grep -F -x -q 'target triple = "arm64-apple-macosx15.0.0"' "$scratch/darwin.ll"
if grep -q '^target datalayout' "$scratch/darwin.ll"; then
    echo 'target_ir_test.sh: Mac IR has a datalayout' >&2
    exit 1
fi
"$opt" -passes=verify -disable-output "$scratch/darwin.ll"
echo 'Mac IR form: one module passed opt verification'
