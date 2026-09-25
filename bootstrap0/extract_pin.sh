#!/usr/bin/env bash
# usage: extract_pin.sh <sha> <output-dir> <linux|darwin>
# Writes the pin's src/fort and its own tools/assemble_std.sh to <output-dir>, the
# pin's std/ to <output-dir>/std-pin, and the flat standard root of the target
# to <output-dir>/std. The pin's std/ lands in std-pin through `tar
# --strip-components`, not through `mv`: vboxsf, the VM's shared folder, cannot
# rename a directory that holds subdirectories.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
sha=$1 out=$2 target=$3

rm -rf "$out"
mkdir -p "$out/std-pin"
git -C "$root" archive "$sha" src/fort tools/assemble_std.sh | tar -x -C "$out"
git -C "$root" archive "$sha" std | tar -x -C "$out/std-pin" --strip-components=1
bash "$out/tools/assemble_std.sh" "$target" "$out/std-pin" "$out/std"

echo "$sha" >"$out/.assembled-$sha-$target"
