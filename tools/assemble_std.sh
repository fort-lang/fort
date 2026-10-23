#!/usr/bin/env bash
# usage: assemble_std.sh <linux|darwin> <std-dir> <out-dir>
# Writes the standard root of one target: each .ft below <std-dir> and below <std-dir>/<target>,
# at its path below that directory. std/digest/sha256.ft gives <out-dir>/digest/sha256.ft and
# std/<target>/libc.ft gives <out-dir>/libc.ft. The directory of another target gives nothing.
set -euo pipefail

targets="linux darwin"
target=$1 std=$2 out=$3
if [ ! -d "$std/$target" ]; then
    echo "assemble_std.sh: no standard modules for target '$target'" >&2
    exit 2
fi
mkdir -p "$out"

# copy_tree <dir> <skip-targets>: copy each .ft below <dir> to its path below $out.
copy_tree() {
    local dir=$1 skip=$2 file rel top
    while IFS= read -r -d '' file; do
        rel=${file#"$dir/"}
        top=${rel%%/*}
        if [ "$skip" = yes ] && [ "$top" != "$rel" ] && [[ " $targets " == *" $top "* ]]; then
            continue
        fi
        mkdir -p "$out/$(dirname "$rel")"
        cp "$file" "$out/$rel"
    done < <(find "$dir/" -name '*.ft' -type f -print0)
}

copy_tree "$std" yes
copy_tree "$std/$target" no
