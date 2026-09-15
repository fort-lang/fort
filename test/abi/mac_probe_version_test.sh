#!/bin/bash
# Check the Mac target text without a VM build.
set -eu

probe_root=$(git rev-parse --show-toplevel)
cd "$probe_root"

probe_two=$(FORT_MAC_PROBE_VERSION=15.0 bash test/abi/mac_probe.sh --print-target)
probe_three=$(FORT_MAC_PROBE_VERSION=26.6.2 bash test/abi/mac_probe.sh --print-target)
[ "$probe_two" = arm64-apple-macosx15.0.0 ]
[ "$probe_three" = arm64-apple-macosx26.6.2 ]
if FORT_MAC_PROBE_VERSION=15.a bash test/abi/mac_probe.sh --print-target >/dev/null 2>&1; then
    printf 'mac_probe_version_test: invalid version passed\n' >&2
    exit 1
fi
printf 'mac_probe_version_test: two valid versions and one invalid version pass\n'
