#!/bin/bash
# Build the language server for the selected target.
set -euo pipefail

if [ "$#" -lt 7 ]; then
    echo "usage: build_lsp.sh <compiler> <target> <std> <cc> <output> <entry> <include>..." >&2
    exit 2
fi

compiler=$1
target=$2
std=$3
cc=$4
output=$5
entry=$6
shift 6

if [ ! -x "$compiler" ]; then
    echo "build_lsp.sh: compiler is missing" >&2
    exit 2
fi
if [ "$target" != x86_64-linux-gnu ] &&
   ! [[ "$target" =~ ^arm64-apple-macosx[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "build_lsp.sh: unsupported target '$target'" >&2
    exit 2
fi

args=()
for root in "$@"; do
    args+=(-I "$root")
done
exec "$compiler" --std-dir "$std" --cc "$cc" --target "$target" \
    "${args[@]}" -o "$output" "$entry"
