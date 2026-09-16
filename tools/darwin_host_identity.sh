#!/bin/bash
# T-147: record the darwin host, seed, tools, and fort input paths.
set -euo pipefail

if [ "$#" -ne 0 ] || [ "$(uname -sm)" != "Darwin arm64" ]; then
    echo "darwin identity: requires a darwin arm64 host and no arguments" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$root"
if [ -n "$(git status --porcelain --untracked-files=all)" ]; then
    echo "darwin identity: source tree is not clean" >&2
    exit 1
fi
for input in build/darwin/stage2/fort build/darwin/seed.identity \
             build/darwin/bootstrap-stages.tsv \
             build/darwin/fort-lsp build/darwin/std/libc.ft \
             build/darwin/std/net.ft; do
    if [ ! -f "$input" ]; then
        echo "darwin identity: missing $input" >&2
        exit 1
    fi
done

echo "source SHA: $(git rev-parse HEAD)"
echo "main SHA: $(git rev-parse main)"
echo "git status: empty"
echo "selected target: darwin"
cat build/darwin/seed.identity
echo "host CPU: $(uname -m)"
echo "host OS: $(sw_vers -productVersion)"
echo "host kernel: $(uname -r)"
echo "SDK path: $(xcrun --sdk macosx --show-sdk-path)"
echo "SDK version: $(xcrun --sdk macosx --show-sdk-version)"
echo "Xcode: $(xcodebuild -version | tr '\n' ' ')"

opt=${FORT_DARWIN_OPT:-/opt/homebrew/bin/opt}
cc=$(xcrun --sdk macosx --find clang)
tools=(bash sh awk basename cat chmod cmp cmake ctest cp cut diff dirname file find git grep \
       head ln mkdir mktemp mv nm node od otool python3 rg rm sed shasum sort tail \
       tr uname wc xcrun xcode-select xcodebuild sw_vers ninja)
tools+=("$opt" "$cc")
for tool in "${tools[@]}"; do
    path=$(command -v "$tool") || {
        echo "darwin identity: missing tool $tool" >&2
        exit 2
    }
    resolved=$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$path")
    version="no independent version command; binary hash identifies this tool"
    case "$tool" in
    bash|cmake|ctest|git|node|python3|rg|xcrun|ninja|file|nm|otool)
        output=$("$path" --version 2>&1)
        version=${output%%$'\n'*} ;;
    xcodebuild)
        version=$("$path" -version | tr '\n' ' ') ;;
    xcode-select)
        version=$("$path" -version 2>&1) ;;
    "$opt"|"$cc")
        output=$("$path" --version 2>&1)
        version=${output%%$'\n'*} ;;
    esac
    echo "tool $tool: $resolved; $version; $(shasum -a 256 "$resolved" | awk '{print $1}')"
done

echo "darwin compiler: build/darwin/stage2/fort; $(shasum -a 256 build/darwin/stage2/fort | awk '{print $1}')"
echo "darwin LSP: build/darwin/fort-lsp; $(shasum -a 256 build/darwin/fort-lsp | awk '{print $1}')"
python3 tools/darwin_input_manifest.py
