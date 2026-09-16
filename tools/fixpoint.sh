#!/bin/bash
# tools/fixpoint.sh <build-dir>: the compiler must reproduce itself, in both
# build modes (D19.5).
#
# The compiler that builds HEAD is the last pin of tools/bootstrap.ref.
# The verified seed reached it through each prior source pin (D14.7).
# --bootstrap names that last pin. Stage2 is HEAD's src/fort compiled by the
# pin. Stage3 uses stage2, and stage4 uses stage3.
#
# Which pair D19.5 compares, and why it is not the pair it used to be. The last
# pin and HEAD are different programs, so their `-S` texts differ on any commit
# that touches the emitter, and a comparison of the pin's module with stage2's
# would go red on every such commit. The two comparisons D19.5 asks for must
# both hold two things that embody HEAD's sources. stage2 and stage3 are the
# first such pair: HEAD's sources through two different compilers. stage3 and
# stage4 are the second. So this script compares the module stage2 and stage3
# emit, and the stage3 and stage4 binaries, and it runs `-S` twice and not
# three times, which is what D19.5's last sentence forbids.
#
# The check runs twice, once in each build mode. Checked mode traps on an
# overflow and release mode does not (D11.1), so the two modes emit different
# code and a fixed point in one mode does not prove the other.
#
# Each mode compares two things, in this order.
#
# 1. The LLVM IR module. stage2 emits the module of src/fort, and stage3
#    emits it again. D19.5 makes that text a function of the program alone,
#    and stage2 and stage3 are one program, so the two texts must be the same
#    bytes. Both modules pass `opt -passes=verify` first (D19.1): a module that
#    the verifier refuses is a broken compiler even when the two texts agree.
# 2. The two binaries. stage3 is clang over stage2's module and stage4 is
#    clang over stage3's module, so equal modules give equal binaries while
#    clang is deterministic. The binary comparison is what removes that
#    assumption: it compares the artefacts and not a reading of them.
#
# The module comparison comes first because it names the part that failed.
# Two identical modules that link to different bytes are clang or the linker
# and not the compiler, and the two failures have different causes.
#
# It builds all six binaries itself, under <build-dir>/fixpoint/<mode>, and
# the three stages of a mode differ in nothing that reaches the module except
# the compiler and the `-o` path. The module does not hold the output path.
# The darwin target uses one link path for stage3 and stage4, as D19.5 requires.
#
# Everything that does reach the module must be spelled the same way for the
# two stages, and the file paths are what reach it. The compiler names
# each file the path it opened it by (D14.2) and writes that path into the
# module as the file constant of a failure block (D19.6, D11.4). So
# `--std-dir build/debug/std` and `--std-dir /vagrant/build/debug/std` give
# two different programs. The measurement: the CMake target fort_stage2
# compiles `src/fort/main.ft` by its absolute path, the same compile from the
# top of the worktree gives it a relative path, and the two binaries differ in
# 27233 of 480088 bytes. Both binaries run.
#
# It also writes no file that another test reads. <build-dir>/stage2/fort
# belongs to the CMake target fort_stage2, and lang-stage2, diff-ir and
# stage-usage judge it while this test runs. The source chain owns each
# <build-dir>/pin/<n>/fort.
#
# ctest runs it as the test `bootstrap`, label lang.
set -eu

usage() {
    echo "usage: fixpoint.sh <build-dir> [--bootstrap <fort>] [--cc <clang>]" >&2
    echo "                   [--target <triple>] [--opt <opt>] [--entry <file>]" >&2
    echo "                   [--std <dir>] [--source-root <dir>]" >&2
}

# The compiler drives a clang over the LLVM IR it emits (D14.3), and the guest
# `cc` is a native gcc, so the target compiler is named. Each tool is an
# option, because the build knows which one it configured: CMake passes the
# same three in the ctest and in the check-lang command, so the gate and the
# test measure one toolchain. The environment is the fallback for a hand run.
cc=${FORT_TARGET_CC:-clang}
opt=${FORT_OPT:-opt-18}
target=${FORT_TARGET_TRIPLE:-x86_64-linux-gnu}
# The compiler that builds HEAD. CMake passes it, and a hand run reads the last
# pin off tools/bootstrap.ref, so the two measure one chain.
bootstrap=
entry=
std=
source_root=

