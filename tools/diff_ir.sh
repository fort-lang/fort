#!/bin/bash
# stage1 and stage2 must emit the same LLVM IR module for every .ft file in the
# repository that stage1 compiles.
#
# `fort -S <file>` runs the whole pipeline as far as the module of D19.1 and
# writes it out, stopping before --cc (toolchain.md 2). D19.5 makes that text a
# function of the program alone -- every counter is per function, every list is
# appended to in emission order and nothing is iterated out of a hash table --
# so two compilers that agree about a program must write the same bytes, and a
# byte that differs is a difference in the emitter and not in the weather.
#
# This is the emitter's differential oracle, the fourth beside
# tools/diff_tokens.sh, tools/diff_ast.sh and tools/diff_check.sh, and it is
# the strongest of the four, because it is the only one that sees the whole
# front end and the emitter at once: a type the checker built differently, a
# constant it folded differently or a symbol it resolved differently all reach
# the text here.
#
# It asks a second question of each module, and the two are apart. The first is
# differential: do the two emitters write the same bytes. The second is
# absolute: does LLVM read what each of them wrote. `opt -passes=verify` parses
# the module and runs the LLVM verifier over it, which is the check of D19.1.
# Two modules that agree may both be wrong, and a module LLVM cannot parse is
# the worst outcome a program has: a refusal tells the author something and a
# wrong answer is visible, while a parse error inside LLVM tells them nothing
# about their program. T-127 was filed for one of those, an i32 compared with a
# 64-bit float bit pattern that the checker passed, and T-128 fixed it.
#
# What this line adds, measured and not argued. run_tests.py --verify-ir
# already runs the same `opt` over every test that compiles, and CMake passes
# it to `lang` and `fort-modules` under stage1 and to `lang-stage2` under
# stage2. Of the PROGRAM_FILES programs below, 350 stand under test/lang and
# are verified under both compilers there; the other 177 are 173 test/fort
# module tests, 2 test/tty programs, src/lsp/main.ft and src/fort/main.ft.
# `fort-modules` is the only run of test/fort and it uses stage1, and there is
# no stage2 twin of it, so 176 of those 177 had their stage2 module verified by
# nothing. The exception is src/fort/main.ft, which tools/fixpoint.sh verifies
# under both compilers and in both build modes.
#
# The reach of that, stated as narrowly as it is true. This script verifies
# both modules of every file stage1 compiles into a module, and it skips every
# other file before either module exists, so a file stage1 refuses reaches no
# `opt` here. That is the family notes/testing.md names: a float literal, a
# `?:`, a `do`-`while` and a nested array or span level. On 2026-09-14, 409 of
# the 936 .ft files are skipped, 84 of them are programs stage2 compiles and
# stage1 refuses, and all 84 stand under test/lang, where lang-stage2 passes
# --verify-ir and reads them. So the gap is empty today and it is not closed:
# a program of that family added under test/fort, test/tty or src/ would have
# its stage2 module verified by nothing, because this script skips it and no
# other stage2 runner walks those directories. CMakeLists.txt says the same
# beside lang-stage2, which T-043 gave --verify-ir for exactly this reason.
#
# What neither this line nor --verify-ir can do is see a shape the corpus does
# not spell, which is how T-127 lived. tools/sweep_untyped.sh is the generated
# corpus beside this measured one, and it is where a new shape goes.
#
# Why only the files stage1 compiles. `-S` builds a program, so it demands an
# entry module that defines `main` (D8.6): a module of the compiler or of the
# standard library is not one, and neither is a `fail` test. What is compared
# is therefore every program the repository holds -- the `run` and `programs`
# tests of test/lang, the module tests of test/fort, and src/fort/main.ft
# itself, which is the compiler compiling itself and the largest single case
# there is.
#
# What it cannot see: a program the corpus does not spell. The text is held
# against stage1's, so a construct neither compiler is ever asked to emit is
# unjudged here and is pinned by test/fort/gen*_test.ft instead. It also says
# nothing about whether stage1's own text is right, which test/gen*_test.c and
# test/ir/*.ll are for.
#
# It is written like the other three, including their two guards: stage1's own
# answer is held against what an emitter must produce -- exit 0 and a module
# that begins with the target triple of D19.1 -- before the two are compared,
# because two compilers that both refuse `-S` agree about everything; and the
# file list is read line by line so that a path holding a space is one path.
#
# Usage: diff_ir.sh <build-dir> [--opt <opt>], from the top of the worktree.
set -eu

usage() {
    echo "usage: diff_ir.sh <build-dir> [--opt <opt>]" >&2
}

