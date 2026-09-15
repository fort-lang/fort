#!/bin/bash
# Build the self-hosted compiler through the chain of pinned commits (D19.5).
#
# stage1 is build/<preset>/fort, the compiler in C. It builds pin 0's src/fort
# and each pin builds the next (tools/bootstrap.ref, notes/compiler.md 8); the
# last pin builds HEAD's src/fort into build/<preset>/stage2/fort. stage3 is
# src/fort compiled by stage2 and stage4 is src/fort compiled by stage3.
# stage3 and stage4 are built from the same sources by two different
# compilers, so the two binaries must agree. That is the fixed point
# self-hosting means.
#
# This script builds the chain and stage2 through the CMake target fort_stage2,
# which is the one description of the chain. With --stage3 it goes on to
# tools/fixpoint.sh, which checks the fixed point in both build modes and which
# ctest runs as the test `bootstrap`. The two therefore check the same thing:
# this script is the way to ask by hand, and the ctest is the way the gate
# asks. fixpoint.sh builds a stage2 per build mode of its own and must not
# write build/<preset>/stage2/fort, which lang-stage2, diff-ir and stage-usage
# read.
#
# Runs in the guest, from anywhere in the worktree; the CMake target
# fort_stage2 runs the stage2 step of this script at every build.
set -eu

usage() {
    echo "usage: tools/bootstrap.sh [--preset <preset>] [--stage3]" >&2
}

# The chain runs in the guest; the binaries it builds are x86-64 (D14.1).

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

# stage1, every pin of tools/bootstrap.ref, and stage2. The CMake target holds
# the chain, so this script never spells a hop: a pin added to the ref file is
# built here with no edit.
echo "== the chain: stage1, the pins of tools/bootstrap.ref, then stage2 (preset $preset)"
cmake --build "$build" --target fort_std fort_stage2

# The compiler drives a clang over the LLVM IR it emits (D14.3), and the guest
# `cc` is a native gcc, so the target compiler is named explicitly.
cc=${FORT_TARGET_CC:-clang}
target=${FORT_TARGET_TRIPLE:-x86_64-linux-gnu}
bootstrap=$build/pin/$(bash "$root/tools/pin.sh" last)/fort

if [ "$stage3" != yes ]; then
    echo "last pin: $bootstrap"
    echo "stage2: $build/stage2/fort"
    echo "stage3 and stage4 need --stage3"
    exit 0
fi

# The fixed point, in both build modes, with the verifier over each module.
# fixpoint.sh builds stage2, stage3 and stage4 of each mode itself.
echo "== the fixed point (tools/fixpoint.sh)"
exec bash "$root/tools/fixpoint.sh" "$build" --bootstrap "$bootstrap" \
    --cc "$cc" --target "$target"
