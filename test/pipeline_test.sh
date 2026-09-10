#!/bin/bash
# test/pipeline_test.sh <build-dir>: the cross pipeline of toolchain.md 2 on
# the hand-written LLVM IR modules under test/ir, which are the reference for
# the form of a module (D19.1). Every emitted module must pass the verifier
# (D19.1), so each one is verified with `opt -passes=verify` first, then
# compiled and linked by the target clang with <build-dir>/std/fort_rt.o,
# exactly as the compiler does it (D14.3), and run under qemu through
# binfmt_misc. hello must print its line and exit 0; abort must
# print its line, then the runtime error of D11.4 on stderr, and die with
# SIGABRT (status 134); floats must print the D11.7 text of each of its
# values. Every binary must be position independent (D14.3). ctest runs it as
# the unit test `pipeline`.
set -eu

if [ $# -ne 1 ]; then
    echo "usage: $0 <build-dir>" >&2
    exit 2
fi
build=$1
ir=$(cd "$(dirname "$0")/ir" && pwd)
runtime=$build/std/fort_rt.o
cc=${FORT_TARGET_CC:-clang}
opt=${FORT_OPT:-opt-18}
target=${FORT_TARGET_TRIPLE:-x86_64-linux-gnu}
export QEMU_LD_PREFIX=${QEMU_LD_PREFIX:-/usr/x86_64-linux-gnu}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

failures=0
fail() {
    echo "pipeline: $*" >&2
    failures=$((failures + 1))
}

# expect_file <what> <file> <expected text>: byte-exact comparison.
expect_file() {
    if ! printf '%s' "$3" | cmp -s - "$2"; then
        fail "$1: unexpected content:"
        od -c "$2" >&2
    fi
}

test -f "$runtime" || {
    echo "pipeline: $runtime not found (build the fort_rt target first)" >&2
    exit 2
}

# A missing tool is a broken environment, not a failing module: report it as
# such (exit 2) instead of letting the verification or the link fail below.
for tool in "$opt" "$cc"; do
    command -v "$tool" >/dev/null || {
        echo "pipeline: $tool not found (llvm-18 and clang, see tools/provision.sh)" >&2
        exit 2
    }
done

# The module must satisfy the IR verifier before anything compiles it, so a
# malformed module is reported as such and not as a compiler crash.
for prog in hello abort floats; do
    "$opt" -passes=verify -disable-output "$ir/$prog.ll" ||
        fail "$prog: the IR verifier rejected the module"
    "$cc" --target="$target" -O1 -fPIE -pie -Wno-override-module \
        -o "$work/$prog" "$ir/$prog.ll" "$runtime" ||
        fail "$prog: compiling and linking failed"
    if ! readelf -h "$work/$prog" | grep -q 'Type: *DYN'; then
        fail "$prog: not a position-independent executable"
    fi
done

# hello: the line on stdout, nothing on stderr, status 0.
status=0
"$work/hello" >"$work/hello.out" 2>"$work/hello.err" || status=$?
[ "$status" -eq 0 ] || fail "hello: exit status $status, expected 0"
expect_file hello.stdout "$work/hello.out" 'hello, world!
'
expect_file hello.stderr "$work/hello.err" ''

# drop_qemu_notice <file>: qemu-user reports a fatal signal on the program's
# stderr ("qemu: uncaught target signal 6 (Aborted) - core dumped"); native
# execution prints nothing, so the line is not part of the expected output.
drop_qemu_notice() {
    grep -v '^qemu: uncaught target signal' "$1" >"$1.clean" || true
    mv "$1.clean" "$1"
}

# abort: the buffered line reaches stdout before the failure, the D11.4 line
# is on stderr, and the process dies with SIGABRT.
status=0
"$work/abort" >"$work/abort.out" 2>"$work/abort.err" || status=$?
[ "$status" -eq 134 ] || fail "abort: exit status $status, expected 134 (SIGABRT)"
drop_qemu_notice "$work/abort.err"
expect_file abort.stdout "$work/abort.out" 'before
'
expect_file abort.stderr "$work/abort.err" \
    'abort.ft:12:14: runtime error: index 5 out of range for length 3
'

# With both descriptors on one pipe the stdout line is flushed before the
# error line is written (D11.4).
"$work/abort" >"$work/abort.both" 2>&1 || true
drop_qemu_notice "$work/abort.both"
expect_file abort.order "$work/abort.both" 'before
abort.ft:12:14: runtime error: index 5 out of range for length 3
'

# floats: the D11.7 rendering of each value on stdout, nothing on stderr and
# status 0. The digits come from the target's own C library (D18.2), so this is
# where the printers are checked on the target.
status=0
"$work/floats" >"$work/floats.out" 2>"$work/floats.err" || status=$?
[ "$status" -eq 0 ] || fail "floats: exit status $status, expected 0"
expect_file floats.stdout "$work/floats.out" '0.1
1e+17
-0.0
inf
nan
0.1
16777216.0
'
expect_file floats.stderr "$work/floats.err" ''

if [ "$failures" -ne 0 ]; then
    echo "pipeline: $failures failure(s)" >&2
    exit 1
fi
echo "pipeline: ok"
