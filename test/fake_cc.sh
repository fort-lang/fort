#!/bin/bash
# test/fake_cc.sh: the --cc of the driver unit tests (test/driver_test.c). It
# records the whole command line the driver spawned, one argument per line
# and its own path first, in $FORT_FAKE_CC_LOG, then exits with
# $FORT_FAKE_CC_STATUS (0 by default), or kills itself with
# $FORT_FAKE_CC_SIGNAL when that is set, so that both failure paths of D14.3
# ("fort: error: cc failed with status N" and "... with signal N") are
# exercised too. It compiles nothing: the driver's contract is the argv it
# builds, not what clang makes of it.
set -eu

log=${FORT_FAKE_CC_LOG:?fake_cc.sh: FORT_FAKE_CC_LOG is not set}
printf '%s\n' "$0" >"$log"
for arg in "$@"; do
    printf '%s\n' "$arg" >>"$log"
done
if [ -n "${FORT_FAKE_CC_SIGNAL:-}" ]; then
    kill -"$FORT_FAKE_CC_SIGNAL" $$
fi
exit "${FORT_FAKE_CC_STATUS:-0}"
