#!/bin/bash
# Test names in the Darwin standard root with stage2.
# The Linux standard root conflicts with Darwin libc declarations.
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: darwin_fort_lint_test.sh <stage2-fort>" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$root"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
bash tools/assemble_std.sh darwin std "$work/std"

darwin_files=(std/darwin/*.ft)
if [ ! -e "${darwin_files[0]}" ]; then
    echo "darwin fort lint: no darwin standard file exists" >&2
    exit 1
fi

python3 tools/fort_lint.py --fort "$1" --std-dir "$work/std" "${darwin_files[@]}"
