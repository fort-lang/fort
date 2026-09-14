#!/bin/bash
# stage1 and stage2 must agree about every .ft file in the repository that
# checks clean.
#
# `fort --check <file>` runs the front end and stops: it reads the file, walks
# its import closure and checks every module of it, emitting nothing (D20.1).
# This script runs it with both compilers over every .ft file there is and
# compares the diagnostics and the exit statuses of the files stage1 accepts.
#
# Why only those files. src/fort/check.ft is the ported half of the checker:
# it resolves the declarations of a module and stops before the function
# bodies, which are src/bootstrap/check_stmt.c's and are not ported yet
# (T-036). A file stage1 refuses is therefore refused by stage2 only when its
# diagnostic comes from a declaration, and the language corpus under stage2
# (ctest lang-stage2, with test/lang/xfail-stage2.txt) is what holds those
# diagnostics against stage1's, test by test and position by position. What
# no test could see before this script is the other direction: a file that
# checks clean under stage1 and that the port refuses, or crashes on. That is
# the false positive a transliteration makes -- a rule applied where it does
# not hold, a type built wrong, a lookup that misses -- and it is silent
# everywhere else, since the corpus's clean files are `run` tests, which stage2
# cannot build yet and which are expected failures to a test harness.
#
# So the corpus here is the whole repository, and it is a strong one: the
# compiler's own thirteen thousand lines of fort and the standard library are
# in it, and `--check` over src/fort/main.ft alone resolves every declaration
# of every module of the compiler. A change that made stage1 refuse half the
# corpus would shrink the comparison in silence, so the number of files
# actually compared is an equality like FT_FILES and not a count in a line
# nothing reads.
#
# It is written like tools/diff_tokens.sh and tools/diff_ast.sh, including
# their two guards: stage1's own answer is checked before the two are compared,
# and the file list is read line by line so that a path holding a space is one
# path.
#
# Usage: diff_check.sh <build-dir>, from the top of the worktree.
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: diff_check.sh <build-dir>" >&2
    exit 2
fi
build=$1
stage1=$build/fort
stage2=$build/stage2/fort

for binary in "$stage1" "$stage2"; do
    if [ ! -x "$binary" ]; then
        echo "diff_check.sh: not built: $binary" >&2
        exit 2
    fi
done

# The number of .ft files the repository holds, as tools/diff_ast.sh counts
# them: an equality and not a floor, so that a file cannot slip out of the
# comparison. A ticket that adds or removes a .ft file changes all four
# lines in the same commit: diff_tokens.sh, diff_ast.sh, diff_check.sh and
# diff_ir.sh.
FT_FILES=940

# The files of that corpus stage1 checks clean, which are the ones compared.
# It is an equality for the reason FT_FILES is: a comparison that shrank would
# otherwise pass while seeing less, and ctest reads the exit status and not the
# counts printed below. A ticket that adds a clean .ft file, or that makes
# stage1 accept or refuse one, changes this line with FT_FILES.
# It is 22 below diff_ir.sh's PROGRAM_FILES for a reason and not by error, and
# the two sets are not nested: they differ by 106 files one way and 84 the
# other, which is why the gap is not the number of files a ticket adds. That
# script passes -I src -I src/fort -I test/fort/support, so it reaches the 106
# test/fort tests that import a support module, which this one cannot resolve
# and skips; this one compares every file that checks clean, including the 84
# that hold no `main` -- std, src/fort, src/lsp, the fixtures under
# test/fort/support and the imported half of a multi-module test -- which that
# script skips because they compile into no module. Move the two numbers
# together only when both the roots and those two questions agree. A clean file
# that holds no `main` joins the second group and narrows the gap by one, which
# is what T-045's std/sort.ft did.
# The two group sizes are measured and not derived: add `echo "$file"` beside
# the `compared` counter of this script and of diff_ir.sh, sort the two lists,
# and read them off `comm -13` and `comm -23`. T-064 ran that and found every
# one of the 106 importing a support module.
CLEAN_FILES=508

# The search roots every run is given: the standard library the build copied,
# src/fort, so that the compiler's own modules resolve their imports, and src,
# so that a module of the language server, whose module path is `lsp.<name>`,
# resolves as well (T-063). A file that needs none of them is unaffected, since
# a root that holds no module of the path is simply not the one that answers
# (D9.2).
STD_DIR=$build/std
ROOT=src/fort
LSP_ROOT=src

files=$(find . -name '*.ft' -not -path './build/*' -not -path './.git/*' \
    -not -path './.worktrees/*' | sort)
count=$(printf '%s\n' "$files" | grep -c .)
if [ "$count" -ne "$FT_FILES" ]; then
    echo "diff_check.sh: found $count .ft files, expected exactly $FT_FILES" >&2
    echo "diff_check.sh: a ticket that adds or removes one raises or lowers FT_FILES;" >&2
    echo "diff_check.sh: a count far below it means the walk itself is broken" >&2
    exit 1
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

status=0
differing=0
compared=0
skipped=0
while IFS= read -r file; do
    set +e
    "$stage1" --check --std-dir "$STD_DIR" -I "$ROOT" -I "$LSP_ROOT" "$file" \
        >"$work/one.out" 2>"$work/one.err"
    one_status=$?
    set -e
    if [ "$one_status" -ne 0 ]; then
        # stage1 refuses the file: its diagnostics are held against stage2's
        # by the language corpus, test by test, and the ones that come from a
        # function body are expected failures until T-036.
        skipped=$((skipped + 1))
        continue
    fi
    compared=$((compared + 1))
    set +e
    "$stage2" --check --std-dir "$STD_DIR" -I "$ROOT" -I "$LSP_ROOT" "$file" \
        >"$work/two.out" 2>"$work/two.err"
    two_status=$?
    set -e
    if [ "$two_status" -ne 0 ]; then
        echo "diff_check.sh: $file: stage1 checked it clean, stage2 exited $two_status" >&2
        head -n 20 "$work/two.err" >&2
        status=1
        differing=$((differing + 1))
        continue
    fi
    if ! diff -u "$work/one.err" "$work/two.err" >"$work/diags.diff"; then
        echo "diff_check.sh: $file: the diagnostics differ:" >&2
        head -n 40 "$work/diags.diff" >&2
        status=1
        differing=$((differing + 1))
        continue
    fi
    if ! diff -u "$work/one.out" "$work/two.out" >"$work/out.diff"; then
        echo "diff_check.sh: $file: the output differs:" >&2
        head -n 40 "$work/out.diff" >&2
        status=1
        differing=$((differing + 1))
    fi
done <<EOF
$files
EOF

if [ "$compared" -ne "$CLEAN_FILES" ]; then
    echo "diff_check.sh: compared $compared files, expected exactly $CLEAN_FILES" >&2
    echo "diff_check.sh: a ticket that adds a clean .ft file, or that changes which files" >&2
    echo "diff_check.sh: stage1 accepts, raises or lowers CLEAN_FILES beside FT_FILES;" >&2
    echo "diff_check.sh: a count far below it means stage1 refused a corpus it used to pass" >&2
    exit 1
fi
if [ "$status" -eq 0 ]; then
    echo "stage1 and stage2 agree about the $compared .ft files of the repository that"
    echo "check clean; $skipped of $count were refused by stage1 and are the language"
    echo "corpus's to compare"
else
    echo "diff_check.sh: $differing of $compared compared .ft files differ" >&2
fi
exit "$status"
