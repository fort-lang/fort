#!/bin/bash
# Run the six ABI probes of the target: variadic tails, callbacks, aggregate
# results, large frames, nested callbacks and the program arguments.
#
# Each probe compiles a corpus program with `fort -S`, links the module with
# the target clang and runs it. The module text must carry the fixed form of
# each ABI rule, and the assembly of the large-frame probe must hold the
# inline stack probe of the target.
set -eu

if [ "$#" -ne 4 ]; then
    echo "usage: probe.sh <fort> <std-dir> <cc> <target-triple>" >&2
    exit 2
fi

probe_root=$(cd "$(dirname "$0")/../.." && pwd -P)
compiler=$(cd "$(dirname "$1")" && pwd -P)/$(basename "$1")
std=$(cd "$2" && pwd -P)
cc=$3
triple=$4
if [ ! -x "$compiler" ] || [ ! -f "$std/rt.ft" ]; then
    printf 'probe: compiler or standard root is missing\n' >&2
    exit 2
fi
case "$triple" in
arm64-apple-macosx*)
    target=darwin
    sdk=$(xcrun --sdk macosx --show-sdk-path)
    cc_args=(--target="$triple" -isysroot "$sdk")
    # The arm64 inline probe stores the zero register at the stack pointer.
    probe_site='^[[:space:]]+str[[:space:]]+xzr, \[sp\]$'
    ;;
*)
    target=linux
    cc_args=(--target="$triple")
    export QEMU_LD_PREFIX=${QEMU_LD_PREFIX:-/usr/x86_64-linux-gnu}
    # The x86-64 inline probe stores a zero at the stack pointer.
    probe_site='^[[:space:]]+movq[[:space:]]+\$0, \(%rsp\)$'
    ;;
esac
cd "$probe_root"
probe_dir=$(mktemp -d)
trap 'rm -r -- "$probe_dir"' EXIT

probe_count=0
for probe_name in variadic callback aggregate large_frames nested_callbacks args; do
    case "$probe_name" in
    variadic)
        probe_source=test/lang/run/ffi/014_variadic_tails.ft
        probe_helper_args=(test/lang/ffi/helpers.c)
        probe_expected=$'525\n4294967317'
        probe_expected_status=0
        probe_cli_args=()
        ;;
    callback)
        probe_source=test/lang/run/ffi/005_helpers_callback.ft
        probe_helper_args=(test/lang/ffi/helpers.c)
        probe_expected=$'30 -9\n43\n10'
        probe_expected_status=0
        probe_cli_args=()
        ;;
    aggregate)
        probe_source=test/lang/run/functions/005_aggregate_abi.ft
        probe_helper_args=()
        probe_expected=$'1 2 3\n100 8 1\nhello 5\n12 -3 250 9'
        probe_expected_status=0
        probe_cli_args=()
        ;;
    large_frames)
        probe_source=test/lang/run/modes/005_large_frames.ft
        probe_helper_args=()
        probe_expected=$'abcd\n4'
        probe_expected_status=0
        probe_cli_args=()
        ;;
    nested_callbacks)
        probe_source=test/lang/run/ffi/015_nested_variadic_callback.ft
        probe_helper_args=(test/lang/ffi/helpers.c)
        probe_expected=$'36'
        probe_expected_status=0
        probe_cli_args=()
        ;;
    args)
        probe_source=test/lang/run/functions/004_main_args.ft
        probe_helper_args=()
        probe_expected=$'4\nalpha gamma\nalpha beta gamma'
        probe_expected_status=3
        probe_cli_args=(alpha beta gamma)
        ;;
    esac
    "$compiler" -S --std-dir "$std" \
        -o "$probe_dir/$probe_name.ll" "$probe_source"
    "$cc" "${cc_args[@]}" -O1 -fPIE -Wno-override-module \
        -o "$probe_dir/$probe_name" "$probe_dir/$probe_name.ll" \
        "${probe_helper_args[@]}"
    if probe_actual=$("$probe_dir/$probe_name" "${probe_cli_args[@]}"); then
        probe_status=0
    else
        probe_status=$?
    fi
    if [ "$probe_actual" != "$probe_expected" ] || [ "$probe_status" -ne "$probe_expected_status" ]; then
        printf 'probe: %s differs; exit %s; output:\n%s\n' \
            "$probe_name" "$probe_status" "$probe_actual" >&2
        exit 1
    fi
    probe_count=$((probe_count + 1))
done

grep -Eq '^declare i32 @helper_apply\(ptr, i32\) nobuiltin$' "$probe_dir/callback.ll"
grep -Eq '^declare i64 @helper_tail_mix\(i32, \.\.\.\) nobuiltin$' "$probe_dir/variadic.ll"
grep -Eq '^declare i32 @helper_tail_nested_callback\(i32, \.\.\.\) nobuiltin$' \
    "$probe_dir/nested_callbacks.ll"
grep -Eq 'call i32 \(i32, \.\.\.\) @helper_tail_nested_callback\(' \
    "$probe_dir/nested_callbacks.ll"
grep -Eq 'call void @"005_aggregate_abi.make"\(ptr sret\(' "$probe_dir/aggregate.ll"
grep -Eq 'call void @"std.rt.args"\(ptr sret\(%fort.span\) %args\)' "$probe_dir/args.ll"
grep -Eq 'attributes #8 = .*"probe-stack"="inline-asm"' "$probe_dir/large_frames.ll"
"$cc" "${cc_args[@]}" -O1 -S -x ir -Wno-override-module \
    -o "$probe_dir/large_frames.s" "$probe_dir/large_frames.ll"
probe_stack_count=$(grep -Ec "$probe_site" "$probe_dir/large_frames.s")
[ "$probe_stack_count" -gt 0 ]
printf 'probe: %s; %s programs pass; %s stack probe sites\n' \
    "$target" "$probe_count" "$probe_stack_count"
