#!/bin/bash
# Build each source compiler stage for one selected target (D14.7).
#
# The user-supplied seed builds the source baseline. Each source pin builds
# the next pin. The last pin builds HEAD. The script records the default IR
# triple and executable file format of each compiler that it builds.
set -eu

usage() {
    cat <<'EOF' >&2
usage: bootstrap_chain.sh linux|darwin <seed> <build-dir>
                          [--cc <clang>] [--opt <opt>] [--file <file>]
                          [--ref <file>] [--seed-ref <file>]
EOF
}

die() {
    echo "bootstrap_chain.sh: $*" >&2
    exit 1
}

if [ "$#" -lt 3 ]; then
    usage
    exit 2
fi
target_name=$1
seed_input=$2
build_input=$3
shift 3

case "$target_name" in
linux)
    target=x86_64-linux-gnu
    expected_triple=x86_64-unknown-linux-gnu
    default_cc=clang
    default_opt=opt-18
    ;;
darwin)
    target=""
    expected_triple=""
    default_cc=/usr/bin/clang
    default_opt=/opt/homebrew/bin/opt
    ;;
*)
    usage
    exit 2
    ;;
esac

cc=${FORT_TARGET_CC:-$default_cc}
opt=${FORT_OPT:-$default_opt}
file_tool=file
ref_override=""
seed_ref_override=""
while [ "$#" -gt 0 ]; do
    if [ "$#" -lt 2 ]; then
        echo "bootstrap_chain.sh: $1 needs an argument" >&2
        usage
        exit 2
    fi
    case "$1" in
    --cc) cc=$2 ;;
    --opt) opt=$2 ;;
    --file) file_tool=$2 ;;
    --ref) ref_override=$2 ;;
    --seed-ref) seed_ref_override=$2 ;;
    *)
        echo "bootstrap_chain.sh: unknown option '$1'" >&2
        usage
        exit 2
        ;;
    esac
    shift 2
done

root=$(cd "$(dirname "$0")/.." && pwd -P)
case "$build_input" in
/*) build=$build_input ;;
*) build=$PWD/$build_input ;;
esac
mkdir -p "$build"
build=$(cd "$build" && pwd -P)

for tool in python3 "$cc" "$opt" "$file_tool"; do
    command -v "$tool" >/dev/null || {
        echo "bootstrap_chain.sh: tool not found: $tool" >&2
        exit 2
    }
done

ref=${ref_override:-$root/tools/bootstrap.ref}
seed_ref=${seed_ref_override:-$root/tools/bootstrap.seed}
pin=$root/tools/pin.sh
verify_seed=$root/tools/verify_seed.py
generator=$root/tools/gen_mac_platform.sh

bash "$pin" verify --ref "$ref" >/dev/null
baseline=$(sed -n 's/^baseline \([0-9a-f]\{40\}\)$/\1/p' "$seed_ref")
first_pin=$(sed -n 's/^bootstrap-0[[:space:]][[:space:]]*\([0-9a-f]\{40\}\)$/\1/p' "$ref")
if [ -z "$baseline" ] || [ "$first_pin" != "$baseline" ]; then
    die "bootstrap-0 must equal the source baseline in tools/bootstrap.seed"
fi

# Copy top-level standard modules into one flat target root. The darwin target replaces
# libc.ft and net.ft with the target sources that still use std/mac until
# T-152 renames that directory.
prepare_std() {
    local source=$1
    local output=$2
    local source_file name
    rm -rf "$output"
    mkdir -p "$output"
    for source_file in "$source"/*.ft; do
        name=${source_file##*/}
        if [ "$target_name" = darwin ] && { [ "$name" = libc.ft ] || [ "$name" = net.ft ]; }; then
            continue
        fi
        cp "$source_file" "$output/$name"
    done
    if [ "$target_name" = darwin ]; then
        for source_file in "$source"/mac/*.ft; do
            [ -f "$source_file" ] || die "darwin standard source is missing: $source/mac"
            cp "$source_file" "$output/${source_file##*/}"
        done
    fi
}

# The entry directory selects the target platform module before src/fort.
prepare_entry() {
    local source=$1
    local output=$2
    rm -rf "$output"
    mkdir -p "$output"
    if [ "$target_name" = linux ]; then
        cp "$source/main.ft" "$output/main.ft"
        cp "$source/platform.ft" "$output/platform.ft"
    else
        bash "$generator" "$output" "${target#arm64-apple-macosx}" \
            "$source/main.ft"
    fi
}

# First extract the baseline. Its standard root lets the verifier exercise the
# supplied compiler before that compiler becomes a build input.
first_tree=$build/pin/0
bash "$pin" extract "$baseline" "$first_tree" --ref "$ref"
prepare_std "$first_tree/std" "$first_tree/target/std"

identity=$build/seed.identity
rm -f "$identity"
python3 "$verify_seed" "$target_name" "$seed_input" \
    "$first_tree/target/std" --ref "$seed_ref" >"$identity"
