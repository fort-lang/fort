#!/bin/bash
# T-147: record the host, guest seed, tools, and fort input paths.
set -euo pipefail

if [ "$#" -ne 0 ] || [ "$(uname -sm)" != "Darwin arm64" ]; then
    echo "mac identity: requires a Mac arm64 host and no arguments" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$root"
if [ -n "$(git status --porcelain --untracked-files=all)" ]; then
    echo "mac identity: source tree is not clean" >&2
    exit 1
fi
for input in build/debug/stage2/fort build/mac-native/fort \
             build/mac-native/fort-lsp build/mac-native/std/libc.ft \
             build/mac-native/std/net.ft; do
    if [ ! -f "$input" ]; then
        echo "mac identity: missing $input" >&2
        exit 1
    fi
done

echo "source SHA: $(git rev-parse HEAD)"
echo "main SHA: $(git rev-parse main)"
echo "git status: empty"
echo "host CPU: $(uname -m)"
echo "host OS: $(sw_vers -productVersion)"
echo "host kernel: $(uname -r)"
echo "SDK path: $(xcrun --sdk macosx --show-sdk-path)"
echo "SDK version: $(xcrun --sdk macosx --show-sdk-version)"
echo "Xcode: $(xcodebuild -version | tr '\n' ' ')"

opt=${FORT_MAC_OPT:-/opt/homebrew/bin/opt}
cc=$(xcrun --sdk macosx --find clang)
tools=(bash sh awk basename cat cmp cmake cp cut diff dirname file find git grep \
       head ln mkdir mktemp nm node od otool python3 rg rm sed shasum sort tail \
       tr uname wc xcrun xcode-select xcodebuild sw_vers ninja vagrant \
       VBoxManage ssh)
tools+=("$opt" "$cc")
for tool in "${tools[@]}"; do
    path=$(command -v "$tool") || {
        echo "mac identity: missing tool $tool" >&2
        exit 2
    }
    resolved=$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$path")
    version="no independent version command; binary hash identifies this tool"
    case "$tool" in
    bash|cmake|git|node|python3|rg|xcrun|ninja|file|VBoxManage|nm|otool)
        output=$("$path" --version 2>&1)
        version=${output%%$'\n'*} ;;
    vagrant)
        output=$("$path" --version 2>&1)
        version=${output%%$'\n'*} ;;
    ssh)
        output=$("$path" -V 2>&1)
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

echo "Linux seed: build/debug/stage2/fort; $(shasum -a 256 build/debug/stage2/fort | awk '{print $1}')"
echo "Mac compiler: build/mac-native/fort; $(shasum -a 256 build/mac-native/fort | awk '{print $1}')"
echo "Mac LSP: build/mac-native/fort-lsp; $(shasum -a 256 build/mac-native/fort-lsp | awk '{print $1}')"
python3 tools/mac_input_manifest.py

common=$(git rev-parse --git-common-dir)
main_root=$(cd "$(dirname "$common")" && pwd -P)
uuid="$main_root/.vagrant-2/machines/default/virtualbox/id"
if [ ! -f "$uuid" ]; then
    echo "mac identity: missing VM slot 2 UUID" >&2
    exit 1
fi
echo "VM directory: $main_root; slot 2; UUID $(cat "$uuid")"
echo "main Vagrantfile: $(shasum -a 256 "$main_root/Vagrantfile" | awk '{print $1}')"
FORT_VM_SLOT=2 tools/vm run 'uname -rm; sha256sum /var/lib/dpkg/status /etc/profile.d/fort.sh /proc/sys/kernel/core_pattern /proc/sys/fs/binfmt_misc/qemu-x86_64'
FORT_VM_SLOT=2 tools/vm run 'bash tools/mac_guest_identity.sh'
