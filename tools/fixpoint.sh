#!/bin/bash
# tools/fixpoint.sh <build-dir>: the compiler must reproduce itself, in both
# build modes (D19.5).
#
# stage1 is <build-dir>/fort, the compiler in C. stage2 is src/fort compiled
# by stage1. stage3 is src/fort compiled by stage2. stage2 and stage3 come
# from the same sources and from two different compilers, so they must be the
# same program. That is the fixed point.
#
# The check runs twice, once in each build mode. Checked mode traps on an
# overflow and release mode does not (D11.1), so the two modes emit different
# code and a fixed point in one mode does not prove the other.
#
# Each mode compares two things, in this order.
#
# 1. The LLVM IR module. stage1 emits the module of src/fort, and stage2
#    emits it again. D19.5 makes that text a function of the program alone,
#    so the two texts must be the same bytes. Both modules pass
#    `opt -passes=verify` first (D19.1): a module that the verifier refuses
#    is a broken compiler even when the two texts agree.
# 2. The two binaries. stage2 is clang over stage1's module and stage3 is
#    clang over stage2's module, so equal modules give equal binaries while
#    clang is deterministic. The binary comparison is what removes that
#    assumption: it compares the artefacts and not a reading of them.
#
# The module comparison comes first because it names the part that failed.
# Two identical modules that link to different bytes are clang or the linker
# and not the compiler, and the two failures have different causes.
#
# It builds all four binaries itself, under <build-dir>/fixpoint/<mode>, and
# stage2 and stage3 of a mode differ in nothing that reaches the module: the
# compiler, and the `-o` path, which no module holds. Two reasons.
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
# stage-usage judge it while this test runs.
#
# ctest runs it as the test `bootstrap`, label lang.
set -eu

usage() {
    echo "usage: fixpoint.sh <build-dir> [--cc <clang>] [--target <triple>] [--opt <opt>]" >&2
}

# The compiler drives a clang over the LLVM IR it emits (D14.3), and the guest
# `cc` is a native gcc, so the target compiler is named. Each tool is an
# option, because the build knows which one it configured: CMake passes the
# same three in the ctest and in the check-lang command, so the gate and the
# test measure one toolchain. The environment is the fallback for a hand run.
cc=${FORT_TARGET_CC:-clang}
opt=${FORT_OPT:-opt-18}
target=${FORT_TARGET_TRIPLE:-x86_64-linux-gnu}

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
        --cc) cc=$2 ;;
        --target) target=$2 ;;
        --opt) opt=$2 ;;
        *)
            echo "fixpoint.sh: unknown argument '$1'" >&2
            usage
            exit 2
            ;;
    esac
    shift 2
done
root=$(cd "$(dirname "$0")/.." && pwd)
entry=$root/src/fort/main.ft
stage1=$build/fort
std=$build/std
export QEMU_LD_PREFIX=${QEMU_LD_PREFIX:-/usr/x86_64-linux-gnu}

if [ ! -x "$stage1" ]; then
    echo "fixpoint.sh: not built: $stage1 (build the fort target first)" >&2
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
    "$compiler" $flags --std-dir "$std" --cc "$cc" --target "$target" \
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
    if ! grep -q '^target triple = "x86_64-unknown-linux-gnu"$' "$1"; then
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
    "$compiler" $flags -S --std-dir "$std" --target "$target" \
        -o "$output" "$entry"
}

# check_mode <name> <mode-flags> -- the fixed point in one build mode.
check_mode() {
    local mode=$1
    local flags=$2
    local two=$work/$mode/stage2/fort
    local three=$work/$mode/stage3/fort
    local one_ll=$work/$mode/stage1.ll
    local two_ll=$work/$mode/stage2.ll

    echo "== $mode: compiling src/fort with stage1"
    mkdir -p "$work/$mode"
    if ! compile "$stage1" "$flags" "$two"; then
        echo "fixpoint.sh: $mode: stage1 could not compile src/fort" >&2
        status=1
        return
    fi

    echo "== $mode: the modules stage1 and stage2 emit"
    # Neither module of the other mode may stand here, for the reason the
    # binaries above are removed: a compiler that exits 0 and writes nothing
    # would otherwise make cmp read the previous file and report agreement.
    rm -f "$one_ll" "$two_ll"
    if ! emit "$stage1" "$flags" "$one_ll"; then
        echo "fixpoint.sh: $mode: stage1 emitted no module for src/fort" >&2
        status=1
        return
    fi
    if ! emit "$two" "$flags" "$two_ll"; then
        echo "fixpoint.sh: $mode: stage2 emitted no module for src/fort" >&2
        status=1
        return
    fi
    for module in "$one_ll" "$two_ll"; do
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
    if cmp -s "$one_ll" "$two_ll"; then
        echo "$mode: the emitted modules are identical and both verify"
    else
        # D19.5 asks for cmp with diff as the debugging output. The module is
        # about 3 MB, so the first 40 lines of the diff stand for it.
        echo "fixpoint.sh: $mode: the modules stage1 and stage2 emit differ" >&2
        echo "fixpoint.sh:   diff $one_ll $two_ll" >&2
        diff -u "$one_ll" "$two_ll" >"$work/$mode/module.diff" || true
        head -n 40 "$work/$mode/module.diff" >&2
        status=1
        return
    fi

    echo "== $mode: stage3"
    if ! compile "$two" "$flags" "$three"; then
        echo "fixpoint.sh: $mode: stage2 could not compile src/fort" >&2
        status=1
        return
    fi
    if cmp -s "$two" "$three"; then
        echo "$mode: stage2 and stage3 are identical: the compiler is self-hosted"
    else
        echo "fixpoint.sh: $mode: the emitted modules agree but the binaries differ;" >&2
        echo "fixpoint.sh: $mode: that is clang or the linker, not the compiler" >&2
        status=1
    fi
}

check_mode checked ""
check_mode release --release

exit "$status"
