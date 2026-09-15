#!/usr/bin/env bash
# Generate the Mac build target and fcntl C ABI module beside the compiler entry.
# The entry directory must be the first search root for platform.ft (D9.2).
# D14.1, D14.3
set -euo pipefail

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: gen_mac_platform.sh <build-dir> [host-version]" >&2
    exit 2
fi

source_dir=$(cd "$(dirname "$0")/.." && pwd)
output_dir=$1
# The VM can replace its fixed version for a build-refresh probe.
if [ "$#" -eq 2 ] && [ -n "${FORT_MAC_PLATFORM_TEST_VERSION:-}" ]; then
    version=$FORT_MAC_PLATFORM_TEST_VERSION
elif [ "$#" -eq 2 ]; then
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
if ! cmp -s "$source_dir/src/fort/main.ft" "$output_dir/main.ft"; then
    cp "$source_dir/src/fort/main.ft" "$output_dir/main.ft"
fi
platform_tmp=$(mktemp "$output_dir/platform.ft.XXXXXX")
trap 'rm -f "$platform_tmp"' EXIT
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
    'bool IS_MAC = true;' \
    '' \
    '/// Sets a file-descriptor flag through the Mac C variable tail.' \
    '/// D9.8, D14.3' \
    'extern fn fcntl(i32 fd, i32 cmd, ...) i32;' \
    '' \
    'fn fcntl_setfd(i32 fd, i32 cmd, i32 arg) i32 {' \
    '    return fcntl(fd, cmd, arg);' \
    '}' >"$platform_tmp"
if ! cmp -s "$platform_tmp" "$output_dir/platform.ft"; then
    chmod 644 "$platform_tmp"
    mv "$platform_tmp" "$output_dir/platform.ft"
fi
