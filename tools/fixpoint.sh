#!/bin/bash
# tools/fixpoint.sh <build-dir>: verify compiler reproduction in both build modes.
#
# CMake supplies stage2. Stage2 builds stage3, and stage3 builds stage4.
# Each mode compares the stage2 and stage3 modules, then the stage3 and stage4 binaries.
# The script verifies each module before it compares output bytes.
# Checked and release modes emit different overflow behavior, so both modes run.
#
# The script writes temporary output below <build-dir>/fixpoint/<mode>.
# Each stage uses identical source paths because those paths can enter failure blocks.
# The script does not change <build-dir>/fort or other product-test inputs.
set -eu

usage() {
    echo "usage: fixpoint.sh <build-dir> --compiler <fort>" >&2
    echo "                   [--cc <clang>]" >&2
    echo "                   [--target <triple>] [--opt <opt>] [--entry <file>]" >&2
    echo "                   [--std <dir>] [--source-root <dir>]" >&2
}

# The compiler drives clang over its emitted LLVM IR.
# The guest `cc` is gcc, so the command names the target compiler.
# CMake passes the configured compiler and verifier. Environment variables support hand runs.
cc=${FORT_TARGET_CC:-clang}
opt=${FORT_OPT:-opt-18}
target=${FORT_TARGET_TRIPLE:-x86_64-linux-gnu}
# The current compiler. CMake passes it from the graph.
compiler=
entry=
std=
source_root=

if [ "$#" -lt 1 ]; then
    usage
    exit 2
fi
build=$1
shift
while [ "$#" -gt 0 ]; do
    if [ "$#" -lt 2 ]; then
        echo "fixpoint.sh: $1 needs an argument" >&2
        usage
        exit 2
    fi
    case "$1" in
        --compiler) compiler=$2 ;;
        --cc) cc=$2 ;;
        --target) target=$2 ;;
        --opt) opt=$2 ;;
        --entry) entry=$2 ;;
        --std) std=$2 ;;
        --source-root) source_root=$2 ;;
        *)
            echo "fixpoint.sh: unknown argument '$1'" >&2
            usage
            exit 2
            ;;
    esac
    shift 2
done
root=$(cd "$(dirname "$0")/.." && pwd)
entry=${entry:-$root/src/fort/main.ft}
std=${std:-$build/std}
source_root=${source_root:-$(dirname "$entry")}
if [ -z "$compiler" ]; then
    echo "fixpoint.sh: --compiler is required" >&2
    exit 2
fi
export QEMU_LD_PREFIX=${QEMU_LD_PREFIX:-/usr/x86_64-linux-gnu}

case "$target" in
x86_64-linux-gnu)
    expected_triple=x86_64-unknown-linux-gnu
    target_name=linux
    ;;
