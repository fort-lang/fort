#!/bin/bash
# Lint the standard modules of the target this compiler does not build for.
# The compiler's own root holds the modules of its target. The modules of the
# other target need a root of their own, since each root declares one libc.
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: cross_fort_lint_test.sh <stage2-fort> <linux|darwin>" >&2
    exit 2
fi

case "$2" in
linux) other=darwin ;;
darwin) other=linux ;;
*)
    echo "cross_fort_lint_test.sh: unknown target '$2'" >&2
    exit 2
    ;;
esac

root=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$root"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
bash tools/assemble_std.sh "$other" std "$work/std"

other_files=(std/"$other"/*.ft)
if [ ! -e "${other_files[0]}" ]; then
    echo "cross fort lint: no $other standard file exists" >&2
    exit 1
fi

python3 tools/fort_lint.py --fort "$1" --std-dir "$work/std" "${other_files[@]}"
