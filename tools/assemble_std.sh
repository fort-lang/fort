#!/usr/bin/env bash
# usage: assemble_std.sh <linux|darwin> <std-dir> <out-dir>
# Writes the flat standard root of one target: std/*.ft and std/<target>/*.ft.
set -euo pipefail

target=$1 std=$2 out=$3
if [ ! -d "$std/$target" ]; then
    echo "assemble_std.sh: no standard modules for target '$target'" >&2
    exit 2
fi
mkdir -p "$out"
cp "$std"/*.ft "$std/$target"/*.ft "$out/"
