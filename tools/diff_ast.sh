#!/bin/bash
# stage1 and stage2 must parse every .ft file in the repository into the same
# syntax tree.
#
# `fort --ast <file>` lexes and parses that one file and writes its tree as
# one S-expression in the form of spec/toolchain.md 1 (D14.1). Both compilers
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
# The one construct the two parsers read differently is skipped here, and it
# is skipped by name rather than by silence (T-043): src/fort reads the nested
# array and span levels of D3.6 (`i32[3][4]`, `i32[4]@`, `u8@@`, `node@[4]`)
# and src/bootstrap refuses them, so stage1 answers such a file with `not
# supported by the bootstrap compiler: <one of four features>` and no tree the
# comparison could use. A file whose stage1 diagnostics name one of those four
# features is therefore counted and skipped, and nothing else is: the count is
# an equality like FT_FILES, so a file cannot leave the comparison quietly. The
# skip asks stage2's side too -- that it took the file and printed no such
# message of its own -- and it stands after the two guards below, so that a
# stage1 which exits 2 while printing the message is reported and not counted.
#
# `do`-`while` and `?:` are the second such construct and are skipped the same
# way (D6.6, D7.5, T-044), with a count of their own and one guard more:
# stage2 must not report the refusal stage1 reports.
#
# A float literal is the third (D2.6, T-041), skipped the same way again. The
# three counts are order-dependent and the order is the one the loop runs in:
# a file holding a `?:` and a float literal is counted once, by the first test
# that matches it. Each count is an equality, so no file leaves the comparison
# quietly whichever reason takes it.
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
# that adds or removes a .ft file changes all four lines in the same commit:
# diff_tokens.sh, diff_ast.sh, diff_check.sh and diff_ir.sh.
FT_FILES=912

# The files of that corpus src/bootstrap refuses for a nested array or span
# level, which src/fort reads (D3.6, T-043). They are skipped below, and this
# is an equality for the reason FT_FILES is: a comparison that shrank would
# otherwise pass while seeing less. A ticket that adds or removes a test using
# a nested level raises or lowers it beside FT_FILES.
NESTED_FILES=24

# The four diagnostics src/bootstrap reports for such a type
# (src/bootstrap/parser.c, check_one_aggregate_level).
NESTED_MESSAGES='multi-dimensional arrays|spans of arrays|arrays of spans|spans of spans'

# The same for the second construct the two parsers read differently:
# src/fort implements `do`-`while` and `?:` (D6.6, D7.5, T-044) and
# src/bootstrap refuses both, so stage1 answers such a file with the refusal
# and no tree to compare. These files are not skipped in silence either: the
# count is an equality, and stage2 must not report the refusal itself, which
# is what says the divergence is the one intended.
FORM_FILES=13
FORM_MESSAGES='do-while|\?:'

