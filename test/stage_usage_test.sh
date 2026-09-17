#!/bin/bash
# Stage1 and stage2 share their version, usage, and common help lines. Stage2
# adds the source-only --cfg line.
#
# Both binaries exist at build time, so this test runs and compares them.
# Stage2 is an x86-64 binary and runs under qemu.
#
# Usage: stage_usage_test.sh <build-dir>
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: stage_usage_test.sh <build-dir>" >&2
    exit 2
fi
build=$1
stage1=$build/bootstrap/stage1/fort
stage2=$build/fort

for binary in "$stage1" "$stage2"; do
    if [ ! -x "$binary" ]; then
        echo "stage_usage_test.sh: not built: $binary" >&2
        exit 2
    fi
done

status=0

# Each answer is captured rather than piped into diff, so that the exit status
# is checked too and two binaries printing nothing cannot compare equal: a
# stage2 with the right bytes and the wrong status would otherwise pass.
answer=""
answer_status=0
capture() {
    answer=$("$1" "$2" 2>/dev/null) && answer_status=0 || answer_status=$?
}

for option in --help --version; do
    capture "$stage1" "$option"
    one=$answer
    one_status=$answer_status
    capture "$stage2" "$option"
    two=$answer
    two_status=$answer_status
    two_compared=$two
    if [ "$option" = --help ]; then
        if [[ "$two" != *"  --cfg <list>"* ]]; then
            echo "stage_usage_test.sh: stage2 help omits --cfg" >&2
            status=1
        fi
        # The source compiler owns this option. Remove its one extra line before
        # the frozen bootstrap's remaining help is compared.
        two_compared=$(printf '%s\n' "$two" | sed '/^  --cfg <list>/d')
    fi
    if [ -z "$one" ]; then
        echo "stage_usage_test.sh: stage1 printed nothing for $option" >&2
        status=1
    fi
    # --help and --version print and exit 0 (toolchain.md 1).
    if [ "$one_status" -ne 0 ] || [ "$two_status" -ne 0 ]; then
        echo "stage_usage_test.sh: $option exited $one_status (stage1) and" \
            "$two_status (stage2), expected 0" >&2
        status=1
    fi
    if [ "$one" != "$two_compared" ]; then
        echo "stage_usage_test.sh: stage1 and stage2 disagree about $option" >&2
        diff -u <(printf '%s\n' "$one") <(printf '%s\n' "$two_compared") >&2 || true
        status=1
    fi
done

# With no argument each prints one usage line on stderr and exits 2
# (toolchain.md 1), so the status is part of the answer here as well.
one=$("$stage1" 2>&1 >/dev/null) && one_status=0 || one_status=$?
two=$("$stage2" 2>&1 >/dev/null) && two_status=0 || two_status=$?
if [ -z "$one" ]; then
    echo "stage_usage_test.sh: stage1 printed no usage line" >&2
    status=1
fi
if [ "$one_status" -ne 2 ] || [ "$two_status" -ne 2 ]; then
    echo "stage_usage_test.sh: no argument exited $one_status (stage1) and" \
        "$two_status (stage2), expected 2" >&2
    status=1
fi
if [ "$one" != "$two" ]; then
    echo "stage_usage_test.sh: the usage lines differ:" >&2
    echo "  stage1: $one" >&2
    echo "  stage2: $two" >&2
    status=1
fi

# The usage errors of toolchain.md 1 are the other half of the option table:
# src/fort/main.ft parses the command line of D14.1 a second time, so the two
# compilers must refuse the same lines in the same words. Each case is a whole
# command line after the program name.
usage_cases=(
    "--bogus main.ft"
    "-x main.ft"
    "-l main.ft"
    "a.ft b.ft"
    "-S --release"
    "main.ft -o"
    "--json main.ft"
    "--tokens --check main.ft"
    "--tokens --json main.ft"
    "--tokens --index main.ft"
    "--ast --tokens main.ft"
    "--ast --check main.ft"
    "--ast --json main.ft"
    "--ast --index main.ft"
    "- main.ft"
)
for line in "${usage_cases[@]}"; do
    read -r -a args <<<"$line"
    one=$("$stage1" "${args[@]}" 2>&1 >/dev/null) && one_status=0 || one_status=$?
    two=$("$stage2" "${args[@]}" 2>&1 >/dev/null) && two_status=0 || two_status=$?
    # A usage error is exit 2 with a `fort: error:` line (D14.1).
    if [ "$one_status" -ne 2 ] || [ "$two_status" -ne 2 ]; then
        echo "stage_usage_test.sh: 'fort $line' exited $one_status (stage1) and" \
            "$two_status (stage2), expected 2" >&2
        status=1
    fi
    if [ "$one" != "$two" ]; then
        echo "stage_usage_test.sh: 'fort $line' is reported differently:" >&2
        diff -u <(printf '%s\n' "$one") <(printf '%s\n' "$two") >&2 || true
        status=1
    fi