seed_hash=$(sed -n 's/^seed SHA-256: //p' "$identity")
seed_triple=$(sed -n 's/^default triple: //p' "$identity")
if [ "$target_name" = darwin ]; then
    target=$seed_triple
    expected_triple=$seed_triple
fi

# Use one stable copy after verification. The copy hash must equal the hash
# that the verifier recorded for the supplied executable.
seed_dir=$build/seed
mkdir -p "$seed_dir"
seed_tmp=$seed_dir/fort.tmp
seed=$seed_dir/fort
rm -f "$seed_tmp"
cp "$seed_input" "$seed_tmp"
chmod 700 "$seed_tmp"
copy_hash=$(python3 - "$seed_tmp" <<'PY'
import hashlib
import pathlib
import sys

print(hashlib.sha256(pathlib.Path(sys.argv[1]).read_bytes()).hexdigest())
PY
)
if [ "$copy_hash" != "$seed_hash" ]; then
    rm -f "$seed_tmp"
    die "the seed changed after verification"
fi
rm -f "$seed"
mv "$seed_tmp" "$seed"

probe=$build/bootstrap-probe.ft
printf 'fn main() i32 { return 0; }\n' >"$probe"
record=$build/bootstrap-stages.tsv
record_tmp=$record.tmp
rm -f "$record" "$record_tmp"
printf 'stage\tsource-sha\tdefault-triple\tfile-format\n' >"$record_tmp"

record_stage() {
    local stage=$1
    local source_sha=$2
    local compiler=$3
    local std=$4
    local module=$build/$stage.default.ll
    local count triple format
    rm -f "$module"
    "$compiler" -S --std-dir "$std" -o "$module" "$probe"
    if [ ! -s "$module" ]; then
        die "$stage wrote no default-target IR module"
    fi
    "$opt" -passes=verify -disable-output "$module"
    count=$(grep -c '^target triple = "[^"]*"$' "$module" || true)
    triple=$(sed -n 's/^target triple = "\([^"]*\)"$/\1/p' "$module")
    if [ "$count" -ne 1 ] || [ "$triple" != "$expected_triple" ]; then
        die "$stage has default triple '$triple'; expected '$expected_triple'"
    fi
    format=$("$file_tool" -b "$compiler")
    case "$target_name:$format" in
    linux:"ELF 64-bit LSB pie executable, x86-64"*) ;;
    darwin:"Mach-O 64-bit executable arm64"*) ;;
    *) die "$stage has the wrong file format: $format" ;;
    esac
    printf '%s\t%s\t%s\t%s\n' "$stage" "$source_sha" "$triple" "$format" \
        >>"$record_tmp"
    echo "$stage: $triple; $format"
}

compile_stage() {
    local compiler=$1
    local source=$2
    local source_root=$3
    local std=$4
    local output=$5
    rm -f "$output"
    mkdir -p "$(dirname "$output")"
    "$compiler" --std-dir "$std" -I "$source_root" --cc "$cc" --target "$target" \
        -o "$output" "$source"
    if [ ! -x "$output" ]; then
        die "$compiler exited 0 and wrote no executable $output"
    fi
}

previous=$seed
previous_name=seed
pin_count=0
while read -r name sha extra; do
    [ -n "$name" ] || continue
    case "$name" in \#*) continue ;; esac
    [ -z "${extra:-}" ] || die "malformed pin line for $name"
    number=${name#bootstrap-}
    tree=$build/pin/$number
    if [ "$number" != 0 ]; then
        bash "$pin" extract "$sha" "$tree" --ref "$ref"
        prepare_std "$tree/std" "$tree/target/std"
    fi
    prepare_entry "$tree/src/fort" "$tree/target/entry"
    binary=$tree/fort
    echo "== $name: $previous_name builds $sha for $target_name"
    compile_stage "$previous" "$tree/target/entry/main.ft" "$tree/src/fort" \
        "$tree/target/std" "$binary"
    record_stage "$name" "$sha" "$binary" "$tree/target/std"
    previous=$binary
    previous_name=$name
    pin_count=$((pin_count + 1))
done <"$ref"
[ "$pin_count" -gt 0 ] || die "tools/bootstrap.ref names no source pin"

head_dir=$build/bootstrap-head
prepare_std "$root/std" "$head_dir/std"
prepare_entry "$root/src/fort" "$head_dir/entry"
head_sha=$(git -C "$root" rev-parse HEAD)
stage2=$build/stage2/fort
echo "== stage2: $previous_name builds HEAD for $target_name"
compile_stage "$previous" "$head_dir/entry/main.ft" "$root/src/fort" \
    "$head_dir/std" "$stage2"
record_stage stage2 "$head_sha" "$stage2" "$head_dir/std"

mv "$record_tmp" "$record"
echo "bootstrap chain: $pin_count source pin(s) and HEAD built for $target_name"
echo "bootstrap chain record: $record"
