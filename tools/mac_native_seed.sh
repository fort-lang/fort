#!/bin/bash
# Build the darwin compiler through the source chain (D14.7).
#
# T-152 removes this transitional wrapper and its old file name. The wrapper
# keeps the current darwin CMake graph while bootstrap_chain.sh owns the chain.
set -eu

if [ "$#" -ne 1 ] || [ "$(uname -sm)" != "Darwin arm64" ]; then
    echo "usage: mac_native_seed.sh <darwin-build-dir> on darwin arm64" >&2
    exit 2
fi
if [ -z "${FORT_BOOTSTRAP_SEED:-}" ]; then
    echo "mac_native_seed.sh: FORT_BOOTSTRAP_SEED must name the darwin seed" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd -P)
build=$(cd "$1" && pwd -P)
case "$build" in
"$root"/*) ;;
*) echo "mac_native_seed.sh: build directory is outside $root" >&2; exit 2 ;;
esac

cc=$(xcrun --sdk macosx --find clang)
SDKROOT=$(xcrun --sdk macosx --show-sdk-path)
export SDKROOT
opt=${FORT_MAC_OPT:-/opt/homebrew/bin/opt}

bash "$root/tools/bootstrap_chain.sh" darwin "$FORT_BOOTSTRAP_SEED" "$build" \
    --cc "$cc" --opt "$opt"

# T-152 moves the CMake output to stage2/fort. Keep the current path until
# that ticket changes all darwin consumers together.
cp "$build/stage2/fort" "$build/fort"
chmod 755 "$build/fort"
echo "darwin stage2: the verified seed built the source chain and HEAD"
