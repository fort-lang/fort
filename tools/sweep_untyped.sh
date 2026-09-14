#!/bin/bash
# No program that the checker accepts may die inside LLVM.
#
# This is the sweep of T-127's third criterion. It compiles one program per
# line of a program list, with one compiler, and puts each program in one of
# four classes:
#
#   REPORTED  the checker refused it and named a reason. The author learns
#             something, so this is a good outcome.
#   RAN       the checker accepted it, clang built it and the program printed
#             something. The answer is in the list beside the program, so a
#             wrong answer is a failure here and not a pass.
#   CC-FAIL   the checker accepted it and the build failed. This is the class
#             the criterion forbids: the author learns nothing about their
#             program from a parse error inside LLVM.
#   SILENT    the checker accepted it, the program ran and printed nothing.
#             The author learns nothing either, and T-117 and T-125 each fixed
#             a family of this shape.
#   TRAP      the checker accepted it, clang built it and the program exited
#             non-zero. Checked arithmetic traps at run time (D11.1), so this
#             is a good outcome as well, and the sweep needs it to reach the
#             overflow and bounds paths the emitter writes by width.
#
# T-128 built the first version of this script under build/probe/, which is
# gitignored, and the worktree took it at the merge. This one lives in tools/
# so that the next ticket can run it. It keeps T-128's four classes and its two
# command lines, and it adds the expectation column, which T-128's version did
# not have: a row that changes class, or a RAN row that changes its answer, now
# fails the run instead of moving a summary count that only a reader compares.
#
# Usage, from the top of the worktree, after `tools/vm build <preset>`:
#
#   COMPILER=build/debug/fort bash tools/sweep_untyped.sh tools/sweep_untyped.txt
#   COMPILER=build/debug/stage2/fort \
#       COMPILER_ARGS="--std-dir build/debug/std --cc clang" \
#       bash tools/sweep_untyped.sh tools/sweep_untyped.txt
#
# stage2 needs the two extra arguments because it is not the binary CMake
# configured: it reads no default standard library directory and no default
# clang path from the build (notes/environment.md 5).
#
# The list format is one row per line:
#
#   <stage1 class> <stage2 class> <body>
#
# `<body>` is the statement list that goes into main, and it may hold several
# statements. A class is REPORTED, CC-FAIL, SILENT, RAN:<output> or
# TRAP:<status>, where <output> is what the program prints with every run of
# whitespace replaced by `_` and <status> is the exit status. The two compilers differ on the rows that hold a float literal or a
# `?:`, which stage1 refuses at the lexer, which is why each row carries two
# classes and not one.
#
# A line `!frame <name>` selects the frame every row below it goes into, until
# the next such line. There are three frames and `base` is the first:
#
#   base   fn i32 z(i32 v) { return v; }, and i64, char and u8 twins of it,
#          with main declaring `i32 n`, `i32 thirtyone`, `bool c` and
#          `i32[4] mut a`, each through a call so that it is a run-time value.
#          The body is the statement list of main.
#   float  base plus `fn f64 zf(f64 v)` and `fn f32 zg(f32 v)`, and main
#          declaring `f64 d` as well. stage1 refuses the whole frame, because
#          it supports no float at all, so every row here is REPORTED under
#          stage1 and that is the right answer for a compiler with no floats.
#   ret    base plus `fn i64 r(i32 n)`, whose body is the row. main prints
#          `r(z(1))`. This is the one frame that puts the row in a return
#          position.
#
# Every value the frames declare comes through a call, so every expression
# built from one folds to no value. That is the state the rule of the default
# type answers for (D4.5).
#
# What the sweep covers and what it cannot reach is in the header of
# tools/sweep_untyped.txt.
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: COMPILER=<fort> sweep_untyped.sh <program-list>" >&2
    exit 2
fi
list=$1
compiler=${COMPILER:-}
compiler_args=${COMPILER_ARGS:-}

if [ -z "$compiler" ]; then
    echo "sweep_untyped.sh: set COMPILER to the compiler to sweep with" >&2
    exit 2
fi
if [ ! -x "$compiler" ]; then
    echo "sweep_untyped.sh: not built: $compiler" >&2
    exit 2
fi
if [ ! -f "$list" ]; then
    echo "sweep_untyped.sh: no such program list: $list" >&2
    exit 2
fi

# Which column of the list this run reads. stage2 is the binary under
# <build-dir>/stage2/, and every other path is stage1.
case $compiler in
    *stage2*) stage=2 ;;
    *) stage=1 ;;
esac

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

reported=0
ran=0
ccfail=0
silent=0
trapped=0
rows=0
wrong=0

