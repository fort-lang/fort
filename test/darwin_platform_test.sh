#!/usr/bin/env bash
# Check the generated darwin target, fcntl call form, and entry search root.
# This VM test checks source form. T-146 checks the host binary default.
# D14.1
set -euo pipefail

stage2=$1
generated=$2
fort_src=$3
std_dir=$4
source_dir=$(cd "$fort_src/../.." && pwd)
generator=$source_dir/tools/gen_darwin_platform.sh
scratch=$(mktemp -d "$PWD/darwin-platform-test.XXXXXX")
trap 'rm -rf "$scratch"' EXIT

grep -E -x -q 'string BUILT_TARGET = "arm64-apple-macosx[0-9]+\.[0-9]+\.[0-9]+";' \
    "$generated/platform.ft"
grep -F -x -q 'string DEFAULT_CC = "/usr/bin/clang";' "$generated/platform.ft"
grep -F -x -q 'bool IS_MAC = true;' "$generated/platform.ft"
grep -F -x -q 'extern fn fcntl(i32 fd, i32 cmd, ...) i32;' "$generated/platform.ft"
cmp "$generated/main.ft" "$fort_src/main.ft"
"$stage2" --check --std-dir "$std_dir" -I "$fort_src" "$generated/main.ft"
document=$("$stage2" --check --json --std-dir "$std_dir" -I "$fort_src" \
    "$generated/main.ft")
if ! grep -F -q "$generated/platform.ft" <<<"$document"; then
    echo 'darwin_platform_test.sh: did not load the generated platform' >&2
    exit 1
fi
if grep -F -q "$fort_src/platform.ft" <<<"$document"; then
    echo 'darwin_platform_test.sh: loaded the linux platform' >&2
    exit 1
fi

bash "$generator" "$scratch/two" 15.0
grep -F -x -q 'string BUILT_TARGET = "arm64-apple-macosx15.0.0";' \
    "$scratch/two/platform.ft"
"$stage2" --check --std-dir "$std_dir" -I "$fort_src" "$scratch/two/main.ft"

bash "$generator" "$scratch/three" 26.6.2
grep -F -x -q 'string BUILT_TARGET = "arm64-apple-macosx26.6.2";' \
   "$scratch/three/platform.ft"
"$stage2" --check --std-dir "$std_dir" -I "$fort_src" "$scratch/three/main.ft"

# A source-chain pin supplies its archived entry as the third argument. The
# explicit version must still win over the host version in this form.
printf 'import driver;\nimport std.sys;\nfn main(string@ args) i32 { return driver.run(args); }\n' \
    >"$scratch/pin-main.ft"
bash "$generator" "$scratch/pin" 15.2.7 "$scratch/pin-main.ft"
cmp "$scratch/pin-main.ft" "$scratch/pin/main.ft"
grep -F -x -q 'string BUILT_TARGET = "arm64-apple-macosx15.2.7";' \
    "$scratch/pin/platform.ft"

for version in 15 15.a 15.0.1.2; do
    if bash "$generator" "$scratch/invalid-$version" "$version" \
        >"$scratch/invalid.out" 2>"$scratch/invalid.err"; then
        echo "darwin_platform_test.sh: accepted host version '$version'" >&2
        exit 1
    fi
    if [ -e "$scratch/invalid-$version/platform.ft" ]; then
        echo "darwin_platform_test.sh: created a source for '$version'" >&2
        exit 1
    fi
done

echo 'darwin platform source: 3 versions stored, 1 archived entry copied, 3 forms rejected'
