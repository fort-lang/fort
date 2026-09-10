#!/bin/bash
# test/pipeline_test.sh <build-dir>: the cross pipeline of toolchain.md 2 on
# the hand-written programs under test/asm. Each program is assembled and
# linked by the target C compiler with <build-dir>/std/fort_rt.o, exactly as
# the compiler will do it, and run under qemu through binfmt_misc. hello must
# print its line and exit 0; abort must print its line, then the runtime error
# of D11.4 on stderr, and die with SIGABRT (status 134). Both binaries must be
# position independent (D14.3). ctest runs it as the unit test `pipeline`.
set -eu

if [ $# -ne 1 ]; then
    echo "usage: $0 <build-dir>" >&2
    exit 2
fi
build=$1
asm=$(cd "$(dirname "$0")/asm" && pwd)
runtime=$build/std/fort_rt.o
cc=${FORT_TARGET_CC:-x86_64-linux-gnu-gcc}
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

for prog in hello abort; do
    "$cc" -o "$work/$prog" "$asm/$prog.s" "$runtime" ||
        fail "$prog: assembling and linking failed"
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
# stderr ("qemu: uncaught target signal 6 (Abort) - core dumped"); native
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

if [ "$failures" -ne 0 ]; then
    echo "pipeline: $failures failure(s)" >&2
    exit 1
fi
echo "pipeline: ok"
