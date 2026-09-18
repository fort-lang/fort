#!/bin/bash
# Check untyped expression programs against their expected result classes.
#
# Compile one program for each list row. Each row names one of six classes:
#
#   REPORTED  the checker exited 1 and reported the row or a later line.
#   FRAME     the checker refused before it reached the row: it exited 1 and
#             its first diagnostic points at a line above the row's own. The
#             row itself is then unmeasured, and the class says so.
#   RAN       the checker accepted it, clang built it and the program printed
#             something. The answer is in the list beside the program, so a
#             wrong answer is a failure here and not a pass.
#   CC-FAIL   the checker accepted it, but the target build failed.
#   SILENT    the checker accepted it, and the program printed nothing.
#   TRAP      the checker accepted it, clang built it and the program exited
#             non-zero. Checked arithmetic traps at run time, so this is valid.
#             The sweep needs this class to reach overflow and bounds paths.
#
# Two more classes name a broken compiler and not a program. No row may expect
# either, and each one fails the run:
#
#   BAD-STATUS:<n>  the checker exited with a status other than 0 or 1.
#   NO-DIAGNOSTIC   the checker exited 1 and wrote no `error:` line. A refusal
#                   that names nothing teaches the author nothing.
#
# A changed class or RAN answer fails the run.
#
# Usage from the worktree root after `tools/vm build <preset>`.
#
#   COMPILER=build/debug/fort \
#       COMPILER_ARGS="--std-dir build/debug/std --cc clang" \
#       bash tools/sweep_untyped.sh tools/sweep_untyped.txt
# This command runs the sweep.
#
# The compiler needs the explicit standard root because the root is beside it.
# Its executable-relative fallback selects `build/std` instead.
#
# The list format is one row per line:
#
#   <class> <body>
#
# `<body>` is the statement list that goes into main, and it may hold several
# statements. A class is REPORTED, FRAME, CC-FAIL, SILENT, RAN:<output>, or
# TRAP:<status>. In <output>, `_` replaces each whitespace run. <status> is the
# exit status.
#
# A row without a body is a usage error. A class the list misspells is also a
# usage error.
#
# The list states its own length in a directive, `!rows <n>`, and the script
# holds it as an equality. Without it a deleted row passes in silence: every
# row that is left still matches, and only a reader compares the summary.
#
# A line `!frame <name>` selects the frame every row below it goes into, until
# the next such line. There are three frames and `base` is the first:
#
#   base   `fn z(i32 v) i32 { return v; }`, with i64, char, and u8 variants.
#          main declares `i32 n`, `i32 thirtyone`, `bool c`, and
#          `i32[4] mut a`. Calls give each declaration a run-time value.
#          The body is the statement list of main.
#   float  base plus `fn zf(f64 v) f64` and `fn zg(f32 v) f32`, and main
#          declaring `f64 d = zf(0.5);` as well.
#   ret    base plus `fn r(i32 n) i64`, whose body is the row. main prints
#          `r(z(1))`. This is the one frame that puts the row in a return
#          position.
#
# Every frame value comes through a call, so expressions cannot fold its value.
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

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

reported=0
framed=0
ran=0
ccfail=0
silent=0
trapped=0
badstatus=0
nodiag=0
rows=0
wrong=0
# The length the list declares in its `!rows` directive. -1 until it is read,
# so a list without the directive is a usage error and not a silent pass.
want_rows=-1

# A class a row may expect. The list rejects typos and rows
# that contain only a class. Such rows would test nothing.
valid_class() {
    case $1 in
        REPORTED|FRAME|CC-FAIL|SILENT) return 0 ;;
        RAN:?*|TRAP:?*) return 0 ;;
        *) return 1 ;;
    esac
}

# The output replaces each whitespace run with `_`. This keeps a class and its
# expected output in one row field.
squash() {
    tr '\n\t ' '___' <"$1" | sed 's/__*/_/g; s/^_//; s/_$//'
}

