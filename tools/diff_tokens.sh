#!/bin/bash
# Stage1 and stage2 must lex the common subset into the same tokens.
#
# `fort --tokens <file>` lexes that one file and writes one line per token in
# the form of spec/toolchain.md 1 (D14.1). Both compilers implement it --
# src/bootstrap/lexer.c and src/fort/lexer.ft -- and this script is what holds
# the second against the first while Phase B is written. It compares 964 token
# dumps, diagnostics and exit statuses byte for byte. It separately measures
# three `$cfg` files and four `$if` files that only the source compiler accepts.
# A common-subset file that deliberately fails to lex is compared like any
# other (D14.2).
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
FT_FILES=973

# The source compiler reads `$cfg`; the frozen C bootstrap reports `$`.
CFG_FILES=3

# The source compiler reads `$if`; the frozen C bootstrap reports `$`.
IF_FILES=4

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
cfg_files=0
if_files=0
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
    if grep -Fq "unexpected character '$'" "$work/one.err" &&
        grep -Fq '0 "$if" $if' "$work/two.out"; then
        if_files=$((if_files + 1))
        if_bad=0
        if [ "$one_status" -ne 1 ]; then
            echo "diff_tokens.sh: $file: stage1 exited $one_status, expected 1" >&2
            status=1
            if_bad=1
        fi
        if [ "$two_status" -ne 0 ]; then
            echo "diff_tokens.sh: $file: stage2 exited $two_status, expected 0" >&2
            status=1
            if_bad=1
        fi
        if [ -s "$work/two.err" ]; then
            echo "diff_tokens.sh: $file: stage2 reports a compile-time condition diagnostic" >&2
            head -n 5 "$work/two.err" >&2
            status=1
            if_bad=1
        fi
        for dump in one two; do
            case $(tail -n 1 "$work/$dump.out") in
            *" end of file") ;;
            *)
                echo "diff_tokens.sh: $file: $dump dump has no end-of-file token" >&2
                tail -n 3 "$work/$dump.out" >&2
                status=1
                if_bad=1
                ;;
            esac
        done
        if [ "$if_bad" -ne 0 ]; then
            differing=$((differing + 1))
        fi
        continue
    fi
    if grep -Fq "unexpected character '$'" "$work/one.err" &&
        grep -Fq '0 "$cfg" $cfg' "$work/two.out"; then
        cfg_files=$((cfg_files + 1))
        cfg_bad=0
        if [ "$one_status" -ne 1 ]; then
            echo "diff_tokens.sh: $file: stage1 exited $one_status, expected 1" >&2
            status=1
            cfg_bad=1
        fi
        if [ "$two_status" -ne 0 ]; then
            echo "diff_tokens.sh: $file: stage2 exited $two_status, expected 0" >&2
            status=1
            cfg_bad=1
        fi
        if [ -s "$work/two.err" ]; then
            echo "diff_tokens.sh: $file: stage2 reports a configuration diagnostic" >&2
            head -n 5 "$work/two.err" >&2
            status=1
            cfg_bad=1
        fi
        for dump in one two; do
            case $(tail -n 1 "$work/$dump.out") in
            *" end of file") ;;
            *)
                echo "diff_tokens.sh: $file: $dump dump has no end-of-file token" >&2
                tail -n 3 "$work/$dump.out" >&2
                status=1
                cfg_bad=1
                ;;
            esac
        done
        if [ "$cfg_bad" -ne 0 ]; then
            differing=$((differing + 1))
        fi
        continue
    fi
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

if [ "$cfg_files" -ne "$CFG_FILES" ]; then
    echo "diff_tokens.sh: found configuration syntax in $cfg_files files, expected $CFG_FILES" >&2
    status=1
fi
if [ "$if_files" -ne "$IF_FILES" ]; then
    echo "diff_tokens.sh: found compile-time conditions in $if_files files," >&2
    echo "diff_tokens.sh: expected exactly $IF_FILES" >&2
    status=1
fi

if [ "$status" -eq 0 ]; then
    echo "stage1 and stage2 agree on $((count - cfg_files - if_files)) of $count .ft files;"
    echo "$cfg_files configuration files and $if_files compile-time condition files"
    echo "have the expected divergence and statuses"
else
    echo "diff_tokens.sh: $differing of $count .ft files differ" >&2
fi
exit "$status"
