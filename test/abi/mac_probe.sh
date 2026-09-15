#!/bin/bash
# Run the stage2 Apple arm64 ABI probes on the Mac host.
# This probe uses a local errno bridge until T-143 updates the library.
set -eu

probe_root=$(git rev-parse --show-toplevel)
cd "$probe_root"
FORT_VM_SLOT=1 tools/vm build
probe_target="arm64-apple-macosx$(sw_vers -productVersion)"
probe_dir=$(mktemp -d build/debug/mac-probe.XXXXXX)
trap 'rm -r -- "$probe_dir"' EXIT

probe_count=0
for probe_name in variadic callback aggregate large_frames; do
    case "$probe_name" in
    variadic)
        probe_source=test/lang/run/ffi/014_variadic_tails.ft
        probe_helper_args=(test/lang/ffi/helpers.c)
        probe_expected=$'525\n4294967317'
        ;;
    callback)
        probe_source=test/lang/run/ffi/005_helpers_callback.ft
        probe_helper_args=(test/lang/ffi/helpers.c)
        probe_expected=$'30 -9\n43\n10'
        ;;
    aggregate)
        probe_source=test/lang/run/functions/005_aggregate_abi.ft
        probe_helper_args=()
        probe_expected=$'1 2 3\n100 8 1\nhello 5\n12 -3 250 9'
        ;;
    large_frames)
        probe_source=test/lang/run/modes/005_large_frames.ft
        probe_helper_args=()
        probe_expected=$'abcd\n4'
        ;;
    esac
    FORT_VM_SLOT=1 tools/vm run build/debug/stage2/fort -S \
        --target "$probe_target" --std-dir std \
        -o "$probe_dir/$probe_name.ll" "$probe_source"
    xcrun clang -O1 -fPIE -Wno-override-module \
        -o "$probe_dir/$probe_name" "$probe_dir/$probe_name.ll" \
        test/abi/mac_errno_shim.c "${probe_helper_args[@]}"
    probe_actual=$("$probe_dir/$probe_name")
    if [ "$probe_actual" != "$probe_expected" ]; then
        printf 'mac_probe: %s output differs\n%s\n' "$probe_name" "$probe_actual" >&2
        exit 1
    fi
    probe_count=$((probe_count + 1))
done

rg -q '^declare i32 @helper_apply\(ptr, i32\)$' "$probe_dir/callback.ll"
rg -q '^declare i64 @helper_tail_mix\(i32, \.\.\.\)$' "$probe_dir/variadic.ll"
rg -q 'call void @"005_aggregate_abi.make"\(ptr sret\(' "$probe_dir/aggregate.ll"
rg -q 'attributes #8 = .*__chkstk_darwin' "$probe_dir/large_frames.ll"
xcrun clang -O1 -S -x ir -Wno-override-module \
    -o "$probe_dir/large_frames.s" "$probe_dir/large_frames.ll"
probe_stack_count=$(rg -c '___chkstk_darwin@GOTPAGE$' "$probe_dir/large_frames.s")
[ "$probe_stack_count" -gt 0 ]
printf 'mac_probe: %s programs pass; %s Darwin stack probe sites\n' \
    "$probe_count" "$probe_stack_count"