# The LLVM that reads the modules. It is an option because the build knows
# which one it configured: CMake resolves `opt-18` or else `opt` into
# FORT_OPT_PATH and passes it to lang, fort-modules, lang-stage2 and
# fixpoint.sh, and it passes it here as well, so the gate and a hand run
# measure one toolchain. Where find_program answers `opt` and not `opt-18`,
# a script that read the name alone would exit 2 while every other test
# passed. The environment is the fallback for a hand run, as in fixpoint.sh.
opt=${FORT_OPT:-opt-18}

if [ "$#" -lt 1 ]; then
    usage
    exit 2
fi
build=$1
shift
while [ "$#" -gt 0 ]; do
    if [ "$#" -lt 2 ]; then
        echo "diff_ir.sh: $1 needs an argument" >&2
        usage
        exit 2
    fi
    case "$1" in
        --opt) opt=$2 ;;
        *)
            echo "diff_ir.sh: unknown argument '$1'" >&2
            usage
            exit 2
            ;;
    esac
    shift 2
done
stage1=$build/fort
stage2=$build/stage2/fort

# A missing tool is a broken environment and not an emitter that disagreed with
# itself, so it exits 2 as tools/fixpoint.sh does.
command -v "$opt" >/dev/null || {
    echo "diff_ir.sh: $opt not found (llvm-18, see tools/provision.sh)" >&2
    exit 2
}

for binary in "$stage1" "$stage2"; do
    if [ ! -x "$binary" ]; then
        echo "diff_ir.sh: not built: $binary" >&2
        exit 2
    fi
done

# The library both runs read. An unbuilt pin would otherwise reach the
# compilers as a --std-dir that holds nothing, and two compilers that cannot
# find std.rt agree about every file.
if [ ! -f "$build/pin/0/std/rt.ft" ]; then
    echo "diff_ir.sh: not built: $build/pin/0/std (build the fort_stage2 target first)" >&2
    exit 2
fi

# The number of .ft files the repository holds, as tools/diff_ast.sh counts
# them: an equality and not a floor, so that a file cannot slip out of the
# comparison. A ticket that adds or removes a .ft file changes all four lines
# in the same commit.
FT_FILES=959

# The files of that corpus stage1 compiles into a module, which are the ones
# compared. It is an equality for the reason FT_FILES is: a comparison that
# shrank would otherwise pass while seeing less, and ctest reads the exit
# status and not the counts printed below.
# It is 21 above diff_check.sh's CLEAN_FILES, and the two sets are not nested:
# they differ by 106 files one way and 85 the other (measured by T-131, and by
# T-132 on its own intermediate commit, which read 106 and 85 while
# std/rt_float.ft stood empty). This script's roots are wider -- -I src
# -I src/fort -I test/fort/support reaches the 106 test/fort tests that import
# a support module, which that script cannot resolve and skips -- and its
# question is narrower, since the 85 files that check clean and hold no `main`
# compile into no module and are skipped here. diff_check.sh says beside its
# own constant how the two group sizes are measured.
# T-144 remeasured 509 clean, 530 programs, 85 clean-only, and 106 program-only.
PROGRAM_FILES=532

# The pass pipeline `opt` is given. `verify` alone parses the module and runs
# the LLVM verifier over it, which is the check of D19.1, and it runs no
# transform. It is named here so that one line adds a second check.
VERIFY_PASSES=verify

# The search roots every run is given: the standard library, src/fort so that
# the compiler's own modules resolve, test/fort/support so that a module test's
# fixture does, and src so that a module of the language server, whose module
# path is `lsp.<name>`, does as well (T-063). A file that needs none of them is
# unaffected, since a root that holds no module of the path is simply not the
# one that answers (D9.2).
#
# The library is pin 0's and not HEAD's, on both sides, for the reason
# tools/diff_check.sh gives at its own copy of this line (T-131). It is also
# what keeps the two modules comparable: a module holds the whole closure
# (D9.10) and names each file by the path the compiler opened it by (D19.5), so
# the two runs must be given one directory.
STD_DIR=$build/pin/0/std

files=$(find . -name '*.ft' -not -path './build/*' -not -path './.git/*' \
    -not -path './.worktrees/*' | sort)
count=$(printf '%s\n' "$files" | grep -c .)
if [ "$count" -ne "$FT_FILES" ]; then
    echo "diff_ir.sh: found $count .ft files, expected exactly $FT_FILES" >&2
    echo "diff_ir.sh: a ticket that adds or removes one raises or lowers FT_FILES;" >&2
    echo "diff_ir.sh: a count far below it means the walk itself is broken" >&2
    exit 1
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

status=0
differing=0
compared=0
skipped=0
verified=0

