#!/bin/bash
# Test D1.4 names in the Mac standard root with stage2.
# T-144: the Linux standard root conflicts with Mac libc declarations.
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: mac_fort_lint_test.sh <stage2-fort>" >&2
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

mac_files=(std/mac/*.ft)
if [ ! -e "${mac_files[0]}" ]; then
    echo "mac fort lint: no Mac standard file exists" >&2
    exit 1
fi
for source in "${mac_files[@]}"; do
    cp "$source" "$work/std/"
done

python3 tools/fort_lint.py --fort "$1" --std-dir "$work/std" "${mac_files[@]}"