frame=base
while IFS= read -r line; do
    case $line in
        ''|'#'*) continue ;;
        '!rows '*)
            want_rows=${line#'!rows '}
            case $want_rows in
                ''|*[!0-9]*)
                    echo "sweep_untyped.sh: !rows needs a number: $line" >&2
                    exit 2
                    ;;
            esac
            continue
            ;;
        '!frame '*)
            frame=${line#'!frame '}
            case $frame in
                base|float|ret) ;;
                *) echo "sweep_untyped.sh: no such frame: $frame" >&2; exit 2 ;;
            esac
            continue
            ;;
        '!'*)
            echo "sweep_untyped.sh: no such directive: $line" >&2
            exit 2
            ;;
    esac
    # Two fields. A row without a body tests nothing.
    case $line in
        *' '*) ;;
        *)
            echo "sweep_untyped.sh: a row needs two fields: $line" >&2
            exit 2
            ;;
    esac
    expect=${line%% *}
    body=${line#* }
    case $body in
        *[![:space:]]*) ;;
        *) echo "sweep_untyped.sh: a row body must not be empty: $line" >&2; exit 2 ;;
    esac
    if ! valid_class "$expect"; then
        echo "sweep_untyped.sh: no such class: $expect" >&2
        echo "sweep_untyped.sh: row: $line" >&2
        exit 2
    fi
    rows=$((rows + 1))

    prog=$work/row.ft
    {
        echo 'fn z(i32 v) i32 {'
        echo '    return v;'
        echo '}'
        echo ''
        echo 'fn zw(i64 v) i64 {'
        echo '    return v;'
        echo '}'
        echo ''
        echo 'fn zc(char v) char {'
        echo '    return v;'
        echo '}'
        echo ''
        echo 'fn zb(u8 v) u8 {'
        echo '    return v;'
        echo '}'
        echo ''
        if [ "$frame" = float ]; then
            echo 'fn zf(f64 v) f64 {'
            echo '    return v;'
            echo '}'
            echo ''
            echo 'fn zg(f32 v) f32 {'
            echo '    return v;'
            echo '}'
            echo ''
        fi
        if [ "$frame" = ret ]; then
            echo 'fn r(i32 n) i64 {'
            echo "    $body"
            echo '}'
            echo ''
        fi
        echo 'fn main() i32 {'
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

    # The line the row itself stands on in the generated program. A refusal
    # above it did not reach the row, which is the FRAME class. The body is
    # written with `echo "    $body"`, so an exact whole-line match finds it.
    # And the `ret` frame writes it inside `r()` before main calls it, so the
    # first match is the row.
    body_line=$(grep -n -F -x -m 1 "    $body" "$prog" | cut -d: -f1)
    if [ -z "$body_line" ]; then
        echo "sweep_untyped.sh: the row is not in the program it generated" >&2
        echo "sweep_untyped.sh: row: $body" >&2
        exit 2
    fi

    set +e
    # shellcheck disable=SC2086
    "$compiler" $compiler_args --check "$prog" >"$work/check.out" 2>"$work/check.err"
    check_status=$?
    set -e
    if [ "$check_status" -gt 1 ]; then
        # The compiler uses result statuses 0 and 1. Anything else is a
        # crash or a usage error and not a diagnostic. And reading it as one
        # would let a compiler that dies on every row pass the sweep.
        actual=BAD-STATUS:$check_status
        detail=$(head -n 1 "$work/check.err")
        badstatus=$((badstatus + 1))
    elif [ "$check_status" -eq 1 ]; then
        first=$(grep -m 1 -E '^[^ ]+:[0-9]+:[0-9]+: error: ' "$work/check.err" || true)
        if [ -z "$first" ]; then
            actual=NO-DIAGNOSTIC
            detail=$(head -n 1 "$work/check.err")
            nodiag=$((nodiag + 1))
        else
            detail=$first
            diag_line=$(printf '%s\n' "$first" | sed -E 's/^[^:]*:([0-9]+):.*/\1/')
            if [ "$diag_line" -lt "$body_line" ]; then
                actual=FRAME
                framed=$((framed + 1))
            else
                actual=REPORTED
                reported=$((reported + 1))
            fi
        fi
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

echo "$rows rows: $reported reported, $framed refused at the" \
     "frame, $ran ran, $trapped trapped, $ccfail refused by clang," \
     "$silent silent, $badstatus with a bad status, $nodiag with no diagnostic"

status=0
if [ "$want_rows" -lt 0 ]; then
    echo "sweep_untyped.sh: the list declares no !rows directive" >&2
    status=1
elif [ "$rows" -ne "$want_rows" ]; then
    echo "sweep_untyped.sh: read $rows rows, the list declares $want_rows" >&2
    echo "sweep_untyped.sh: a ticket that adds or removes a row moves that number" >&2
    status=1
fi
if [ "$badstatus" -ne 0 ]; then
    echo "sweep_untyped.sh: $badstatus rows left the checker with a status D14.1 does not give it" >&2
    status=1
fi
if [ "$nodiag" -ne 0 ]; then
    echo "sweep_untyped.sh: $nodiag rows were refused with no diagnostic" >&2
    status=1
fi
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
