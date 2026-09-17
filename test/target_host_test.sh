#!/bin/bash
# Check native host detection and rejected host forms.
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: target_host_test.sh linux|darwin" >&2
    exit 2
fi
selected=$1
case "$(uname -s):$selected" in
Linux:linux) triple=x86_64-linux-gnu ;;
Darwin:darwin) triple=arm64-apple-macosx ;;
*) echo "target host: selected target does not match this host" >&2; exit 1 ;;
esac

root=$(cd "$(dirname "$0")/.." && pwd -P)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cmake -S "$root" -B "$work/accepted" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    >"$work/accepted.out" 2>&1
grep -F -q "$triple" "$work/accepted/build.ninja"

if cmake -S "$root" -B "$work/cross" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_SYSTEM_NAME=Generic \
    >"$work/cross.out" 2>&1; then
    echo "target host: accepted a cross build" >&2
    exit 1
fi
grep -F -q "requires a native build" "$work/cross.out"
echo "target host: accepted $selected and rejected cross compilation"
