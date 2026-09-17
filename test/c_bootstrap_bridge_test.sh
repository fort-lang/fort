#!/bin/bash
# Check the C compiler and the one source compiler that it builds.
set -eu

if [ "$#" -ne 6 ]; then
    echo "usage: c_bootstrap_bridge_test.sh linux|darwin <stage1> <bridge> <std> <cc> <opt>" >&2
    exit 2
fi
target_name=$1
stage1=$2
bridge=$3
std=$4
cc=$5
opt=$6

case "$target_name" in
linux)
    target=x86_64-linux-gnu
    triple=x86_64-unknown-linux-gnu
    format='ELF 64-bit LSB pie executable, x86-64'
    ;;
darwin)
    target=arm64-apple-macosx11.0.0
    triple=$target
    format='Mach-O 64-bit executable arm64'
    ;;
*) exit 2 ;;
esac

for input in "$stage1" "$bridge" "$cc" "$opt"; do
    [ -x "$input" ] || { echo "c bootstrap: missing executable: $input" >&2; exit 2; }
done
[ -f "$std/rt.ft" ] || { echo "c bootstrap: standard root is missing" >&2; exit 2; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
printf 'fn main() i32 { return 0; }\n' >"$work/main.ft"
for compiler in "$stage1" "$bridge"; do
    "$compiler" -S --std-dir "$std" --target "$target" \
        -o "$work/module.ll" "$work/main.ft"
    grep -F -x -q "target triple = \"$triple\"" "$work/module.ll"
    "$opt" -passes=verify -disable-output "$work/module.ll"
done
file -b "$bridge" | grep -F -q "$format"
"$bridge" --std-dir "$std" --cc "$cc" --target "$target" \
    -o "$work/program" "$work/main.ft"
file -b "$work/program" | grep -F -q "$format"
"$work/program"
echo "c bootstrap: stage1 and bootstrap-0 pass for $target_name"
