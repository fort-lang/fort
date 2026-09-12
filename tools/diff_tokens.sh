#!/bin/bash
# stage1 and stage2 must lex every .ft file in the repository into the same
# tokens.
#
# `fort --tokens <file>` lexes that one file and writes one line per token in
# the form of notes/toolchain.md 1 (D14.1). Both compilers implement it --
# src/bootstrap/lexer.c and src/fort/lexer.ft -- and this script is what holds
# the second against the first while Phase B is written: for every .ft file it
# compares the two dumps, the two sets of diagnostics and the two exit
# statuses, byte for byte. A file that deliberately fails to lex is compared
# like any other, since the two compilers must agree about its diagnostics as
# well (D14.2).
#
# The corpus is every .ft file the repository holds, not only test/lang:
# std/*.ft, src/fort/*.ft, test/fort/*.ft and test/fort_lint/*.ft hold
# constructs the language tests do not. The copies under build/ are the same
# bytes as std/ and are skipped.
#
# Usage: diff_tokens.sh <build-dir>, from the top of the worktree.
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: diff_tokens.sh <build-dir>" >&2
    exit 2
fi
build=$1
stage1=$build/fort
stage2=$build/stage2/fort

for binary in "$stage1" "$stage2"; do
    if [ ! -x "$binary" ]; then
        echo "diff_tokens.sh: not built: $binary" >&2
        exit 2
    fi
done

# The number of .ft files the repository holds. It is an equality and not a
# floor: the point of the constant is that a file cannot slip out of the
# comparison, and a floor only notices the walk losing all of them. A ticket
# that adds or removes a .ft file changes all four lines in the same commit:
# diff_tokens.sh, diff_ast.sh, diff_check.sh and diff_ir.sh.
FT_FILES=772

files=$(find . -name '*.ft' -not -path './build/*' -not -path './.git/*' \
    -not -path './.worktrees/*' | sort)
count=$(printf '%s\n' "$files" | grep -c .)
if [ "$count" -ne "$FT_FILES" ]; then
    echo "diff_tokens.sh: found $count .ft files, expected exactly $FT_FILES" >&2
    echo "diff_tokens.sh: a ticket that adds or removes one raises or lowers FT_FILES;" >&2
    echo "diff_tokens.sh: a count far below it means the walk itself is broken" >&2
    exit 1
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

status=0
differing=0
# The list is read line by line rather than word by word: a path holding a
# space would otherwise be split into two paths neither compiler can open,
# and two compilers failing alike is what this script would call agreement.
while IFS= read -r file; do
    set +e
    "$stage1" --tokens "$file" >"$work/one.out" 2>"$work/one.err"
    one_status=$?
    "$stage2" --tokens "$file" >"$work/two.out" 2>"$work/two.err"
    two_status=$?
    set -e
    # Two dead compilers agree about everything, so stage1's answer is held
    # against what a lexer must produce before the two are compared at all: a
    # status of 0 (clean) or 1 (a lexical error was reported, D14.1) and a
    # dump whose last line is the end-of-file token, which every dump ends in
    # (D14.2). Without this the script passed over the whole corpus with both
    # binaries replaced by a stub printing `unknown option '--tokens'`.
    if [ "$one_status" -gt 1 ]; then
        echo "diff_tokens.sh: $file: stage1 exited $one_status, expected 0 or 1" >&2
        cat "$work/one.err" >&2
        status=1
        differing=$((differing + 1))
        continue
    fi
    case $(tail -n 1 "$work/one.out") in
    *" end of file") ;;
    *)
        echo "diff_tokens.sh: $file: stage1's dump does not end in the end-of-file token" >&2
        tail -n 3 "$work/one.out" >&2
        status=1
        differing=$((differing + 1))
        continue
        ;;
    esac
    if [ "$one_status" -ne "$two_status" ]; then
        echo "diff_tokens.sh: $file: stage1 exited $one_status, stage2 $two_status" >&2
        status=1
        differing=$((differing + 1))
        continue
    fi
    if ! diff -u "$work/one.out" "$work/two.out" >"$work/tokens.diff"; then
        echo "diff_tokens.sh: $file: the token dumps differ:" >&2
        head -n 40 "$work/tokens.diff" >&2
        status=1
        differing=$((differing + 1))
        continue
    fi
    if ! diff -u "$work/one.err" "$work/two.err" >"$work/diags.diff"; then
        echo "diff_tokens.sh: $file: the diagnostics differ:" >&2
        head -n 40 "$work/diags.diff" >&2
        status=1
        differing=$((differing + 1))
    fi
done <<EOF
$files
EOF

if [ "$status" -eq 0 ]; then
    echo "stage1 and stage2 agree about the tokens, the diagnostics and the exit"
    echo "status of all $count .ft files in the repository"
else
    echo "diff_tokens.sh: $differing of $count .ft files differ" >&2
fi
exit "$status"