arm64-apple-macosx[0-9]*.[0-9]*.[0-9]*)
    if ! [[ "$target" =~ ^arm64-apple-macosx[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        echo "fixpoint.sh: unsupported target '$target'" >&2
        exit 2
    fi
    expected_triple=$target
    target_name=darwin
    ;;
*)
    echo "fixpoint.sh: unsupported target '$target'" >&2
    exit 2
    ;;
esac

if [ ! -x "$compiler" ]; then
    echo "fixpoint.sh: build/fort is not built" >&2
    exit 2
fi
if [ ! -f "$entry" ] || [ ! -d "$std" ] || [ ! -d "$source_root" ]; then
    echo "fixpoint.sh: target entry, source root, or standard root is missing" >&2
    exit 2
fi

# A missing tool is an environment error. Exit 2 before a compiler comparison.
for tool in "$opt" "$cc"; do
    command -v "$tool" >/dev/null || {
        echo "fixpoint.sh: $tool not found (llvm-18 and clang, see tools/provision.sh)" >&2
        exit 2
    }
done

work=$build/fixpoint
mkdir -p "$work"

status=0

# compile <compiler> <mode-flags> <output>: compile src/fort with one compiler.
# Remove stale output before the compile. A successful compiler must write new output.
compile() {
    local compiler=$1
    local flags=$2
    local output=$3
    mkdir -p "$(dirname "$output")"
    rm -f "$output"
    # $flags is empty in checked mode and `--release` in release mode. It is
    # one option and not a path, so the split of an empty $flags into no
    # argument is what is wanted here.
    # shellcheck disable=SC2086
    "$compiler" $flags --std-dir "$std" -I "$source_root" \
        --cc "$cc" --target "$target" \
        -o "$output" "$entry"
    if [ ! -x "$output" ]; then
        echo "fixpoint.sh: $compiler exited 0 and wrote no $output" >&2
        return 1
    fi
}

# is_module <file>: require a non-empty module.
# Empty files compare equal, and opt accepts them. Reject them before comparison.
is_module() {
    if [ ! -s "$1" ]; then
        echo "fixpoint.sh: $1 is empty" >&2
        return 1
    fi
    if ! grep -q "^target triple = \"$expected_triple\"$" "$1"; then
        echo "fixpoint.sh: $1 is not a module of D19.1" >&2
        return 1
    fi
}

# emit <compiler> <mode-flags> <output.ll> -- the module of src/fort.
emit() {
    local compiler=$1
    local flags=$2
    local output=$3
    # `-S` stops before --cc, so it names no target compiler.
    # It keeps --target, which names the triple that the module carries.
    # shellcheck disable=SC2086
    "$compiler" $flags -S --std-dir "$std" -I "$source_root" --target "$target" \
        -o "$output" "$entry"
}

# check_mode <name> <mode-flags> -- the fixed point in one build mode.
check_mode() {
    local mode=$1
    local flags=$2
    local two=$compiler
    local three=$work/$mode/stage3/fort
    local four=$work/$mode/stage4/fort
    local two_ll=$work/$mode/stage2.ll
    local three_ll=$work/$mode/stage3.ll

    echo "== $mode: using build/fort from the CMake product graph"
    mkdir -p "$work/$mode"

    echo "== $mode: stage3"
    if [ "$target_name" = darwin ]; then
        three=$work/$mode/stage3.copy
        four=$work/$mode/fort
        if ! compile "$two" "$flags" "$four"; then
            echo "fixpoint.sh: $mode: stage2 could not compile src/fort" >&2
            status=1
            return
        fi
        cp "$four" "$three"
    elif ! compile "$two" "$flags" "$three"; then
        echo "fixpoint.sh: $mode: stage2 could not compile src/fort" >&2
        status=1
        return
    fi

    echo "== $mode: the modules stage2 and stage3 emit"
    # Neither module of the other mode may stand here, for the reason the
    # binaries above are removed. A compiler that exits 0 and writes nothing
    # would otherwise make cmp read the previous file and report agreement.
    rm -f "$two_ll" "$three_ll"
    if ! emit "$two" "$flags" "$two_ll"; then
        echo "fixpoint.sh: $mode: stage2 emitted no module for src/fort" >&2
        status=1
        return
    fi
    if ! emit "$three" "$flags" "$three_ll"; then
        echo "fixpoint.sh: $mode: stage3 emitted no module for src/fort" >&2
        status=1
        return
    fi
    for module in "$two_ll" "$three_ll"; do
        if ! is_module "$module"; then
            echo "fixpoint.sh: $mode: $module is no module to compare" >&2
            status=1
            return
        fi
        if ! "$opt" -passes=verify -disable-output "$module"; then
            echo "fixpoint.sh: $mode: the verifier refused $module" >&2
            status=1
            return
        fi
    done
    if cmp -s "$two_ll" "$three_ll"; then
        echo "$mode: the emitted modules are identical and both verify"
    else
        # Use cmp for the result and diff for details. Limit the large module diff.
        echo "fixpoint.sh: $mode: the modules stage2 and stage3 emit differ" >&2
        echo "fixpoint.sh:   diff $two_ll $three_ll" >&2
        diff -u "$two_ll" "$three_ll" >"$work/$mode/module.diff" || true
        head -n 40 "$work/$mode/module.diff" >&2
        status=1
        return
    fi

    echo "== $mode: stage4"
    if [ "$target_name" = darwin ]; then
        rm -f "$four"
    fi
    if ! compile "$three" "$flags" "$four"; then
        echo "fixpoint.sh: $mode: stage3 could not compile src/fort" >&2
        status=1
        return
    fi
    if cmp -s "$three" "$four"; then
        echo "$mode: stage3 and stage4 are identical: the compiler is self-hosted"
    else
        echo "fixpoint.sh: $mode: the emitted modules agree but the binaries differ;" >&2
        echo "fixpoint.sh: $mode: that is clang or the linker, not the compiler" >&2
        status=1
    fi
}

check_mode checked ""
check_mode release --release

exit "$status"
