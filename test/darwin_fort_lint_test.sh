#!/bin/bash
# Test D1.4 names in the darwin standard root with stage2.
# T-144: the linux standard root conflicts with darwin libc declarations.
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: darwin_fort_lint_test.sh <stage2-fort>" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$root"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
mkdir "$work/std"
for source in std/*.ft; do
    case "$source" in
    std/libc.ft|std/net.ft) ;;
    *) cp "$source" "$work/std/" ;;
    esac
done

darwin_files=(std/darwin/*.ft)
if [ ! -e "${darwin_files[0]}" ]; then
    echo "darwin fort lint: no darwin standard file exists" >&2
    exit 1
fi
for source in "${darwin_files[@]}"; do
    cp "$source" "$work/std/"
done

python3 tools/fort_lint.py --fort "$1" --std-dir "$work/std" "${darwin_files[@]}"
