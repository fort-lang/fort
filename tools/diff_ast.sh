#!/bin/bash
# stage1 and stage2 must parse every .ft file in the repository into the same
# syntax tree.
#
# `fort --ast <file>` lexes and parses that one file and writes its tree as
# one S-expression in the form of notes/toolchain.md 1 (D14.1). Both compilers
# implement it -- src/bootstrap/ast_dump.c and src/fort/ast.ft -- and this
# script is what holds the second against the first while Phase B is written:
# for every .ft file it compares the two trees, the two sets of diagnostics
# and the two exit statuses, byte for byte. A file that deliberately fails to
# parse is compared like any other, since the two compilers must agree about
# its diagnostics and about the error nodes their recovery leaves as well
# (D14.2).
#
# It is the parser's analogue of tools/diff_tokens.sh and is written the same
# way, including the two guards that script learned the hard way: stage1's own
# answer is checked before the two are compared, since two compilers that both
# refuse `--ast` agree about everything, and the file list is read line by line
# so that a path holding a space is one path.
#
# What the comparison does not cover: it sees the tree and the diagnostics, so
# it cannot see a node's range (D20.4), which the dump does not print, nor the
# name ranges, nor anything the checker would say. Ranges are pinned by the
# assertions in test/parser_loc_test.c and test/fort/parser_range_test.ft.
#
# The corpus is every .ft file the repository holds, not only test/lang:
# std/*.ft, src/fort/*.ft, test/fort/*.ft and test/fort_lint/*.ft hold
# constructs the language tests do not. The copies under build/ are the same
# bytes as std/ and are skipped.
#
# Usage: diff_ast.sh <build-dir>, from the top of the worktree.
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: diff_ast.sh <build-dir>" >&2
    exit 2
fi
build=$1
stage1=$build/fort
stage2=$build/stage2/fort

for binary in "$stage1" "$stage2"; do
    if [ ! -x "$binary" ]; then
        echo "diff_ast.sh: not built: $binary" >&2
        exit 2
    fi
done

# The number of .ft files the repository holds. It is an equality and not a
# floor, for the reason tools/diff_tokens.sh gives at its own copy of this
# constant: a file must not be able to slip out of the comparison. A ticket
# that adds or removes a .ft file changes both lines in the same commit.
FT_FILES=643

files=$(find . -name '*.ft' -not -path './build/*' -not -path './.git/*' \
    -not -path './.worktrees/*' | sort)
count=$(printf '%s\n' "$files" | grep -c .)
if [ "$count" -ne "$FT_FILES" ]; then
    echo "diff_ast.sh: found $count .ft files, expected exactly $FT_FILES" >&2
    echo "diff_ast.sh: a ticket that adds or removes one raises or lowers FT_FILES;" >&2
    echo "diff_ast.sh: a count far below it means the walk itself is broken" >&2
    exit 1
fi

# Bytes of context on each side of the first difference between two trees.
WINDOW=60

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

status=0
differing=0
# The list is read line by line rather than word by word: a path holding a
# space would otherwise be split into two paths neither compiler can open,
# and two compilers failing alike is what this script would call agreement.
while IFS= read -r file; do
    set +e
    "$stage1" --ast "$file" >"$work/one.out" 2>"$work/one.err"
    one_status=$?
    "$stage2" --ast "$file" >"$work/two.out" 2>"$work/two.err"
    two_status=$?
    set -e
    # Two dead compilers agree about everything, so stage1's answer is held
    # against what a parser must produce before the two are compared at all: a
    # status of 0 (clean) or 1 (a diagnostic was reported, D14.1) and a tree
    # that is one module node, which every file yields since the parser always
    # covers the whole file (D14.2).
    if [ "$one_status" -gt 1 ]; then
        echo "diff_ast.sh: $file: stage1 exited $one_status, expected 0 or 1" >&2
        cat "$work/one.err" >&2
        status=1
        differing=$((differing + 1))
        continue
    fi
    case $(cat "$work/one.out") in
    "(module"*")") ;;
    *)
        echo "diff_ast.sh: $file: stage1's dump is not a module node" >&2
        head -c 200 "$work/one.out" >&2
        echo >&2
        status=1
        differing=$((differing + 1))
        continue
        ;;
    esac
    if [ "$one_status" -ne "$two_status" ]; then
        echo "diff_ast.sh: $file: stage1 exited $one_status, stage2 $two_status" >&2
        status=1
        differing=$((differing + 1))
        continue
    fi
    # A tree is one long line, so `diff` would print both whole trees and say
    # nothing about where they part. `cmp` gives the first differing byte and
    # the two windows around it are what a reader needs.
    if ! cmp -s "$work/one.out" "$work/two.out"; then
        at=$(cmp "$work/one.out" "$work/two.out" 2>/dev/null | sed 's/.*byte \([0-9]*\),.*/\1/')
        if [ -z "$at" ]; then
            at=1
        fi
        from=$((at > WINDOW ? at - WINDOW : 1))
        to=$((at + WINDOW))
        echo "diff_ast.sh: $file: the syntax trees differ at byte $at:" >&2
        echo "  stage1: $(cut -b "$from-$to" "$work/one.out")" >&2
        echo "  stage2: $(cut -b "$from-$to" "$work/two.out")" >&2
        status=1
        differing=$((differing + 1))
        continue
    fi
    if ! diff -u "$work/one.err" "$work/two.err" >"$work/diags.diff"; then
        echo "diff_ast.sh: $file: the diagnostics differ:" >&2
        head -n 40 "$work/diags.diff" >&2
        status=1
        differing=$((differing + 1))
    fi
done <<EOF
$files
EOF

if [ "$status" -eq 0 ]; then
    echo "stage1 and stage2 agree about the syntax tree, the diagnostics and the exit"
    echo "status of all $count .ft files in the repository"
else
    echo "diff_ast.sh: $differing of $count .ft files differ" >&2
fi
exit "$status"
