#!/bin/bash
# T-147: record slot-2 guest tools that build and verify the Linux seed.
set -euo pipefail

for tool in bash clang cmake ninja opt-18 python3 qemu-x86_64-static git \
            sha256sum sed find sort cp cmp awk head readlink tar tr wc; do
    path=$(command -v "$tool") || {
        echo "mac guest identity: missing $tool" >&2
        exit 2
    }
    resolved=$(readlink -f "$path")
    version="no independent version command; binary hash identifies this tool"
    case "$tool" in
    bash|clang|cmake|ninja|opt-18|python3|qemu-x86_64-static|git|\
    sha256sum|sed|find|sort|cp|cmp|awk|head|readlink|tar|tr|wc)
        output=$("$path" --version 2>&1)
        version=${output%%$'\n'*} ;;
    esac
    echo "guest tool $tool: $resolved; $version; $(sha256sum "$resolved" | awk '{print $1}')"
done
interpreter=/usr/libexec/qemu-binfmt/x86_64-binfmt-P
if [ ! -f "$interpreter" ]; then
    echo "mac guest identity: missing $interpreter" >&2
    exit 2
fi
echo "guest binfmt interpreter: $interpreter; $(sha256sum "$interpreter" | awk '{print $1}')"
