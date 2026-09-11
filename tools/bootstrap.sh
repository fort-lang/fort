#!/bin/bash
# Build the self-hosted compiler from the C bootstrap (notes/toolchain.md 8).
#
# stage1 is build/<preset>/fort, the compiler in C; stage2 is src/fort compiled
# by stage1; stage3 is src/fort compiled by stage2. stage2 and stage3 are built
# from the same sources by two different compilers, so once stage2 compiles
# what stage1 compiles the two binaries must agree -- that is the fixed point
# self-hosting means, and --stage3 is what checks it.
#
# Until then stage2 is a driver skeleton that cannot compile a program, so
# stage3 is not attempted unless it is asked for. Runs in the guest, from
# anywhere in the worktree; the CMake target fort_stage2 runs the stage2 step
# of this script at every build.
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
cmake --build "$build" --target fort fort_rt

entry=$root/src/fort/main.ft
std=$build/std
# The compiler drives a clang over the LLVM IR it emits (D14.3), and the guest
# `cc` is a native gcc, so the target compiler is named explicitly.
cc=${FORT_TARGET_CC:-clang}
target=${FORT_TARGET_TRIPLE:-x86_64-linux-gnu}

echo "== stage2: compiling src/fort with stage1"
mkdir -p "$build/stage2"
"$build/fort" --std-dir "$std" --cc "$cc" --target "$target" \
    -o "$build/stage2/fort" "$entry"

if [ "$stage3" != yes ]; then
    echo "stage2: $build/stage2/fort"
    echo "stage3 is not attempted; pass --stage3 once stage2 can compile src/fort"
    exit 0
fi

# stage2 is built for the target, so it runs under qemu-user; binfmt runs it
# transparently, as it does every program the compiler builds.
echo "== stage3: compiling src/fort with stage2"
mkdir -p "$build/stage3"
"$build/stage2/fort" --std-dir "$std" --cc "$cc" --target "$target" \
    -o "$build/stage3/fort" "$entry"

# The emitted LLVM IR is the function of the program the compiler is (D19.5),
# so the text is compared first: two identical modules that link to different
# bytes are clang or the linker being nondeterministic, which is not the
# compiler failing to be a fixed point, and the two cases are worth telling
# apart before anyone debugs the wrong one.
echo "== comparing the modules stage1 and stage2 emit"
"$build/fort" --std-dir "$std" -S -o "$build/stage2/fort.ll" "$entry"
"$build/stage2/fort" --std-dir "$std" -S -o "$build/stage3/fort.ll" "$entry"
if cmp -s "$build/stage2/fort.ll" "$build/stage3/fort.ll"; then
    echo "the emitted modules are identical"
else
    echo "bootstrap.sh: the modules stage1 and stage2 emit differ" >&2
    echo "  diff $build/stage2/fort.ll $build/stage3/fort.ll" >&2
    exit 1
fi

echo "== comparing stage2 and stage3"
if cmp -s "$build/stage2/fort" "$build/stage3/fort"; then
    echo "stage2 and stage3 are identical: the compiler is self-hosted"
    exit 0
fi
echo "bootstrap.sh: the emitted modules agree but the binaries differ;" >&2
echo "that is clang or the linker, not the compiler" >&2
exit 1
