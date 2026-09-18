#!/bin/bash
# Drive the built language server with recorded input sessions.
# The test covers src/lsp/main.ft, standard input, standard output, and process status.
#
# Four sessions: an orderly one, whose answers are compared byte for byte and
# whose status is 0; `exit` without a shutdown, whose status is 1. An empty
# stream, whose status is also 1. The final session has non-ASCII document text,
# which holds the frame length to a count of bytes. Each one must also leave
# stderr empty, since nothing below the entry file may write anywhere but the
# frame stream.
# The binary is x86-64 and runs under qemu through binfmt_misc, like every
# program the compiler builds.
#
# ctest runs it as `lsp-binary`, with the binary's path as its one argument.
set -eu

if [ $# -ne 1 ]; then
    echo "usage: $0 <path to fort-lsp>" >&2
    exit 2
fi
server=$1
# A missing binary is a broken environment and not a failing module. The test
# exits 2 for this case, as test/pipeline_test.sh does.
if [ ! -x "$server" ]; then
    echo "lsp-binary: not built: $server" >&2
    exit 2
fi
export QEMU_LD_PREFIX=${QEMU_LD_PREFIX:-/usr/x86_64-linux-gnu}
# The locale is pinned, and it is pinned to a UTF-8 one on purpose. The byte
# count below is what a UTF-8 locale gets wrong, so a guard that runs under
# LC_ALL=C guards nothing. The guest reads en_US.UTF-8 today, which made the
# guard fire by accident of the image rather than by this line.
export LC_ALL=C.UTF-8

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

failures=0
fail() {
    echo "lsp-binary: $*" >&2
    failures=$((failures + 1))
}

# frame <body>: the body inside a Content-Length frame, on stdout.
# The body length counts bytes with `wc -c`. `${#1}` counts characters in the
# pinned UTF-8 locale. It would shorten the fourth session body.
frame() {
    printf 'Content-Length: %d\r\n\r\n%s' "$(printf '%s' "$1" | wc -c)" "$1"
}

# run_session <script file> <output file> <stderr file>: the status the server
# exits with. The stderr of the server is captured rather than inherited: this
# is the only test that runs src/lsp/main.ft over real descriptors. So it is
# the only one that can see a server which prints a line for every frame.
run_session() {
    set +e
    "$server" <"$1" >"$2" 2>"$3"
    local status=$?
    set -e
    echo "$status"
}

# expect_silent <what> <stderr file>: the server says nothing on stderr.
expect_silent() {
    if [ -s "$2" ]; then
        fail "$1 wrote to stderr:"
        cat "$2" >&2
    fi
}

# The two characters above ASCII the sessions below carry, written as the
# bytes they are: U+00E9 is two bytes and U+1D11E is four.
e_acute=$(printf '\303\251')
clef=$(printf '\360\235\204\236')

# The helper declares bytes: this body is 6 characters and 7 bytes, and a
# character count would declare 6 and cut the body short.
declared=$(frame '"caf'"$e_acute"'"' | tr -d '\r' | head -n 1 | cut -d' ' -f2)
if [ "$declared" -ne 7 ]; then
    fail "the frame helper declared $declared for a body of 7 bytes"
fi

initialize='{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}'
initialized='{"jsonrpc":"2.0","method":"initialized","params":{}}'
opened='{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":'
opened=$opened'{"uri":"file:///a.ft","languageId":"fort","version":1,"text":"fn main() i32"}}}'
hover='{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{}}'
shutdown='{"jsonrpc":"2.0","id":3,"method":"shutdown","params":null}'
exit_note='{"jsonrpc":"2.0","method":"exit","params":null}'

# ---- an orderly session ----------------------------------------------------------
{
    frame "$initialize"
    frame "$initialized"
    frame "$opened"
    frame "$hover"
    frame "$shutdown"
    frame "$exit_note"
} >"$work/orderly.in"

status=$(run_session "$work/orderly.in" "$work/orderly.out" "$work/orderly.err")
if [ "$status" -ne 0 ]; then
    fail "an orderly session exited $status, expected 0"
fi
expect_silent "an orderly session" "$work/orderly.err"

capabilities='{"jsonrpc":"2.0","id":1,"result":{"capabilities":{"positionEncoding":"utf-16",'
capabilities=$capabilities'"textDocumentSync":{"openClose":true,"change":1,'
capabilities=$capabilities'"save":{"includeText":false}}},"serverInfo":{"name":"fort"}}}'
not_found='{"jsonrpc":"2.0","id":2,"error":{"code":-32601,"message":'
not_found=$not_found'"the server does not implement the method textDocument/hover"}}'
{
    frame "$capabilities"
    frame "$not_found"
    frame '{"jsonrpc":"2.0","id":3,"result":null}'
} >"$work/orderly.want"

if ! cmp -s "$work/orderly.out" "$work/orderly.want"; then
    fail "an orderly session answered bytes it should not:"
    diff <(od -c "$work/orderly.want") <(od -c "$work/orderly.out") >&2 || true
fi

# ---- exit without shutdown -------------------------------------------------------
{
    frame "$initialize"
    frame "$exit_note"
} >"$work/abrupt.in"

status=$(run_session "$work/abrupt.in" "$work/abrupt.out" "$work/abrupt.err")
if [ "$status" -ne 1 ]; then
    fail "exit without shutdown exited $status, expected 1"
fi
expect_silent "exit without shutdown" "$work/abrupt.err"
frame "$capabilities" >"$work/abrupt.want"
if ! cmp -s "$work/abrupt.out" "$work/abrupt.want"; then
    fail "exit without shutdown answered more than the capabilities"
fi

# ---- an empty stream -------------------------------------------------------------
: >"$work/empty.in"
status=$(run_session "$work/empty.in" "$work/empty.out" "$work/empty.err")
if [ "$status" -ne 1 ]; then
    fail "an empty stream exited $status, expected 1"
fi
expect_silent "an empty stream" "$work/empty.err"
if [ -s "$work/empty.out" ]; then
    fail "an empty stream answered bytes"
fi

# ---- a session whose document text is not ASCII ----------------------------------
# The text holds a two-byte character and a four-byte one. So its body is four
# bytes longer than its characters. A character
# length ends the body inside its text. The server
# then reads the remaining body as a header block.
utf8_opened='{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":'
utf8_opened=$utf8_opened'{"uri":"file:///caf%C3%A9.ft","languageId":"fort",'
utf8_opened=$utf8_opened'"version":1,"text":"// caf'"$e_acute $clef"'\nfn main() i32"}}}'
{
    frame "$initialize"
    frame "$initialized"
    frame "$utf8_opened"
    frame "$shutdown"
    frame "$exit_note"
} >"$work/utf8.in"

status=$(run_session "$work/utf8.in" "$work/utf8.out" "$work/utf8.err")
if [ "$status" -ne 0 ]; then
    fail "a session with a text above ASCII exited $status, expected 0"
fi
expect_silent "a session with a text above ASCII" "$work/utf8.err"
{
    frame "$capabilities"
    frame '{"jsonrpc":"2.0","id":3,"result":null}'
} >"$work/utf8.want"
if ! cmp -s "$work/utf8.out" "$work/utf8.want"; then
    fail "a session with a text above ASCII answered bytes it should not:"
    diff <(od -c "$work/utf8.want") <(od -c "$work/utf8.out") >&2 || true
fi

if [ "$failures" -ne 0 ]; then
    echo "lsp-binary: $failures check(s) failed" >&2
    exit 1
fi
echo "lsp-binary: the server binary answered four sessions as recorded"
