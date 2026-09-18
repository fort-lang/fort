#!/bin/bash
# Test the shipped Darwin compiler, corpus, runtime, network, and LSP.
set -euo pipefail

if [ "$#" -ne 0 ] || [ "$(uname -s)" != "Darwin" ]; then
    echo "darwin gate: requires a Darwin host and no arguments" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$root"
opt=${FORT_DARWIN_OPT:-/opt/homebrew/bin/opt}
cc=$(xcrun --sdk macosx --find clang)
SDKROOT=$(xcrun --sdk macosx --show-sdk-path)
export SDKROOT
tools/darwin pipeline
tools/darwin fixpoint
target=$(cat build/darwin/bootstrap/target)
echo "selected target: darwin"
echo "target triple: $target"
tools/darwin identity
python3 tools/fort_lint.py --fort build/darwin/fort \
    --std-dir build/darwin/std std/darwin/libc.ft std/darwin/net.ft
test/darwin_noreturn_trap_test.sh build/darwin/fort
test/darwin_printf_test.sh build/darwin/fort
bash test/darwin_allocation_failure_test.sh \
    build/darwin/fort build/darwin/std "$target"

trap_fixture=run/ffi/009_noreturn_returns_anyway.ft
errno_fixture=run/stdlib/118_net_errors.ft
sockaddr_fixture=run/ffi/013_sockaddr_layout.ft
conditional_fixture=run/modules/conditional_imports
args=(--fort build/darwin/fort --std-dir build/darwin/std \
      --cc "$cc" --target "$target" --opt "$opt" \
      --xfail test/lang/xfail.txt \
      --exclude-exact "$trap_fixture" --exclude-exact "$errno_fixture" \
      --exclude-exact "$sockaddr_fixture" \
      --exclude-exact "$conditional_fixture")
python3 test/lang/run_tests.py --lint "${args[@]}"
full=$(python3 test/lang/run_tests.py --list | tail -n 1 | awk '{print $1}')
selected=$(python3 test/lang/run_tests.py --list "${args[@]}" | tail -n 1 | awk '{print $1}')
if [ "$full" -ne $((selected + 4)) ]; then
    echo "darwin gate: exact linux exclusion count differs" >&2
    exit 1
fi
echo "darwin gate: $selected corpus tests selected; 4 linux-only fixtures excluded"
echo "darwin gate exclusion: $trap_fixture expects SIGILL; darwin llvm.trap raises SIGTRAP"
echo "darwin gate exclusion: $errno_fixture expects linux ECONNREFUSED 111 and ENOTSOCK 88; darwin returns 61 and 38"
echo "darwin gate exclusion: $sockaddr_fixture uses a linux u16 family field; darwin sockaddr_in starts with u8 length and family"
echo "darwin gate exclusion: $conditional_fixture records linux output 42; darwin selects output 99"
python3 test/lang/run_tests.py --verify-ir "${args[@]}"
python3 test/lang/run_tests.py --check-json "${args[@]}"
tools/darwin core
tools/darwin net
echo "tools/target darwin gate: green; $selected corpus tests; 4 named linux-only exclusions; darwin LSP, fixpoint, lint, runtime, and network pass"
