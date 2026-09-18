#!/bin/bash
# test/fake_cc.sh: the --cc program for driver unit tests.
# Record the spawned command in $FORT_FAKE_CC_LOG, one argument per line.
# Then exit with $FORT_FAKE_CC_STATUS, which defaults to 0.
# If set, $FORT_FAKE_CC_SIGNAL makes the script kill itself.
# The script compiles nothing. Driver tests inspect the recorded arguments and failure status.
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
