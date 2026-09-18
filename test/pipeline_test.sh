#!/bin/bash
# Run the cross pipeline on the hand-written LLVM IR modules under test/ir.
# Verify each module before the target clang compiles and links it.
# Run each binary under qemu through binfmt_misc.
# Each module contains its required `std.rt` entry points and links alone.
# The hello fixture prints one line and exits 0.
# The abort fixture reports a runtime error and exits on SIGABRT.
# The colons fixture keeps a `:` in its fort symbol.
# Each binary must be position independent.
set -eu

if [ $# -ne 0 ]; then
    echo "usage: $0" >&2
    exit 2
fi
ir=$(cd "$(dirname "$0")/ir" && pwd)
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

# A missing tool is a broken environment, not a failing module. Report it as
# such (exit 2) instead of letting the verification or the link fail below.
for tool in "$opt" "$cc" readelf nm; do
    command -v "$tool" >/dev/null || {
        echo "pipeline: $tool not found (llvm-18, clang and binutils, see tools/provision.sh)" >&2
        exit 2
    }
done

# The module must satisfy the IR verifier before anything compiles it, so a
# malformed module is reported as such and not as a compiler crash.
for prog in hello abort colons; do
    "$opt" -passes=verify -disable-output "$ir/$prog.ll" ||
        fail "$prog: the IR verifier rejected the module"
    "$cc" --target="$target" -O1 -fPIE -Wno-override-module \
        -o "$work/$prog" "$ir/$prog.ll" ||
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
# stderr ("qemu: uncaught target signal 6 (Aborted) - core dumped"). Native
# execution prints nothing, so the line is not part of the expected output.
drop_qemu_notice() {
    grep -v '^qemu: uncaught target signal' "$1" >"$1.clean" || true
    mv "$1.clean" "$1"
}

# abort: the buffered line reaches stdout before the runtime error.
# The error is on stderr, and the process dies with SIGABRT.
status=0
"$work/abort" >"$work/abort.out" 2>"$work/abort.err" || status=$?
[ "$status" -eq 134 ] || fail "abort: exit status $status, expected 134 (SIGABRT)"
drop_qemu_notice "$work/abort.err"
expect_file abort.stdout "$work/abort.out" 'before
'
expect_file abort.stderr "$work/abort.err" \
    'abort.ft:12:13: runtime error: index 5 out of range for length 3
'

# With both descriptors on one pipe the stdout line is flushed before the
# error line is written.
"$work/abort" >"$work/abort.both" 2>&1 || true
drop_qemu_notice "$work/abort.both"
expect_file abort.order "$work/abort.both" 'before
abort.ft:12:13: runtime error: index 5 out of range for length 3
'

# colons: an entry base name can contain any byte except `.`.
# The module path and its symbols can contain `:`. Only the link checks that
# LLVM, the assembler, and ELF preserve this byte.
status=0
"$work/colons" >"$work/colons.out" 2>"$work/colons.err" || status=$?
[ "$status" -eq 0 ] || fail "colons: exit status $status, expected 0"
expect_file colons.stdout "$work/colons.out" 'colon
'
expect_file colons.stderr "$work/colons.err" ''
if ! nm "$work/colons" | grep -q ' T my:app.main$'; then
    fail "colons: the ELF symbol table holds no 'my:app.main':"
    nm "$work/colons" >&2
fi

if [ "$failures" -ne 0 ]; then
    echo "pipeline: $failures failure(s)" >&2
    exit 1
fi
echo "pipeline: ok"
