#!/bin/bash
# stage1 and stage2 must answer --help and --version with the same bytes.
#
# src/fort/main.ft carries the option table of toolchain.md 1 and the version
# string of src/bootstrap/driver.h as a second copy of what driver.c prints,
# and nothing else holds the two together. Both binaries exist at build time,
# so the cheap witness is to run them and diff. stage2 is an x86-64 binary and
# runs under qemu, like every program the compiler builds.
#
# Usage: stage_usage_test.sh <build-dir>
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: stage_usage_test.sh <build-dir>" >&2
    exit 2
fi
build=$1
stage1=$build/fort
stage2=$build/stage2/fort

for binary in "$stage1" "$stage2"; do
    if [ ! -x "$binary" ]; then
        echo "stage_usage_test.sh: not built: $binary" >&2
        exit 2
    fi
done

status=0

# Each answer is captured rather than piped into diff, so that the exit status
# is checked too and two binaries printing nothing cannot compare equal: a
# stage2 with the right bytes and the wrong status would otherwise pass.
answer=""
answer_status=0
capture() {
    answer=$("$1" "$2" 2>/dev/null) && answer_status=0 || answer_status=$?
}

for option in --help --version; do
    capture "$stage1" "$option"
    one=$answer
    one_status=$answer_status
    capture "$stage2" "$option"
    two=$answer
    two_status=$answer_status
    if [ -z "$one" ]; then
        echo "stage_usage_test.sh: stage1 printed nothing for $option" >&2
        status=1
    fi
    # --help and --version print and exit 0 (toolchain.md 1).
    if [ "$one_status" -ne 0 ] || [ "$two_status" -ne 0 ]; then
        echo "stage_usage_test.sh: $option exited $one_status (stage1) and" \
            "$two_status (stage2), expected 0" >&2
        status=1
    fi
    if [ "$one" != "$two" ]; then
        echo "stage_usage_test.sh: stage1 and stage2 disagree about $option" >&2
        diff -u <(printf '%s\n' "$one") <(printf '%s\n' "$two") >&2 || true
        status=1
    fi
done

# With no argument each prints one usage line on stderr and exits 2
# (toolchain.md 1), so the status is part of the answer here as well.
one=$("$stage1" 2>&1 >/dev/null) && one_status=0 || one_status=$?
two=$("$stage2" 2>&1 >/dev/null) && two_status=0 || two_status=$?
if [ -z "$one" ]; then
    echo "stage_usage_test.sh: stage1 printed no usage line" >&2
    status=1
fi
if [ "$one_status" -ne 2 ] || [ "$two_status" -ne 2 ]; then
    echo "stage_usage_test.sh: no argument exited $one_status (stage1) and" \
        "$two_status (stage2), expected 2" >&2
    status=1
fi
if [ "$one" != "$two" ]; then
    echo "stage_usage_test.sh: the usage lines differ:" >&2
    echo "  stage1: $one" >&2
    echo "  stage2: $two" >&2
    status=1
fi

if [ "$status" -eq 0 ]; then
    echo "stage1 and stage2 agree about --help, --version and the usage line,"
    echo "each with the exit status toolchain.md 1 gives it"
fi
exit "$status"
