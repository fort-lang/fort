#!/bin/bash
# Check compiler targets derived from verified seed identities.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd -P)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

printf 'default triple: arm64-apple-macosx15.0.0\n' >"$work/darwin"
[ "$(bash "$root/tools/seed_target.sh" "$work/darwin")" = \
    arm64-apple-macosx15.0.0 ]
printf 'default triple: x86_64-unknown-linux-gnu\n' >"$work/linux"
[ "$(bash "$root/tools/seed_target.sh" "$work/linux")" = \
    x86_64-linux-gnu ]
printf 'default triple: arm64-apple-macosx15.0.0\ndefault triple: arm64-apple-macosx26.6.2\n' \
    >"$work/two"
if bash "$root/tools/seed_target.sh" "$work/two" 2>"$work/error"; then
    echo "seed_target_test.sh: two targets were accepted" >&2
    exit 1
fi
grep -q 'must contain one default triple' "$work/error"
echo "seed target: 2 targets selected; 1 duplicate record rejected"
