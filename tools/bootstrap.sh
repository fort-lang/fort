#!/bin/bash
# Build the self-hosted compiler from the C bootstrap (D19.5).
#
# stage1 is build/<preset>/fort, the compiler in C; stage2 is src/fort compiled
# by stage1; stage3 is src/fort compiled by stage2. stage2 and stage3 are built
# from the same sources by two different compilers, so the two binaries must
# agree. That is the fixed point self-hosting means.
#
# This script builds stage1 and stage2. With --stage3 it builds stage1 and
# hands the fixed point to tools/fixpoint.sh, which checks it in both build
# modes and which ctest runs as the test `bootstrap`. The two therefore check
# the same thing: this script is the way to ask by hand, and the ctest is the
# way the gate asks. It builds no stage2 of its own then, because fixpoint.sh
# builds one per build mode and must not write build/<preset>/stage2/fort,
# which lang-stage2, diff-ir and stage-usage read.
#
# Runs in the guest, from anywhere in the worktree; the CMake target
# fort_stage2 runs the stage2 step of this script at every build.
set -eu

usage() {
    echo "usage: tools/bootstrap.sh [--preset <preset>] [--stage3]" >&2
}

preset=debug
stage3=no
while [ "$#" -gt 0 ]; do
    case "$1" in
        --preset)
            if [ "$#" -lt 2 ]; then
                echo "bootstrap.sh: --preset needs an argument" >&2
                usage
                exit 2
            fi
            preset=$2
            shift 2
            ;;
        --stage3)
            stage3=yes
            shift
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            echo "bootstrap.sh: unknown argument '$1'" >&2
            usage
            exit 2
            ;;
    esac
done

root=$(cd "$(dirname "$0")/.." && pwd)
build=$root/build/$preset
cd "$root"

if [ ! -d "$build" ]; then
    echo "bootstrap.sh: no build directory $build; configure the preset first" >&2
    exit 2
fi

# stage1 and the runtime object every generated program links against.
echo "== stage1: building the C bootstrap compiler (preset $preset)"
cmake --build "$build" --target fort fort_std

entry=$root/src/fort/main.ft
std=$build/std
# The compiler drives a clang over the LLVM IR it emits (D14.3), and the guest
# `cc` is a native gcc, so the target compiler is named explicitly.
cc=${FORT_TARGET_CC:-clang}
target=${FORT_TARGET_TRIPLE:-x86_64-linux-gnu}

if [ "$stage3" != yes ]; then
    echo "== stage2: compiling src/fort with stage1"
    mkdir -p "$build/stage2"
    "$build/fort" --std-dir "$std" --cc "$cc" --target "$target" \
        -o "$build/stage2/fort" "$entry"
    echo "stage2: $build/stage2/fort"
    echo "stage3 needs --stage3"
    exit 0
fi

# The fixed point, in both build modes, with the verifier over each module.
# fixpoint.sh builds stage2 and stage3 of each mode itself.
echo "== the fixed point (tools/fixpoint.sh)"
exec bash "$root/tools/fixpoint.sh" "$build" --cc "$cc" --target "$target"