# The same for the third: src/fort reads a float literal (D2.6, T-041) and
# src/bootstrap refuses one, so stage1 answers such a file with the refusal
# and no tree to compare. A file that also holds one of the constructs above
# is counted by the first test that matches it, not by this one.
FLOAT_FILES=68
FLOAT_MESSAGES='float literals'

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
nested=0
forms=0
floats=0
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
    # stage1 refuses `do`-`while` and `?:` and stage2 reads both, so there is
    # nothing to compare. The file is counted, and stage2 is held to the other
    # half of the divergence: it must not report that refusal. Its status and
    # the rest of its output are not compared, since such a file may hold an
    # error of another kind as well -- test/highlight/scopes.ft holds a
    # lexical one and run/constants/006_folding_float_ternary.ft a float
    # literal, which both compilers refuse.
    if grep -Eq "not supported by the bootstrap compiler: ($FORM_MESSAGES)" \
        "$work/one.err"; then
        forms=$((forms + 1))
        if grep -Eq "not supported by the bootstrap compiler: ($FORM_MESSAGES)" \
            "$work/two.err"; then
            echo "diff_ast.sh: $file: stage2 must accept the construct stage1 refuses" >&2
            head -n 5 "$work/two.err" >&2
            status=1
            differing=$((differing + 1))
            continue
        fi
        # There is no tree to compare, so stage2's own answer is held against
        # what a parser must produce, exactly as stage1's is below: a status
        # of 0 or 1 and a dump that is one module node. Without this a stage2
        # that crashed or wrote nothing on a `?:` would pass, which is the
        # "two compilers that answer nothing agree about everything" trap this
        # script is written against.
        if [ "$two_status" -gt 1 ]; then
            echo "diff_ast.sh: $file: stage2 exited $two_status, expected 0 or 1" >&2
            cat "$work/two.err" >&2
            status=1
            differing=$((differing + 1))
            continue
        fi
        case $(cat "$work/two.out") in
        "(module"*")") ;;
        *)
            echo "diff_ast.sh: $file: stage2's dump is not a module node" >&2
            head -c 200 "$work/two.out" >&2
            echo >&2
            status=1
            differing=$((differing + 1))
            ;;
        esac
        continue
    fi
    # Two dead compilers agree about everything, so stage1's answer is held
    # against what a parser must produce before the two are compared at all: a
    # status of 0 (clean) or 1 (a diagnostic was reported, D14.1) and a tree
    # that is one module node, which every file yields since the parser always
    # covers the whole file (D14.2).
    # A float literal is the third such construct: stage1 refuses one and
    # stage2 reads it (D2.6, T-041), so there is no tree to compare. The file
    # is counted and stage2 is held to the other half of the divergence, as
    # the two tests above do.
    if grep -Eq "not supported by the bootstrap compiler: ($FLOAT_MESSAGES)" \
        "$work/one.err"; then
        floats=$((floats + 1))
        if grep -Eq "not supported by the bootstrap compiler: ($FLOAT_MESSAGES)" \
            "$work/two.err"; then
            echo "diff_ast.sh: $file: stage2 must accept the construct stage1 refuses" >&2
            head -n 5 "$work/two.err" >&2
            status=1
            differing=$((differing + 1))
            continue
        fi
        if [ "$two_status" -gt 1 ]; then
            echo "diff_ast.sh: $file: stage2 exited $two_status, expected 0 or 1" >&2
            cat "$work/two.err" >&2
            status=1
            differing=$((differing + 1))
            continue
        fi
        case $(cat "$work/two.out") in
        "(module"*")") ;;
        *)
            echo "diff_ast.sh: $file: stage2's dump is not a module node" >&2
            head -c 200 "$work/two.out" >&2
            echo >&2
            status=1
            differing=$((differing + 1))
            ;;
        esac
        continue
    fi
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
    # stage1 refuses a nested array or span level and stage2 reads it, so
    # there is nothing to compare: count the file and go on. It stands after
    # the two guards above, so a stage1 that exited 2 while printing the
    # message is still reported rather than counted, and it asks stage2's side
    # as well: stage2 must have taken the file (exit 0, or 1 for a diagnostic
    # of its own) and must not have printed the message itself, since a stage2
    # that began refusing these forms again would otherwise let this script
    # print agreement while the language corpus catches it alone.
    if grep -Eq "not supported by the bootstrap compiler: ($NESTED_MESSAGES)" \
        "$work/one.err"; then
        if [ "$two_status" -gt 1 ]; then
            echo "diff_ast.sh: $file: stage1 refuses a nested level, stage2 exited" \
                "$two_status" >&2
            cat "$work/two.err" >&2
            status=1
            differing=$((differing + 1))
            continue
        fi
        if grep -Eq "not supported by the bootstrap compiler: ($NESTED_MESSAGES)" \
            "$work/two.err"; then
            echo "diff_ast.sh: $file: stage2 refuses a nested level too; it reads every" >&2
            echo "diff_ast.sh: form of D3.6 since T-043, so this is a regression" >&2
            cat "$work/two.err" >&2
            status=1
            differing=$((differing + 1))
            continue
        fi
        nested=$((nested + 1))
        continue
    fi
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

if [ "$nested" -ne "$NESTED_FILES" ]; then
    echo "diff_ast.sh: skipped $nested files for a nested array or span level," >&2
    echo "diff_ast.sh: expected exactly $NESTED_FILES; a ticket that adds or removes a test" >&2
    echo "diff_ast.sh: using one raises or lowers NESTED_FILES beside FT_FILES" >&2
    exit 1
fi
if [ "$forms" -ne "$FORM_FILES" ]; then
    echo "diff_ast.sh: skipped $forms files for a do-while or a '?:'," >&2
    echo "diff_ast.sh: expected exactly $FORM_FILES; a ticket that adds or removes a test" >&2
    echo "diff_ast.sh: using one raises or lowers FORM_FILES beside FT_FILES" >&2
    exit 1
fi
if [ "$floats" -ne "$FLOAT_FILES" ]; then
    echo "diff_ast.sh: skipped $floats files for a float literal," >&2
    echo "diff_ast.sh: expected exactly $FLOAT_FILES; a ticket that adds or removes a test" >&2
    echo "diff_ast.sh: holding one raises or lowers FLOAT_FILES beside FT_FILES" >&2
    exit 1
fi
if [ "$status" -eq 0 ]; then
    echo "stage1 and stage2 agree about the syntax tree, the diagnostics and the exit"
    echo "status of $((count - nested - forms - floats)) of the $count .ft files in the"
    echo "repository; $nested use a nested array or span level, $forms a do-while or a"
    echo "'?:' and $floats a float literal, which stage1 alone refuses"
else
    echo "diff_ast.sh: $differing of $count .ft files differ" >&2
fi
exit "$status"
