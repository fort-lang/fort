#!/bin/bash
# Build the language server for the compiler's own target.
set -euo pipefail

if [ "$#" -lt 6 ]; then
    echo "usage: build_lsp.sh <compiler> <std> <cc> <output> <entry> <include>..." >&2
    exit 2
fi

compiler=$1
std=$2
cc=$3
output=$4
entry=$5
shift 5

if [ ! -x "$compiler" ]; then
    echo "build_lsp.sh: compiler is missing" >&2
    exit 2
fi

args=()
for root in "$@"; do
    args+=(-I "$root")
done
exec "$compiler" --std-dir "$std" --cc "$cc" "${args[@]}" -o "$output" "$entry"
