#!/bin/bash
# Break CMake bootstrap validation and exercise the generated stage graph.
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: cmake_bootstrap_test.sh <external-stage1>" >&2
    exit 2
fi
external_stage1=$(cd "$(dirname "$1")" && pwd -P)/$(basename "$1")
root=$(cd "$(dirname "$0")/.." && pwd -P)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
git clone -q --shared "$root" "$work/source"
source=$work/source
bridge=$(awk '$1 == "bootstrap-0" { print $2 }' "$source/tools/bootstrap.ref")
head=$(git -C "$source" rev-parse HEAD)

configure() {
    local name=$1
    local ref=$2
    cmake -S "$source" -B "$work/$name" -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug -DFORT_BOOTSTRAP_REF:FILEPATH="$ref"
}

expect_fail() {
    local name=$1
    local text=$2
    local pattern=$3
    local ref=$work/$name.ref
    printf '%s\n' "$text" >"$ref"
    if configure "$name" "$ref" >"$work/$name.out" 2>"$work/$name.err"; then
        echo "bootstrap chain: $name was accepted" >&2
        exit 1
    fi
    grep -F -q "$pattern" "$work/$name.err"
}

if cmake -S "$source" -B "$work/missing" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug -DFORT_BOOTSTRAP_REF:FILEPATH="$work/no.ref" \
    >"$work/missing.out" 2>"$work/missing.err"; then
    echo "bootstrap chain: missing reference was accepted" >&2
    exit 1
fi
grep -F -q 'bootstrap reference is missing' "$work/missing.err"
expect_fail repeated "bootstrap-0 $bridge
bootstrap-0 $bridge" 'bootstrap-1 is expected'
expect_fail nonzero "bootstrap-1 $bridge" 'bootstrap-0 is expected'
expect_fail leading_zero "bootstrap-00 $bridge" 'bootstrap-0 is expected'
expect_fail gap "bootstrap-0 $bridge
bootstrap-2 $head" 'bootstrap-1 is expected'
expect_fail malformed "bootstrap-x $bridge" 'malformed line'
expect_fail short "bootstrap-0 ${bridge%????????}" 'needs one lowercase full SHA'
upper=$(printf '%s' "$bridge" | tr 'a-f' 'A-F')
expect_fail uppercase "bootstrap-0 $upper" 'needs one lowercase full SHA'
expect_fail no_commit 'bootstrap-0 0000000000000000000000000000000000000000' \
    'is no commit'
GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@example.invalid \
    git -C "$source" tag -a annotated-fixture -m 'annotated fixture' "$bridge"
tag_object=$(git -C "$source" rev-parse annotated-fixture^{tag})
expect_fail annotated_tag "bootstrap-0 $tag_object" 'is no commit object'
oracle_ref=$source/tools/bootstrap-oracle.ref
oracle_contents=$(cat "$oracle_ref")
printf 'bootstrap-0 %s\n' "$tag_object" >"$oracle_ref"
if configure oracle_annotated "$source/tools/bootstrap.ref" \
    >"$work/oracle_annotated.out" 2>"$work/oracle_annotated.err"; then
    echo "bootstrap chain: annotated oracle tag was accepted" >&2
    exit 1
fi
grep -F -q 'names no usable commit object' "$work/oracle_annotated.err"
printf '%s\n' "$oracle_contents" >"$oracle_ref"
unrelated=$(printf 'unrelated\n' | \
    GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@example.invalid \
    GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@example.invalid \
    git -C "$source" commit-tree "$(git -C "$source" rev-parse HEAD^{tree})")
expect_fail nonancestor "bootstrap-0 $unrelated" 'is not an ancestor of'
root_commit=$(git -C "$source" rev-list --max-parents=0 HEAD)
expect_fail missing_tree "bootstrap-0 $root_commit" \
    'lacks required src/fort or std sources'
expect_fail no_darwin \
    'bootstrap-0 3f8c732af6bd1b0abc9add38310739cd766dabd8' \
    'cannot assemble Darwin sources'

mixed_index=$work/mixed.index
GIT_INDEX_FILE=$mixed_index git -C "$source" read-tree "$bridge"
net_blob=$(git -C "$source" rev-parse "$bridge:std/darwin/net.ft")
GIT_INDEX_FILE=$mixed_index git -C "$source" update-index \
    --force-remove std/darwin/net.ft
GIT_INDEX_FILE=$mixed_index git -C "$source" update-index \
    --add --cacheinfo 100644 "$net_blob" std/mac/net.ft
mixed_tree=$(GIT_INDEX_FILE=$mixed_index git -C "$source" write-tree)
mixed=$(printf 'mixed Darwin standard pair\n' | \
    GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@example.invalid \
    GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@example.invalid \
    git -C "$source" commit-tree "$mixed_tree" -p "$bridge")
