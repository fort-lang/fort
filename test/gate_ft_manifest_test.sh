#!/bin/bash
# Prove the source manifest detects ignored fort files and symlinks.
set -euo pipefail

source_root=$(cd "$(dirname "$0")/.." && pwd -P)
fixture=$(mktemp -d)
trap 'rm -rf -- "$fixture"' EXIT
mkdir -p "$fixture/.user-originals" "$fixture/build"
cp "$source_root/test/lang/programs/bignum.ft" "$fixture/base.ft"

before=$(bash "$source_root/tools/gate_ft_manifest.sh" "$fixture")
cp "$source_root/test/lang/programs/bignum.ft" "$fixture/build/generated.ft"
after_build=$(bash "$source_root/tools/gate_ft_manifest.sh" "$fixture")
test "$before" = "$after_build"

cp "$source_root/test/lang/programs/bignum.ft" "$fixture/.user-originals/ignored.ft"
after_add=$(bash "$source_root/tools/gate_ft_manifest.sh" "$fixture")
test "$before" != "$after_add"

mv "$fixture/.user-originals/ignored.ft" "$fixture/.user-originals/renamed.ft"
after_move=$(bash "$source_root/tools/gate_ft_manifest.sh" "$fixture")
test "$after_add" != "$after_move"

cp "$source_root/test/lang/programs/float_stats.ft" "$fixture/.user-originals/renamed.ft"
after_content=$(bash "$source_root/tools/gate_ft_manifest.sh" "$fixture")
test "$after_move" != "$after_content"

cp "$fixture/base.ft" "$fixture/.user-originals/alternate.txt"
ln -s ../base.ft "$fixture/.user-originals/link.ft"
after_link=$(bash "$source_root/tools/gate_ft_manifest.sh" "$fixture")
test "$after_content" != "$after_link"

cmp "$fixture/base.ft" "$fixture/.user-originals/alternate.txt"
# `ln -sf` replaces the link in place on GNU and BSD alike; `mv -T` is GNU only.
ln -sf alternate.txt "$fixture/.user-originals/link.ft"
after_retarget=$(bash "$source_root/tools/gate_ft_manifest.sh" "$fixture")
test "$after_link" != "$after_retarget"