# `opt -passes=verify` over one module. It parses the text and runs the LLVM
# verifier, so it catches both a module LLVM cannot read and one it reads and
# rejects. -disable-output keeps it from writing the bitcode back out.
verify_module() {
    local which=$1
    local module=$2
    local file=$3
    if "$opt" "-passes=$VERIFY_PASSES" -disable-output "$module" \
        >"$work/verify.out" 2>"$work/verify.err"; then
        verified=$((verified + 1))
        return 0
    fi
    echo "diff_ir.sh: $file: LLVM refused the module $which emitted:" >&2
    head -n 20 "$work/verify.err" >&2
    return 1
}
while IFS= read -r file; do
    # Neither module of the previous file may stand here: a compiler that
    # exits 0 and writes nothing would otherwise make the diff read the file
    # before it and report agreement.
    rm -f "$work/one.ll" "$work/two.ll"
    set +e
    "$stage1" -S --std-dir "$STD_DIR" -I src -I src/fort -I test/fort/support \
        -o "$work/one.ll" "$file" >"$work/one.out" 2>"$work/one.err"
    one_status=$?
    set -e
    if [ "$one_status" -ne 0 ] || [ ! -s "$work/one.ll" ] ||
        ! grep -q '^target triple = "x86_64-unknown-linux-gnu"$' "$work/one.ll"; then
        # Not a program: no `main`, or a file that does not compile at all.
        # Its diagnostics are the language corpus's to compare. A stage1 that
        # exited 0 and wrote something that is not a module of D19.1 is
        # counted here too, and PROGRAM_FILES then falls below its equality
        # and fails the run.
        skipped=$((skipped + 1))
        continue
    fi
    compared=$((compared + 1))
    # One defect must not hide another. A stage1 module the verifier refuses
    # is a failure of this file, and the run goes on to emit stage2's module
    # and to read it, so a stage2 defect on the same file is reported in the
    # same run rather than in the one after the stage1 fix. Only a stage2 that
    # wrote no module at all stops the file, because there is then nothing to
    # verify and nothing to compare.
    file_status=0
    if ! verify_module stage1 "$work/one.ll" "$file"; then
        file_status=1
    fi
    set +e
    "$stage2" -S --std-dir "$STD_DIR" -I src -I src/fort -I test/fort/support \
        -o "$work/two.ll" "$file" >"$work/two.out" 2>"$work/two.err"
    two_status=$?
    set -e
    if [ "$two_status" -ne 0 ]; then
        echo "diff_ir.sh: $file: stage1 emitted a module, stage2 exited $two_status" >&2
        head -n 20 "$work/two.err" >&2
        status=1
        differing=$((differing + 1))
        continue
    fi
    if ! verify_module stage2 "$work/two.ll" "$file"; then
        file_status=1
    fi
    if ! diff -u "$work/one.ll" "$work/two.ll" >"$work/module.diff"; then
        echo "diff_ir.sh: $file: the modules differ:" >&2
        head -n 40 "$work/module.diff" >&2
        file_status=1
    fi
    if [ "$file_status" -ne 0 ]; then
        status=1
        differing=$((differing + 1))
    fi
done <<EOF
$files
EOF

if [ "$compared" -ne "$PROGRAM_FILES" ]; then
    echo "diff_ir.sh: compared $compared files, expected exactly $PROGRAM_FILES" >&2
    echo "diff_ir.sh: a ticket that adds a program, or that changes which files stage1" >&2
    echo "diff_ir.sh: compiles, raises or lowers PROGRAM_FILES beside FT_FILES;" >&2
    echo "diff_ir.sh: a count far below it means stage1 refused a corpus it used to pass" >&2
    exit 1
fi
# The verified count is twice PROGRAM_FILES, one module per compiler, and it
# is an equality for the reason PROGRAM_FILES is: a run that verified fewer
# modules than the corpus holds would otherwise pass while seeing less.
#
# It is held against the constant and not against `compared`, and it runs
# whatever `status` is. Against `compared` and under `status -eq 0` it was a
# tautology: every path that skips a verify sets `status` to 1 on the way, so
# the test could never fire. Against the constant it measures the run.
if [ "$verified" -ne $((PROGRAM_FILES * 2)) ]; then
    echo "diff_ir.sh: verified $verified modules, expected $((PROGRAM_FILES * 2))" >&2
    echo "diff_ir.sh: one module per compiler for each of the $PROGRAM_FILES programs;" >&2
    echo "diff_ir.sh: a count below it means a module was emitted and never read" >&2
    status=1
fi
if [ "$status" -eq 0 ]; then
    echo "stage1 and stage2 emit the same module for the $compared .ft files of the"
    echo "repository that compile, and LLVM reads all $verified of those modules;"
    echo "$skipped of $count are not programs and are the language corpus's to compare"
elif [ "$differing" -ne 0 ]; then
    echo "diff_ir.sh: $differing of $compared compared .ft files differ" >&2
fi
exit "$status"
