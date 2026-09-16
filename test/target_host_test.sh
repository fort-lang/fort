#!/bin/bash
# Check the accepted target and the rejected target for this host.
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: target_host_test.sh linux|darwin" >&2
    exit 2
fi
selected=$1
case "$(uname -s):$selected" in
Linux:linux) rejected=darwin ;;
Darwin:darwin) rejected=linux ;;
*) echo "target host: selected target does not match this host" >&2; exit 1 ;;
esac

root=$(cd "$(dirname "$0")/.." && pwd -P)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cmake -S "$root" -B "$work/accepted" -G Ninja \
    -DFORT_TARGET="$selected" >"$work/accepted.out" 2>&1
grep -F -q "FORT_TARGET:STRING=$selected" "$work/accepted/CMakeCache.txt"

if cmake -S "$root" -B "$work/rejected" -G Ninja \
    -DFORT_TARGET="$rejected" >"$work/rejected.out" 2>&1; then
    echo "target host: accepted the $rejected target" >&2
    exit 1
fi
grep -F -q "the $rejected target requires a $rejected" "$work/rejected.out"
echo "target host: accepted $selected and rejected $rejected"
