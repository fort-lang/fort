#!/bin/bash
# tools/ir_snapshot.sh [--fir-stats] <fort> <std-dir> <out-dir>: write the emitted IR of the run
# corpus.
# <std-dir> is the staged standard library of the build, build/<Host>/debug/std, and not std/.
#
# The script reads the test list from `test/lang/run_tests.py --list run/`.
# For each test it writes the `-S` output of <fort> in three modes:
#   <out-dir>/default/<name>.ll    the flags of the test only
#   <out-dir>/release/<name>.ll    `--release` and the flags of the test
#   <out-dir>/nobounds/<name>.ll   `--no-bounds-check` and the flags of the test
# <name> is the test path without `.ft`, with each `/` changed to `_`.
# Two snapshots compare with `diff -r` (D19.5). A ticket that must not change the emitted IR
# uses them as its identity check. The FIR migration compares two snapshots with `llvm-diff`
# (notes/testing.md 6).
#
# With --fir-stats, each `-S` run also gets `--fir-stats` (D14.1), and the script writes the line
# `<mode> <test path> lowered N of M` of each module to <out-dir>/stats.txt: N functions that the
# FIR path wrote, of M definitions (spec/fir.md 16.1). It prints the sums of each mode at the end.
# A compiler before T-253 has no --fir-stats and refuses every test in this mode.
#
# The emitter reads two build options (`gen_options`, src/fort/gen.ft).
# `--release` controls the overflow checks and the overwrite checks.
# `--no-bounds-check` controls the index and span checks, and `--release` keeps those checks
# (D10.6). The two options change different text, so the script writes three modes.
#
# The script compiles each test the way run_tests.py does. It runs in test/lang and gives the
# entry as a path relative to test/lang. A directory test has the entry `<path>/main.ft`.
# The test's `//! flags:` directive comes after the mode option.
# The compiler writes the path of each standard module into the module text, and that path
# starts with <std-dir>. So two snapshots that you compare must use the same <std-dir> path.
#
# The script skips these:
#   - the tests outside run/, which includes programs/ and all fail tests;
#   - a test that the compiler refuses in a mode. The script writes no .ll file for it in that
#     mode, and it writes the line `<mode> <test path> exit <status>` to <out-dir>/skipped.txt.
# The script does not link or run a program, and it does not verify a module.
#
# Exit status: 0 when every test has a .ll file or a line in skipped.txt, 1 when the compiler
# exits 0 and writes no module, 2 for a usage error, a non-empty <out-dir> or two tests that
# give one file name.
set -eu
# The flags of a test split into words below. No flag is a file name, so no word expands.
set -f

usage() {
    echo "usage: ir_snapshot.sh [--fir-stats] <fort> <std-dir> <out-dir>" >&2
}

stats=
if [ "$#" -eq 4 ] && [ "$1" = --fir-stats ]; then
    stats=--fir-stats
    shift
fi
if [ "$#" -ne 3 ]; then
    usage
    exit 2
fi

# absolute <path>: print the absolute form of a path that exists.
absolute() {
    if [ -d "$1" ]; then
        (cd "$1" && pwd)
    else
        echo "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
    fi
}

if [ ! -f "$1" ] || [ ! -x "$1" ]; then
    echo "ir_snapshot.sh: compiler not found: $1" >&2
    exit 2
fi
if [ ! -d "$2" ]; then
    echo "ir_snapshot.sh: standard directory not found: $2" >&2
    exit 2
fi
fort=$(absolute "$1")
std=$(absolute "$2")
out=$3
# An old snapshot in <out-dir> would give old files to `diff -r`, so <out-dir> must be empty.
if [ -e "$out" ] && [ -n "$(ls -A "$out")" ]; then
    echo "ir_snapshot.sh: $out is not empty" >&2
    exit 2
fi
mkdir -p "$out"
out=$(absolute "$out")

root=$(cd "$(dirname "$0")/.." && pwd)
lang=$root/test/lang
list=$out/tests.txt
# The last line of the list is the count `<n> tests`. Only the test paths go into tests.txt.
python3 "$lang/run_tests.py" --list run/ >"$out/list.txt"
grep -v ' tests$' "$out/list.txt" >"$list" || true
rm -f "$out/list.txt"

# flags_of <entry>: print the text of the `//! flags:` directive of the header of <entry>.
# The header is the first run of `//!`, `//<` and `//|` lines (run_tests.py `parse_main`).
flags_of() {
    awk '
        substr($0, 1, 3) != "//!" && substr($0, 1, 3) != "//<" && substr($0, 1, 3) != "//|" {
            exit
        }
        substr($0, 1, 10) == "//! flags:" { print substr($0, 11) }
    ' "$1"
}

status=0
: >"$out/skipped.txt"
if [ -n "$stats" ]; then
    : >"$out/stats.txt"
fi
for mode in default release nobounds; do
    mkdir -p "$out/$mode"
done

cd "$lang"
while IFS= read -r path; do
    case "$path" in
        *.ft) entry=$path ;;
        *) entry=$path/main.ft ;;
    esac
    name=$(echo "${path%.ft}" | tr / _)
    flags=$(flags_of "$entry")
    for mode in default release nobounds; do
        case "$mode" in
            default) option= ;;
            release) option=--release ;;
            nobounds) option=--no-bounds-check ;;
        esac
        module=$out/$mode/$name.ll
        # Two test paths must not give one file name.
        if [ -e "$module" ]; then
            echo "ir_snapshot.sh: $path: $module exists already" >&2
            exit 2
        fi
        # $option is one option or empty. $flags is the directive text, which run_tests.py
        # splits at white space. So the split of each one into words is what is wanted here.
        # $stats is `--fir-stats` or empty, one word or none.
        # shellcheck disable=SC2086
        if "$fort" --std-dir "$std" $stats $option $flags -S -o "$module" "$entry" \
            2>"$out/stderr.txt" >"$out/stdout.txt"; then
            if [ ! -s "$module" ]; then
                echo "ir_snapshot.sh: $mode: $path: the compiler exited 0 and wrote no module" >&2
                status=1
            fi
            if [ -n "$stats" ]; then
                echo "$mode $path $(cat "$out/stdout.txt")" >>"$out/stats.txt"
            fi
        else
            code=$?
            rm -f "$module"
            echo "$mode $path exit $code" >>"$out/skipped.txt"
            echo "ir_snapshot.sh: $mode: $path: the compiler exited $code" >&2
            head -n 5 "$out/stderr.txt" >&2
        fi
    done
done <"$list"
rm -f "$out/stderr.txt" "$out/stdout.txt"

tests=$(wc -l <"$list" | tr -d ' ')
echo "ir_snapshot.sh: $tests tests"
for mode in default release nobounds; do
    count=$(find "$out/$mode" -name '*.ll' | wc -l | tr -d ' ')
    echo "ir_snapshot.sh: $mode: $count modules"
done
skipped=$(wc -l <"$out/skipped.txt" | tr -d ' ')
echo "ir_snapshot.sh: $skipped skipped (skipped.txt)"
if [ -n "$stats" ]; then
    # Each line of stats.txt is `<mode> <path> lowered <n> of <m>`.
    for mode in default release nobounds; do
        awk -v mode="$mode" '
            $1 == mode && $3 == "lowered" { n += $4; m += $6 }
            END { printf "ir_snapshot.sh: %s: lowered %d of %d (stats.txt)\n", mode, n, m }
        ' "$out/stats.txt"
    done
fi
exit "$status"