mixed_head=$(printf 'mixed-pair descendant\n' | \
    GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@example.invalid \
    GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@example.invalid \
    git -C "$source" commit-tree "$(git -C "$source" rev-parse "$head^{tree}")" \
        -p "$mixed")
git -C "$source" checkout -q --detach "$mixed_head"
expect_fail mixed_pair "bootstrap-0 $mixed" 'cannot assemble Darwin sources'
git -C "$source" checkout -q --detach "$head"

expect_fail backwards "bootstrap-0 $bridge
bootstrap-1 6a09cbade82b87a4c36c1f67a99bd18544cc004e" \
    'does not follow'

two=$work/two.ref
printf 'bootstrap-0 %s\nbootstrap-1 %s\n' "$bridge" "$head" >"$two"
configure graph "$two"
if grep -q -- ' --release ' "$work/graph/build.ninja"; then
    echo "bootstrap chain: Debug passes --release" >&2
    exit 1
fi
cmake --build "$work/graph" --target fort -v >"$work/graph.log" 2>&1
for binary in bootstrap/bootstrap-0/fort bootstrap/bootstrap-1/fort fort; do
    [ -x "$work/graph/$binary" ] || {
        echo "bootstrap chain: two-entry graph omitted $binary" >&2
        exit 1
    }
done
grep -F -q 'bootstrap/stage1/fort' "$work/graph.log"
grep -F -q 'bootstrap/bootstrap-0/fort' "$work/graph.log"
grep -F -q 'bootstrap/bootstrap-1/fort' "$work/graph.log"

printf 'bootstrap-0 %s\n' "$bridge" >"$two"
cmake --build "$work/graph" --target fort -v >"$work/reconfigure.log" 2>&1
if grep -q 'fort_bootstrap_1' "$work/graph/build.ninja"; then
    echo "bootstrap chain: a reference edit did not reconfigure CMake" >&2
    exit 1
fi

head_binary=$work/graph/bootstrap/head/fort
pin_binary=$work/graph/bootstrap/bootstrap-0/fort
head_before=$(python3 -c 'import os,sys; print(os.stat(sys.argv[1]).st_mtime_ns)' \
    "$head_binary")
pin_before=$(python3 -c 'import os,sys; print(os.stat(sys.argv[1]).st_mtime_ns)' \
    "$pin_binary")
touch "$source/src/fort/main.ft"
cmake --build "$work/graph" --target fort -v >"$work/head-edit.log" 2>&1
head_after=$(python3 -c 'import os,sys; print(os.stat(sys.argv[1]).st_mtime_ns)' \
    "$head_binary")
pin_after=$(python3 -c 'import os,sys; print(os.stat(sys.argv[1]).st_mtime_ns)' \
    "$pin_binary")
if [ "$head_after" -le "$head_before" ]; then
    echo "bootstrap chain: a HEAD edit did not rebuild HEAD" >&2
    exit 1
fi
if [ "$pin_after" -ne "$pin_before" ]; then
    echo "bootstrap chain: a HEAD edit rebuilt a pinned compiler" >&2
    exit 1
fi

for build_type in Release RelWithDebInfo MinSizeRel; do
    mode=$(printf '%s' "$build_type" | tr 'A-Z' 'a-z')
    cmake -S "$source" -B "$work/$mode" -G Ninja \
        -DCMAKE_BUILD_TYPE="$build_type" -DFORT_BOOTSTRAP_REF:FILEPATH="$two"
    count=$(grep -c -- ' --release ' "$work/$mode/build.ninja")
    if [ "$count" -lt 2 ]; then
        echo "bootstrap chain: $build_type does not pass --release through each stage" >&2
        exit 1
    fi
done

if cmake -S "$source" -B "$work/disabled" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DFORT_ENABLE_BOOTSTRAP=OFF >"$work/disabled.out" 2>&1; then
    echo "bootstrap chain: disabled bootstrap accepted no external stage1" >&2
    exit 1
fi
grep -F -q 'requires FORT_STAGE1_COMPILER' "$work/disabled.out"

cmake -S "$source" -B "$work/external" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DFORT_ENABLE_BOOTSTRAP=OFF \
    -DFORT_BOOTSTRAP_REF:FILEPATH="$work/no-external.ref" \
    -DFORT_STAGE1_COMPILER:FILEPATH="$external_stage1"
cmake --build "$work/external"
[ -x "$work/external/fort" ] || {
    echo "bootstrap chain: external stage1 omitted fort" >&2
    exit 1
}
[ -x "$work/external/fort-lsp" ] || {
    echo "bootstrap chain: external stage1 omitted fort-lsp" >&2
    exit 1
}
if find "$work/external/bootstrap" -maxdepth 1 -type d -name 'bootstrap-*' | grep -q .; then
    echo "bootstrap chain: external stage1 extracted a chain revision" >&2
    exit 1
fi

echo "bootstrap chain: 16 validation failures, 4 build types, and 2 stage1 modes passed"