# The output of a program, with every run of whitespace replaced by `_`, so
# that a class and its expected output are one field of the row.
squash() {
    tr '\n\t ' '___' <"$1" | sed 's/__*/_/g; s/^_//; s/_$//'
}

frame=base
while IFS= read -r line; do
    case $line in
        ''|'#'*) continue ;;
        '!frame '*)
            frame=${line#'!frame '}
            case $frame in
                base|float|ret) ;;
                *) echo "sweep_untyped.sh: no such frame: $frame" >&2; exit 2 ;;
            esac
            continue
            ;;
    esac
    expect1=${line%% *}
    rest=${line#* }
    expect2=${rest%% *}
    body=${rest#* }
    if [ "$stage" -eq 1 ]; then
        expect=$expect1
    else
        expect=$expect2
    fi
    rows=$((rows + 1))

    prog=$work/row.ft
    {
        echo 'fn i32 z(i32 v) {'
        echo '    return v;'
        echo '}'
        echo ''
        echo 'fn i64 zw(i64 v) {'
        echo '    return v;'
        echo '}'
        echo ''
        echo 'fn char zc(char v) {'
        echo '    return v;'
        echo '}'
        echo ''
        echo 'fn u8 zb(u8 v) {'
        echo '    return v;'
        echo '}'
        echo ''
        if [ "$frame" = float ]; then
            echo 'fn f64 zf(f64 v) {'
            echo '    return v;'
            echo '}'
            echo ''
            echo 'fn f32 zg(f32 v) {'
            echo '    return v;'
            echo '}'
            echo ''
        fi
        if [ "$frame" = ret ]; then
            echo 'fn i64 r(i32 n) {'
            echo "    $body"
            echo '}'
            echo ''
        fi
        echo 'fn i32 main() {'
        echo '    i32 n = z(1);'
        echo '    i32 thirtyone = z(31);'
        echo '    bool c = z(1) > 0;'
        echo '    i32[4] mut a = {};'
        if [ "$frame" = float ]; then
            echo '    f64 d = zf(0.5);'
        fi
        if [ "$frame" = ret ]; then
            echo '    println(r(z(1)));'
        else
            echo "    $body"
        fi
        echo '    return 0;'
        echo '}'
    } >"$prog"

    set +e
    # shellcheck disable=SC2086
    "$compiler" $compiler_args --check "$prog" >"$work/check.out" 2>"$work/check.err"
    check_status=$?
    set -e
    if [ "$check_status" -ne 0 ]; then
        actual=REPORTED
        detail=$(head -n 1 "$work/check.err")
        reported=$((reported + 1))
    else
        rm -f "$work/row.bin"
        set +e
        # shellcheck disable=SC2086
        "$compiler" $compiler_args -o "$work/row.bin" "$prog" \
            >"$work/cc.out" 2>"$work/cc.err"
        cc_status=$?
        set -e
        if [ "$cc_status" -ne 0 ] || [ ! -x "$work/row.bin" ]; then
            actual=CC-FAIL
            detail=$(head -n 1 "$work/cc.err")
            ccfail=$((ccfail + 1))
        else
            set +e
            "$work/row.bin" >"$work/run.out" 2>"$work/run.err"
            run_status=$?
            set -e
            printed=$(squash "$work/run.out")
            if [ "$run_status" -ne 0 ]; then
                actual=TRAP:$run_status
                detail=$(head -n 1 "$work/run.err")
                trapped=$((trapped + 1))
            elif [ -z "$printed" ]; then
                actual=SILENT
                detail=$(head -n 1 "$work/run.err")
                silent=$((silent + 1))
            else
                actual=RAN:$printed
                detail=$printed
                ran=$((ran + 1))
            fi
        fi
    fi

    if [ "$actual" = "$expect" ]; then
        printf '%-28s %s\n' "$actual" "$body"
    else
        wrong=$((wrong + 1))
        printf '%-28s %s\n' "$actual" "$body"
        printf '    expected %s, got %s\n' "$expect" "$actual" >&2
        printf '    %s\n' "$detail" >&2
        printf '    row: %s\n' "$body" >&2
    fi
done <"$list"

echo "$rows rows under stage$stage: $reported reported, $ran ran," \
     "$trapped trapped, $ccfail refused by clang, $silent silent"

status=0
if [ "$ccfail" -ne 0 ]; then
    echo "sweep_untyped.sh: $ccfail programs passed the checker and died in LLVM" >&2
    status=1
fi
if [ "$silent" -ne 0 ]; then
    echo "sweep_untyped.sh: $silent programs passed the checker and printed nothing" >&2
    status=1
fi
if [ "$wrong" -ne 0 ]; then
    echo "sweep_untyped.sh: $wrong of $rows rows did not match their expected class" >&2
    status=1
fi
exit "$status"
