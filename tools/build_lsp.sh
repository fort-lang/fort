#!/bin/bash
# Build the language server for the verified seed target.
set -euo pipefail

if [ "$#" -lt 7 ]; then
    echo "usage: build_lsp.sh <compiler> <seed-identity> <std> <cc> <output> <entry> <include>..." >&2
    exit 2
fi

compiler=$1
identity=$2
std=$3
cc=$4
output=$5
entry=$6
shift 6

if [ ! -x "$compiler" ]; then
    echo "build_lsp.sh: compiler or seed identity is missing" >&2
    exit 2
fi
root=$(cd "$(dirname "$0")/.." && pwd -P)
target=$(bash "$root/tools/seed_target.sh" "$identity")

args=()
for root in "$@"; do
    args+=(-I "$root")
done
exec "$compiler" --std-dir "$std" --cc "$cc" --target "$target" \
    "${args[@]}" -o "$output" "$entry"
