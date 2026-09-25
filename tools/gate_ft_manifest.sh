#!/bin/bash
# Hash fort source paths, symlink targets, and contents outside build.
set -euo pipefail

scan_root=${1:-$(cd "$(dirname "$0")/.." && pwd -P)}
cd "$scan_root"

find . -path ./build -prune -o -path ./.git -prune -o -path ./.worktrees -prune -o \
    -name '*.ft' \( -type f -o -type l \) -print0 |
    LC_ALL=C sort -z |
    while IFS= read -r -d '' source_path; do
        if [ -L "$source_path" ]; then
            # GNU readlink has -z; BSD readlink does not. Print the NUL here.
            printf '%s\0' "$(readlink -- "$source_path")"
        fi
        sha256sum -- "$source_path"
    done |
    sha256sum
