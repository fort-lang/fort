#!/bin/bash
# test/darwin_pipeline_test.sh: prove the Apple arm64 IR pipeline on a darwin host.
# D9.7, D14.3, D19.1, D19.7
set -eu

if [ "$#" -ne 0 ] || [ "$(uname -sm)" != "Darwin arm64" ]; then
    echo "darwin pipeline: requires a darwin arm64 host and no arguments" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd -P)
version=$(sw_vers -productVersion)
case "$version" in
[0-9]*.[0-9]*.[0-9]*) ;;
[0-9]*.[0-9]*) version="$version.0" ;;
*) echo "darwin pipeline: unsupported darwin version '$version'" >&2; exit 2 ;;
esac
if ! [[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "darwin pipeline: unsupported darwin version '$version'" >&2
    exit 2
fi
target="arm64-apple-macosx$version"

sdk=$(xcrun --sdk macosx --show-sdk-path) || exit 2
cc=$(xcrun --sdk macosx --find clang) || exit 2
opt=${FORT_DARWIN_OPT:-/opt/homebrew/bin/opt}
for tool in "$opt" "$cc" file otool nm; do
    if ! command -v "$tool" >/dev/null; then
        echo "darwin pipeline: tool '$tool' not found" >&2
        exit 2
    fi
done
if [ ! -d "$sdk" ]; then
    echo "darwin pipeline: SDK '$sdk' not found" >&2
    exit 2
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
ulimit -c 0
sed "s/arm64-apple-macosx15.0.0/$target/" "$root/test/darwin/ir/darwin.ll" >"$work/darwin.ll"

"$opt" -passes=verify -disable-output "$work/darwin.ll"
"$cc" --target="$target" -isysroot "$sdk" -O1 -fPIE \
    -Wno-override-module -o "$work/darwin" "$work/darwin.ll"

if ! file "$work/darwin" | grep -Eq 'Mach-O 64-bit executable arm64'; then
    echo "darwin pipeline: executable is not ARM64 Mach-O" >&2
    exit 1
fi
if ! otool -hv "$work/darwin" | grep -Eq '(^|[[:space:]])PIE([[:space:]]|$)'; then
    echo "darwin pipeline: executable has no PIE flag" >&2
    exit 1
fi
if ! nm "$work/darwin" | grep -q ' T _my:app.main$'; then
    echo "darwin pipeline: object has no _my:app.main symbol" >&2
    exit 1
fi

status=0
"$work/darwin" >"$work/run.out" 2>"$work/run.err" || status=$?
if [ "$status" -ne 0 ] || ! printf 'colon\n' | cmp -s - "$work/run.out" || [ -s "$work/run.err" ]; then
    echo "darwin pipeline: program output or exit status differs" >&2
    exit 1
fi

status=0
"$work/darwin" trap >"$work/trap.out" 2>"$work/trap.err" || status=$?
if [ "$status" -ne 133 ] || [ -s "$work/trap.out" ] || [ -s "$work/trap.err" ]; then
    echo "darwin pipeline: trap status $status, expected 133 (SIGTRAP)" >&2
    exit 1
fi

echo "darwin pipeline: ok; $target; ARM64 Mach-O PIE; SIGTRAP 133"