if [ "$#" -lt 1 ]; then
    usage
    exit 2
fi
build=$1
shift
while [ "$#" -gt 0 ]; do
    if [ "$#" -lt 2 ]; then
        echo "fixpoint.sh: $1 needs an argument" >&2
        usage
        exit 2
    fi
    case "$1" in
        --bootstrap) bootstrap=$2 ;;
        --cc) cc=$2 ;;
        --target) target=$2 ;;
        --opt) opt=$2 ;;
        --entry) entry=$2 ;;
        --std) std=$2 ;;
        --source-root) source_root=$2 ;;
        *)
            echo "fixpoint.sh: unknown argument '$1'" >&2
            usage
            exit 2
            ;;
    esac
    shift 2
done
root=$(cd "$(dirname "$0")/.." && pwd)
entry=${entry:-$root/src/fort/main.ft}
std=${std:-$build/std}
source_root=${source_root:-$(dirname "$entry")}
if [ -z "$bootstrap" ]; then
    bootstrap=$build/pin/$(bash "$root/tools/pin.sh" last)/fort
fi
export QEMU_LD_PREFIX=${QEMU_LD_PREFIX:-/usr/x86_64-linux-gnu}

case "$target" in
x86_64-linux-gnu)
    expected_triple=x86_64-unknown-linux-gnu
    target_name=linux
    ;;
