#!/usr/bin/env bash
# Generate the Mac build target module beside a copy of the compiler entry.
# The entry directory must be the first search root for platform.ft (D9.2).
# D14.1, D14.3
set -euo pipefail

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: gen_mac_platform.sh <build-dir> [host-version]" >&2
    exit 2
fi

source_dir=$(cd "$(dirname "$0")/.." && pwd)
output_dir=$1
if [ "$#" -eq 2 ]; then
    version=$2
else
    version=$(sw_vers -productVersion)
fi

if [[ "$version" =~ ^[0-9]+\.[0-9]+$ ]]; then
    version=$version.0
elif [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "gen_mac_platform.sh: unsupported host version '$version'" >&2
    exit 2
fi

mkdir -p "$output_dir"
cp "$source_dir/src/fort/main.ft" "$output_dir/main.ft"
printf '%s\n' \
    '// platform: the Mac build target of the self-hosted compiler.' \
    '//' \
    '// The build stores the numeric host version in this module.' \
    '// The entry file beside this module makes its directory the first search root.' \
    '// D14.1, D14.3' \
    '' \
    '/// The target that builds this compiler binary.' \
    '/// D14.1' \
    "string BUILT_TARGET = \"arm64-apple-macosx$version\";" \
    '' \
    '/// The clang program this compiler uses by default.' \
    '/// D14.3' \
    'string DEFAULT_CC = "/usr/bin/clang";' \
    '' \
    '/// Whether this compiler uses the Mac link line.' \
    '/// D14.3' \
    'bool IS_MAC = true;' >"$output_dir/platform.ft"
