#!/bin/bash
# Check that the ABI probe reports the verified seed target.
set -eu

probe_root=$(git rev-parse --show-toplevel)
cd "$probe_root"
probe_work=$(mktemp -d)
trap 'rm -rf "$probe_work"' EXIT

printf 'default triple: arm64-apple-macosx15.0.0\n' >"$probe_work/seed-15.identity"
printf 'default triple: arm64-apple-macosx26.6.2\n' >"$probe_work/seed-26.identity"
printf 'default triple: x86_64-unknown-linux-gnu\n' >"$probe_work/linux.identity"

probe_15=$(FORT_DARWIN_PROBE_IDENTITY="$probe_work/seed-15.identity" \
    bash test/abi/darwin_probe.sh --print-target)
probe_26=$(FORT_DARWIN_PROBE_IDENTITY="$probe_work/seed-26.identity" \
    bash test/abi/darwin_probe.sh --print-target)
[ "$probe_15" = arm64-apple-macosx15.0.0 ]
[ "$probe_26" = arm64-apple-macosx26.6.2 ]
if FORT_DARWIN_PROBE_IDENTITY="$probe_work/linux.identity" \
    bash test/abi/darwin_probe.sh --print-target >"$probe_work/out" 2>"$probe_work/error"; then
    printf 'darwin_probe_target_test: linux identity passed\n' >&2
    exit 1
fi
grep -q 'seed target is not darwin' "$probe_work/error"
printf 'darwin probe target: 2 seed targets passed; 1 linux target rejected\n'
