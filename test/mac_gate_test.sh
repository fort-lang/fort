#!/bin/bash
# T-147: count the shipped Mac compiler, corpus, runtime, network, and LSP.
set -euo pipefail

if [ "$#" -ne 0 ] || [ "$(uname -sm)" != "Darwin arm64" ]; then
    echo "mac gate: requires a Mac arm64 host and no arguments" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$root"
export FORT_VM_SLOT=2
opt=${FORT_MAC_OPT:-/opt/homebrew/bin/opt}
cc=$(xcrun --sdk macosx --find clang)
target="arm64-apple-macosx$(sw_vers -productVersion)"
if [[ "$target" =~ ^arm64-apple-macosx[0-9]+\.[0-9]+$ ]]; then
    target="$target.0"
fi
if ! [[ "$target" =~ ^arm64-apple-macosx[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "mac gate: unsupported Mac target '$target'" >&2
    exit 2
fi

tools/mac pipeline
tools/mac fixpoint
tools/mac identity
python3 tools/fort_lint.py --fort build/mac-native/fort \
    --std-dir build/mac-native/std std/mac/libc.ft std/mac/net.ft
test/mac_noreturn_trap_test.sh build/mac-native/fort
test/mac_printf_test.sh build/mac-native/fort

trap_fixture=run/ffi/009_noreturn_returns_anyway.ft
errno_fixture=run/stdlib/118_net_errors.ft
printf_fixture=run/ffi/008_printf_variadic.ft
sockaddr_fixture=run/ffi/013_sockaddr_layout.ft
args=(--fort build/mac-native/fort --std-dir build/mac-native/std \
      --cc "$cc" --target "$target" --opt "$opt" \
      --xfail test/lang/xfail-stage2.txt \
      --unsupported test/lang/unsupported-stage2.txt \
      --exclude-exact "$trap_fixture" --exclude-exact "$errno_fixture" \
      --exclude-exact "$printf_fixture" --exclude-exact "$sockaddr_fixture")
python3 test/lang/run_tests.py --lint "${args[@]}"
full=$(python3 test/lang/run_tests.py --list | tail -n 1 | awk '{print $1}')
selected=$(python3 test/lang/run_tests.py --list "${args[@]}" | tail -n 1 | awk '{print $1}')
if [ "$full" -ne $((selected + 4)) ]; then
    echo "mac gate: exact Linux exclusion count differs" >&2
    exit 1
fi
echo "mac gate: $selected corpus tests selected; 4 Linux-only fixtures excluded"
echo "mac gate exclusion: $trap_fixture expects SIGILL; Mac llvm.trap raises SIGTRAP"
echo "mac gate exclusion: $errno_fixture expects Linux ECONNREFUSED 111 and ENOTSOCK 88; Mac returns 61 and 38"
echo "mac gate exclusion: $printf_fixture uses a fixed extern for Linux System V; Mac printf needs an explicit variadic tail"
echo "mac gate exclusion: $sockaddr_fixture uses a Linux u16 family field; Mac sockaddr_in starts with u8 length and family"
python3 test/lang/run_tests.py --verify-ir "${args[@]}"
python3 test/lang/run_tests.py --check-json "${args[@]}"
tools/mac core
tools/mac net
echo "tools/mac gate: green; $selected corpus tests; 4 named Linux-only exclusions; native LSP, fixpoint, lint, runtime, and network pass"
