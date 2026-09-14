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
# Usage: diff_ir.sh <build-dir>, from the top of the worktree.
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: diff_ir.sh <build-dir>" >&2
    exit 2
fi
build=$1
stage1=$build/fort
stage2=$build/stage2/fort

for binary in "$stage1" "$stage2"; do
    if [ ! -x "$binary" ]; then
        echo "diff_ir.sh: not built: $binary" >&2
        exit 2
    fi
done

# The number of .ft files the repository holds, as tools/diff_ast.sh counts
# them: an equality and not a floor, so that a file cannot slip out of the
# comparison. A ticket that adds or removes a .ft file changes all four lines
# in the same commit.
FT_FILES=935

# The files of that corpus stage1 compiles into a module, which are the ones
# compared. It is an equality for the reason FT_FILES is: a comparison that
# shrank would otherwise pass while seeing less, and ctest reads the exit
# status and not the counts printed below.
# It is 22 above diff_check.sh's CLEAN_FILES, and the two sets are not nested:
# they differ by 106 files one way and 84 the other. This script's roots are
# wider -- -I src -I src/fort -I test/fort/support reaches the 106 test/fort
# tests that import a support module, which that script cannot resolve and
# skips -- and its question is narrower, since the 84 files that check clean
# and hold no `main` compile into no module and are skipped here. diff_check.sh
# says beside its own constant how the two group sizes are measured.
PROGRAM_FILES=527

# The search roots every run is given: the standard library the build copied,
# src/fort so that the compiler's own modules resolve, test/fort/support so
# that a module test's fixture does, and src so that a module of the language
# server, whose module path is `lsp.<name>`, does as well (T-063). A file that
# needs none of them is unaffected, since a root that holds no module of the
# path is simply not the one that answers (D9.2).
STD_DIR=$build/std

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
    if ! diff -u "$work/one.ll" "$work/two.ll" >"$work/module.diff"; then
        echo "diff_ir.sh: $file: the modules differ:" >&2
        head -n 40 "$work/module.diff" >&2
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
if [ "$status" -eq 0 ]; then
    echo "stage1 and stage2 emit the same module for the $compared .ft files of the"
    echo "repository that compile; $skipped of $count are not programs and are the"
    echo "language corpus's to compare"
else
    echo "diff_ir.sh: $differing of $compared compared .ft files differ" >&2
fi
exit "$status"
