#!/bin/bash
# Run the stage2 Apple arm64 ABI probes on the darwin host.
# T-144 owns the darwin library update. This probe uses a local errno bridge.
set -eu

probe_root=$(git rev-parse --show-toplevel)
cd "$probe_root"
probe_identity=${FORT_DARWIN_PROBE_IDENTITY:-build/darwin/seed.identity}
probe_target=$(bash tools/seed_target.sh "$probe_identity")
if ! [[ "$probe_target" =~ ^arm64-apple-macosx[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    printf 'darwin_probe: seed target is not darwin: %s\n' "$probe_target" >&2
    exit 1
fi
if [ "${1:-}" = --print-target ]; then
    printf '%s\n' "$probe_target"
    exit 0
fi
if [ "$#" -ne 0 ]; then
    printf 'usage: darwin_probe.sh [--print-target]\n' >&2
    exit 2
fi
compiler=build/darwin/stage2/fort
if [ "$(uname -sm)" != "Darwin arm64" ] || [ ! -x "$compiler" ]; then
    printf 'darwin_probe: requires the darwin compiler on a darwin arm64 host\n' >&2
    exit 2
fi
probe_dir=$(mktemp -d build/darwin/darwin-probe.XXXXXX)
trap 'rm -r -- "$probe_dir"' EXIT

probe_clang() {
    xcrun clang --target="$probe_target" "$@"
}

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
    "$compiler" -S --target "$probe_target" --std-dir build/darwin/std \
        -o "$probe_dir/$probe_name.ll" "$probe_source"
    probe_clang -O1 -fPIE -Wno-override-module \
        -o "$probe_dir/$probe_name" "$probe_dir/$probe_name.ll" \
        test/abi/darwin_errno_shim.c "${probe_helper_args[@]}"
    if probe_actual=$("$probe_dir/$probe_name" "${probe_cli_args[@]}"); then
        probe_status=0
    else
        probe_status=$?
    fi
    if [ "$probe_actual" != "$probe_expected" ] || [ "$probe_status" -ne "$probe_expected_status" ]; then
        printf 'darwin_probe: %s differs; exit %s; output:\n%s\n' \
            "$probe_name" "$probe_status" "$probe_actual" >&2
        exit 1
    fi
    probe_count=$((probe_count + 1))
done

rg -q '^declare i32 @helper_apply\(ptr, i32\) nobuiltin$' "$probe_dir/callback.ll"
rg -q '^declare i64 @helper_tail_mix\(i32, \.\.\.\) nobuiltin$' "$probe_dir/variadic.ll"
rg -q '^declare i32 @helper_tail_nested_callback\(i32, \.\.\.\) nobuiltin$' \
    "$probe_dir/nested_callbacks.ll"
rg -q 'call i32 \(i32, \.\.\.\) @helper_tail_nested_callback\(' \
    "$probe_dir/nested_callbacks.ll"
rg -q 'call void @"005_aggregate_abi.make"\(ptr sret\(' "$probe_dir/aggregate.ll"
rg -q 'call void @"std.rt.args"\(ptr sret\(%fort.span\) %args\)' "$probe_dir/args.ll"
rg -q 'attributes #8 = .*"probe-stack"="inline-asm"' "$probe_dir/large_frames.ll"
probe_clang -O1 -S -x ir -Wno-override-module \
    -o "$probe_dir/large_frames.s" "$probe_dir/large_frames.ll"
probe_stack_count=$(rg -c '^\s+str\s+xzr, \[sp\]$' "$probe_dir/large_frames.s")
[ "$probe_stack_count" -gt 0 ]
printf 'darwin_probe: %s programs pass; %s Darwin stack probe sites\n' \
    "$probe_count" "$probe_stack_count"
