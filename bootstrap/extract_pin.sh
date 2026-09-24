#!/usr/bin/env bash
# usage: extract_pin.sh <sha> <output-dir> <linux|darwin>
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
sha=$1 out=$2 target=$3

rm -rf "$out"
mkdir -p "$out/target/entry"
git -C "$root" archive "$sha" src/fort std tools/assemble_std.sh | tar -x -C "$out"
bash "$out/tools/assemble_std.sh" "$target" "$out/std" "$out/target/std"
cp "$out/src/fort/main.ft" "$out/target/entry/"

echo "$sha" >"$out/.assembled-$sha-$target"
