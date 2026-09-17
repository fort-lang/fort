#!/usr/bin/env bash
# Build and verify the source baseline with the target C bootstrap.
set -euo pipefail

if [ "$#" -ne 6 ]; then
    echo "usage: c_bootstrap_test.sh linux|darwin <fort> <build> <oracle> <cc> <opt>" >&2
    exit 2
fi

target_name=$1
compiler=$2
build=$3
oracle=$4
cc=$5
opt=$6
root=$(cd "$(dirname "$0")/.." && pwd -P)

case "$target_name" in
linux)
    expected_triple=x86_64-unknown-linux-gnu
    expected_format='ELF 64-bit LSB pie executable, x86-64'
    expected_cc=clang
    ;;
darwin)
    version=$(sw_vers -productVersion)
    if [[ "$version" =~ ^[0-9]+\.[0-9]+$ ]]; then
        version=$version.0
    elif [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        echo "c bootstrap: unsupported darwin host version '$version'" >&2
        exit 2
    fi
    expected_triple=arm64-apple-macosx$version
    expected_format='Mach-O 64-bit executable arm64'
    expected_cc=/usr/bin/clang
    ;;
*)
    echo "c bootstrap: target must be linux or darwin" >&2
    exit 2
    ;;
esac

case "$build" in
*/c-bootstrap) ;;
*) echo "c bootstrap: build path must end with /c-bootstrap" >&2; exit 2 ;;
esac
if [ ! -x "$compiler" ] || [ ! -d "$oracle/std" ] || [ ! -x "$cc" ] || [ ! -x "$opt" ]; then
    echo "c bootstrap: a compiler, oracle, clang, or verifier is missing" >&2
    exit 2
fi

rm -rf -- "$build"
mkdir -p "$build/source" "$build/target/std" "$build/target/entry"

baseline=$(sed -n 's/^baseline \([0-9a-f]\{40\}\)$/\1/p' "$root/tools/bootstrap.seed")
if [ -z "$baseline" ]; then
    echo "c bootstrap: tools/bootstrap.seed has no baseline" >&2
    exit 2
fi
git -C "$root" archive "$baseline" src/fort | tar -xf - -C "$build/source"

for source in "$oracle/std"/*.ft; do
    cp "$source" "$build/target/std/${source##*/}"
done
if [ "$target_name" = darwin ]; then
    rm -f -- "$build/target/std/libc.ft" "$build/target/std/net.ft"
    target_std=$root/std/darwin
    for source in "$target_std"/*.ft; do
        cp "$source" "$build/target/std/${source##*/}"
    done
    # The frozen runtime does not import its float module. Remove the one
    # Darwin libc wrapper that only that float module uses.
    awk '
        /^\/\/\/ Formats one float for std\.rt\./ { skip = 1; found += 1; next }
        skip && /^}$/ { skip = 0; next }
        !skip { print }
        END { if (skip || found != 1) exit 1 }
    ' "$target_std/libc.ft" >"$build/target/std/libc.ft"
    # The frozen runtime uses the Linux accessor name. Give that runtime one
    # fort wrapper around Darwin's accessor.
    printf '\nfn __errno_location() i32 mut* {\n    return __error();\n}\n' \
        >>"$build/target/std/libc.ft"
    bash "$root/tools/gen_darwin_platform.sh" "$build/target/entry" \
        "${expected_triple#arm64-apple-macosx}" "$build/source/src/fort/main.ft"
else
    cp "$build/source/src/fort/main.ft" "$build/target/entry/main.ft"
    cp "$build/source/src/fort/platform.ft" "$build/target/entry/platform.ft"
fi

# Count unsupported tokens in the compiler source and its standard closure.
# The remaining standard modules are outside this build's import closure.
tokens=$build/unsupported.tokens
: >"$tokens"
for source in "$build/source/src/fort"/*.ft \
    "$build/target/std"/{io,libc,mem,rt,str,strbuf,strmap,sys}.ft; do
    "$compiler" --tokens "$source" >>"$tokens"
done
unsupported=$(rg -n \
    ' "(f32|f64)" (f32|f64)$| float literal$| "\?" \?$' "$tokens" || true)
if [ -n "$unsupported" ]; then
    echo "c bootstrap: the compiler import closure has an unsupported form" >&2
    printf '%s\n' "$unsupported" >&2
    exit 1
fi

# Place the frozen root beside a compiler copy. The alias makes executable-path
# discovery resolve a symbolic link before it finds that root.
mkdir -p "$build/driver" "$build/alias"
cp "$compiler" "$build/driver/fort"
chmod 700 "$build/driver/fort"
ln -s "$build/target/std" "$build/driver/std"
ln -s ../driver/fort "$build/alias/fort"

printf 'fn main() i32 { return 0; }\n' >"$build/main.ft"
unset FORT_STD_DIR
"$build/alias/fort" --help >"$build/help.txt"
grep -F -q "(default $expected_cc)" "$build/help.txt"
"$build/alias/fort" -S -o "$build/default.ll" "$build/main.ft"
if [ "$(grep -c "^target triple = \"$expected_triple\"$" "$build/default.ll")" -ne 1 ]; then
    echo "c bootstrap: the stored target differs" >&2
    exit 1
fi
"$opt" -passes=verify -disable-output "$build/default.ll"

"$build/alias/fort" -o "$build/c-program" "$build/main.ft"
if ! file -b "$build/c-program" | grep -F -q "$expected_format"; then
    echo "c bootstrap: the C compiler output has the wrong format" >&2
    exit 1
fi
if [ "$target_name" = darwin ] &&
   ! file -b "$build/driver/fort" | grep -F -q "$expected_format"; then
    echo "c bootstrap: the Darwin C compiler has the wrong format" >&2
    exit 1
fi

baseline_fort=$build/baseline/fort
mkdir -p "$build/baseline"
"$build/alias/fort" --std-dir "$build/target/std" -I "$build/source/src/fort" \
    -o "$baseline_fort" "$build/target/entry/main.ft"
if ! file -b "$baseline_fort" | grep -F -q "$expected_format"; then
    echo "c bootstrap: the source baseline has the wrong format" >&2
    exit 1
fi

python3 "$root/tools/verify_seed.py" "$target_name" "$baseline_fort" \
    "$build/target/std" --ref "$root/tools/bootstrap.seed" >"$build/baseline.identity"
grep -F -x -q "source baseline SHA: $baseline" "$build/baseline.identity"
grep -F -x -q "default triple: $expected_triple" "$build/baseline.identity"

"$baseline_fort" -S --std-dir "$build/target/std" \
    -o "$build/baseline.ll" "$build/main.ft"
"$opt" -passes=verify -disable-output "$build/baseline.ll"

if [ "$target_name" = darwin ]; then
    if ! otool -hv "$build/c-program" | grep -Eq '(^|[[:space:]])PIE([[:space:]]|$)' ||
       ! otool -hv "$baseline_fort" | grep -Eq '(^|[[:space:]])PIE([[:space:]]|$)'; then
        echo "c bootstrap: a Darwin executable has no PIE flag" >&2
        exit 1
    fi
fi

echo "c bootstrap: $target_name C compiler and baseline pass; 0 unsupported bootstrap forms"
