#!/bin/bash
# Check that the ABI probe reports the selected target.
set -eu

probe_root=$(git rev-parse --show-toplevel)
cd "$probe_root"
probe_work=$(mktemp -d)
trap 'rm -rf "$probe_work"' EXIT

printf 'arm64-apple-macosx15.0.0\n' >"$probe_work/target-15"
printf 'arm64-apple-macosx26.6.2\n' >"$probe_work/target-26"
printf 'x86_64-linux-gnu\n' >"$probe_work/linux-target"

probe_15=$(FORT_DARWIN_PROBE_TARGET_FILE="$probe_work/target-15" \
    bash test/abi/darwin_probe.sh --print-target)
probe_26=$(FORT_DARWIN_PROBE_TARGET_FILE="$probe_work/target-26" \
    bash test/abi/darwin_probe.sh --print-target)
[ "$probe_15" = arm64-apple-macosx15.0.0 ]
[ "$probe_26" = arm64-apple-macosx26.6.2 ]
if FORT_DARWIN_PROBE_TARGET_FILE="$probe_work/linux-target" \
    bash test/abi/darwin_probe.sh --print-target >"$probe_work/out" 2>"$probe_work/error"; then
    printf 'darwin_probe_target_test: Linux target passed\n' >&2
    exit 1
fi
grep -q 'selected target is not darwin' "$probe_work/error"
printf 'darwin probe target: 2 Darwin targets passed; 1 Linux target rejected\n'
