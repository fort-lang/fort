#!/bin/bash
# test/mac_pipeline_test.sh: prove the Apple arm64 IR pipeline on a Mac host.
# D9.7, D14.3, D19.1, D19.7
set -eu

if [ "$#" -ne 0 ] || [ "$(uname -sm)" != "Darwin arm64" ]; then
    echo "mac pipeline: requires a Mac arm64 host and no arguments" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd -P)
version=$(sw_vers -productVersion)
case "$version" in
[0-9]*.[0-9]*.[0-9]*) ;;
[0-9]*.[0-9]*) version="$version.0" ;;
*) echo "mac pipeline: unsupported Mac version '$version'" >&2; exit 2 ;;
esac
if ! [[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "mac pipeline: unsupported Mac version '$version'" >&2
    exit 2
fi
target="arm64-apple-macosx$version"

sdk=$(xcrun --sdk macosx --show-sdk-path) || exit 2
cc=$(xcrun --sdk macosx --find clang) || exit 2
opt=${FORT_MAC_OPT:-/opt/homebrew/bin/opt}
for tool in "$opt" "$cc" file otool nm; do
    if ! command -v "$tool" >/dev/null; then
        echo "mac pipeline: tool '$tool' not found" >&2
        exit 2
    fi
done
if [ ! -d "$sdk" ]; then
    echo "mac pipeline: SDK '$sdk' not found" >&2
    exit 2
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
ulimit -c 0
sed "s/arm64-apple-macosx15.0.0/$target/" "$root/test/ir/mac.ll" >"$work/mac.ll"

"$opt" -passes=verify -disable-output "$work/mac.ll"
"$cc" --target="$target" -isysroot "$sdk" -O1 -fPIE -Wl,-pie \
    -Wno-override-module -o "$work/mac" "$work/mac.ll"

if ! file "$work/mac" | grep -Eq 'Mach-O 64-bit executable arm64'; then
    echo "mac pipeline: executable is not ARM64 Mach-O" >&2
    exit 1
fi
if ! otool -hv "$work/mac" | grep -Eq '(^|[[:space:]])PIE([[:space:]]|$)'; then
    echo "mac pipeline: executable has no PIE flag" >&2
    exit 1
fi
if ! nm "$work/mac" | grep -q ' T _my:app.main$'; then
    echo "mac pipeline: object has no _my:app.main symbol" >&2
    exit 1
fi

status=0
"$work/mac" >"$work/run.out" 2>"$work/run.err" || status=$?
if [ "$status" -ne 0 ] || ! printf 'colon\n' | cmp -s - "$work/run.out" || [ -s "$work/run.err" ]; then
    echo "mac pipeline: program output or exit status differs" >&2
    exit 1
fi

status=0
"$work/mac" trap >"$work/trap.out" 2>"$work/trap.err" || status=$?
if [ "$status" -ne 133 ] || [ -s "$work/trap.out" ] || [ -s "$work/trap.err" ]; then
    echo "mac pipeline: trap status $status, expected 133 (SIGTRAP)" >&2
    exit 1
fi

echo "mac pipeline: ok; $target; ARM64 Mach-O PIE; SIGTRAP 133"