done

# An empty argument is not an option, so it is the entry file and the real one
# after it is a second (D14.1). It cannot go in the list above, since `read -a`
# splits on whitespace and would not give either binary an empty argument.
one=$("$stage1" "" main.ft 2>&1 >/dev/null) && one_status=0 || one_status=$?
two=$("$stage2" "" main.ft 2>&1 >/dev/null) && two_status=0 || two_status=$?
if [ "$one_status" -ne 2 ] || [ "$two_status" -ne 2 ] || [ "$one" != "$two" ]; then
    echo "stage_usage_test.sh: an empty argument is reported differently:" >&2
    diff -u <(printf '%s\n' "$one") <(printf '%s\n' "$two") >&2 || true
    echo "stage_usage_test.sh: exits were $one_status (stage1) and $two_status (stage2)" >&2
    status=1
fi

# The command lines the two drivers must *accept* alike, which the ten above
# cannot show: every option --tokens leaves unused is parsed, its argument
# consumed and its value ignored (D14.1), so a `-o` that swallowed the entry
# file or an `-I` that did not would show up here as a different dump or a
# different status. The entry file is written here rather than named in the
# repository, since ctest runs this script from the build directory.
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
entry=$tmp/entry.ft
printf 'fn main() i32 { return 0; }\n' >"$entry"

accepted_cases=(
    "--tokens $entry"
    "--tokens -o $tmp/out.o -I $tmp --std-dir $tmp --cc nosuchcc --target t -Xcc -v $entry"
    "--tokens -S -c --release --no-bounds-check -lm -lfoo $entry"
    "$entry --tokens"
    "--ast $entry"
    "--ast -o $tmp/out.o -I $tmp --std-dir $tmp --cc nosuchcc --target t -Xcc -v $entry"
    "--ast -S -c --release --no-bounds-check -lm -lfoo $entry"
    "$entry --ast"
    "$entry --help"
)
for line in "${accepted_cases[@]}"; do
    read -r -a args <<<"$line"
    one=$("$stage1" "${args[@]}" 2>"$tmp/one.err") && one_status=0 || one_status=$?
    two=$("$stage2" "${args[@]}" 2>"$tmp/two.err") && two_status=0 || two_status=$?
    two_compared=$two
    if [[ "$line" == *"--help"* ]]; then
        two_compared=$(printf '%s\n' "$two" | sed '/^  --cfg <list>/d')
    fi
    if [ "$one_status" -ne 0 ] || [ "$two_status" -ne 0 ]; then
        echo "stage_usage_test.sh: 'fort $line' exited $one_status (stage1) and" \
            "$two_status (stage2), expected 0" >&2
        cat "$tmp/one.err" "$tmp/two.err" >&2
        status=1
    fi
    if [ -z "$one" ]; then
        echo "stage_usage_test.sh: 'fort $line' printed nothing" >&2
        status=1
    fi
    if [ "$one" != "$two_compared" ]; then
        echo "stage_usage_test.sh: 'fort $line' answers differently:" >&2
        diff -u <(printf '%s\n' "$one") <(printf '%s\n' "$two_compared") >&2 || true
        status=1
    fi
    if ! diff -u "$tmp/one.err" "$tmp/two.err" >"$tmp/err.diff"; then
        echo "stage_usage_test.sh: 'fort $line' writes different stderr:" >&2
        cat "$tmp/err.diff" >&2
        status=1
    fi
done

# The first of those lines is a plain dump, so its last line says the two
# binaries really lexed the file rather than both printing nothing (D14.2).
one=$("$stage1" --tokens "$entry")
if [ "${one##*$'\n'}" != '2:1-2:1 0 "" end of file' ]; then
    echo "stage_usage_test.sh: --tokens did not dump the entry file:" >&2
    printf '%s\n' "$one" >&2
    status=1
fi

# The same for --ast: the tree of the entry file, and not an empty module,
# which is what a binary that parsed nothing would print (D14.1).
one=$("$stage1" --ast "$entry")
if [ "$one" != "(module (fn (type (prim i32)) main (params) (block (return (int 0)))))" ]; then
    echo "stage_usage_test.sh: --ast did not dump the entry file:" >&2
    printf '%s\n' "$one" >&2
    status=1
fi

if [ "$status" -eq 0 ]; then
    echo "stage1 and stage2 agree about --version, the common help, the usage line and the"
    echo "${#usage_cases[@]} usage errors of toolchain.md 1, each with the exit status it gives"
    echo "it, and answer the ${#accepted_cases[@]} accepted lines with the same common bytes"
fi
exit "$status"