arm64-apple-macosx[0-9]*.[0-9]*.[0-9]*)
    if ! [[ "$target" =~ ^arm64-apple-macosx[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        echo "fixpoint.sh: unsupported target '$target'" >&2
        exit 2
    fi
    expected_triple=$target
    target_name=darwin
    ;;
*)
    echo "fixpoint.sh: unsupported target '$target'" >&2
    exit 2
    ;;
esac

if [ ! -x "$bootstrap" ]; then
    echo "fixpoint.sh: not built: $bootstrap (build the fort_stage2 target first)" >&2
    exit 2
fi
if [ ! -f "$entry" ] || [ ! -d "$std" ] || [ ! -d "$source_root" ]; then
    echo "fixpoint.sh: target entry, source root, or standard root is missing" >&2
    exit 2
fi

# A missing tool is a broken environment and not a compiler that failed to
# reproduce itself, so it exits 2 as test/pipeline_test.sh does.
for tool in "$opt" "$cc"; do
    command -v "$tool" >/dev/null || {
        echo "fixpoint.sh: $tool not found (llvm-18 and clang, see tools/provision.sh)" >&2
        exit 2
    }
done

work=$build/fixpoint
mkdir -p "$work"

status=0

# compile <compiler> <mode-flags> <output> -- src/fort through one compiler.
# The previous run's binary may not stand here: a compiler that exits 0 and
# writes nothing would otherwise make cmp compare two stale files and report
# agreement. It is the guard the two modules carry below, for the same reason.
compile() {
    local compiler=$1
    local flags=$2
    local output=$3
    mkdir -p "$(dirname "$output")"
    rm -f "$output"
    # $flags is empty in checked mode and `--release` in release mode. It is
    # one option and not a path, so the split of an empty $flags into no
    # argument is what is wanted here.
    # shellcheck disable=SC2086
    "$compiler" $flags --std-dir "$std" -I "$source_root" \
        --cc "$cc" --target "$target" \
        -o "$output" "$entry"
    if [ ! -x "$output" ]; then
        echo "fixpoint.sh: $compiler exited 0 and wrote no $output" >&2
        return 1
    fi
}

# is_module <file> -- the file holds a module of D19.1 and not nothing. Two
# empty files compare equal and opt accepts an empty module, so a compiler
# that exits 0 and writes nothing would otherwise read as a fixed point.
# tools/diff_ir.sh carries the same guard.
is_module() {
    if [ ! -s "$1" ]; then
        echo "fixpoint.sh: $1 is empty" >&2
        return 1
    fi
    if ! grep -q "^target triple = \"$expected_triple\"$" "$1"; then
        echo "fixpoint.sh: $1 is not a module of D19.1" >&2
        return 1
    fi
}

# emit <compiler> <mode-flags> <output.ll> -- the module of src/fort.
emit() {
    local compiler=$1
    local flags=$2
    local output=$3
    # `-S` stops before --cc (toolchain.md 2), so it names no target compiler.
    # It keeps --target, which names the triple the module carries (D14.1).
    # shellcheck disable=SC2086
    "$compiler" $flags -S --std-dir "$std" -I "$source_root" --target "$target" \
        -o "$output" "$entry"
}

# check_mode <name> <mode-flags> -- the fixed point in one build mode.
check_mode() {
    local mode=$1
    local flags=$2
    local two=$work/$mode/stage2/fort
    local three=$work/$mode/stage3/fort
    local four=$work/$mode/stage4/fort
    local two_ll=$work/$mode/stage2.ll
    local three_ll=$work/$mode/stage3.ll

    echo "== $mode: compiling src/fort with the last pin"
    mkdir -p "$work/$mode"
    if ! compile "$bootstrap" "$flags" "$two"; then
        echo "fixpoint.sh: $mode: the last pin could not compile src/fort" >&2
        status=1
        return
    fi

    echo "== $mode: stage3"
    if [ "$target_name" = darwin ]; then
        three=$work/$mode/stage3.copy
        four=$work/$mode/fort
        if ! compile "$two" "$flags" "$four"; then
            echo "fixpoint.sh: $mode: stage2 could not compile src/fort" >&2
            status=1
            return
        fi
        cp "$four" "$three"
    elif ! compile "$two" "$flags" "$three"; then
        echo "fixpoint.sh: $mode: stage2 could not compile src/fort" >&2
        status=1
        return
    fi

    echo "== $mode: the modules stage2 and stage3 emit"
    # Neither module of the other mode may stand here, for the reason the
    # binaries above are removed: a compiler that exits 0 and writes nothing
    # would otherwise make cmp read the previous file and report agreement.
    rm -f "$two_ll" "$three_ll"
    if ! emit "$two" "$flags" "$two_ll"; then
        echo "fixpoint.sh: $mode: stage2 emitted no module for src/fort" >&2
        status=1
        return
    fi
    if ! emit "$three" "$flags" "$three_ll"; then
        echo "fixpoint.sh: $mode: stage3 emitted no module for src/fort" >&2
        status=1
        return
    fi
    for module in "$two_ll" "$three_ll"; do
        if ! is_module "$module"; then
            echo "fixpoint.sh: $mode: $module is no module to compare" >&2
            status=1
            return
        fi
        if ! "$opt" -passes=verify -disable-output "$module"; then
            echo "fixpoint.sh: $mode: the verifier refused $module" >&2
            status=1
            return
        fi
    done
    if cmp -s "$two_ll" "$three_ll"; then
        echo "$mode: the emitted modules are identical and both verify"
    else
        # D19.5 asks for cmp with diff as the debugging output. The module is
        # about 3 MB, so the first 40 lines of the diff stand for it.
        echo "fixpoint.sh: $mode: the modules stage2 and stage3 emit differ" >&2
        echo "fixpoint.sh:   diff $two_ll $three_ll" >&2
        diff -u "$two_ll" "$three_ll" >"$work/$mode/module.diff" || true
        head -n 40 "$work/$mode/module.diff" >&2
        status=1
        return
    fi

    echo "== $mode: stage4"
    if [ "$target_name" = darwin ]; then
        rm -f "$four"
    fi
    if ! compile "$three" "$flags" "$four"; then
        echo "fixpoint.sh: $mode: stage3 could not compile src/fort" >&2
        status=1
        return
    fi
    if cmp -s "$three" "$four"; then
        echo "$mode: stage3 and stage4 are identical: the compiler is self-hosted"
    else
        echo "fixpoint.sh: $mode: the emitted modules agree but the binaries differ;" >&2
        echo "fixpoint.sh: $mode: that is clang or the linker, not the compiler" >&2
        status=1
    fi
}

check_mode checked ""
check_mode release --release

exit "$status"
