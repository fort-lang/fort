#!/bin/bash
# tools/pin.sh: the pinned commits that build the compiler (notes/compiler.md 8).
#
# The compiler is not built by the C bootstrap any more. `tools/bootstrap.ref`
# names a chain of commits of this repository, oldest first; stage1 (C) builds
# pin 0's `src/fort` with pin 0's `std`, pin k builds pin k+1, and the last pin
# builds HEAD. This script reads that file. It has three jobs:
#
#   verify   every pin exists, is written in full and stands in the history
#   extract  one pin's `src/fort` and `std` into a directory of the build
#   last     the number of the last pin, for a script that needs its path
#
# It runs on the host and in the guest, from the main checkout and from a
# worktree: the guest reaches the object store through the symlink
# tools/provision.sh makes (notes/environment.md 1), and a worktree shares the
# object store of the checkout that made it. A shallow clone has no pin and
# cannot build.
set -eu

usage() {
    cat <<'EOF' >&2
usage: pin.sh verify [--ref <file>] [--tip <rev>]
       pin.sh extract <sha> <dir> [--ref <file>]
       pin.sh last [--ref <file>]
EOF
}

die() {
    echo "pin.sh: $*" >&2
    exit 1
}

root=$(cd "$(dirname "$0")/.." && pwd)
ref=$root/tools/bootstrap.ref
# The revision every pin must be an ancestor of. A pin is a commit of this
# repository's history and not a commit of some other tree, so the build must
# be able to reach it by walking back from what it builds.
tip=HEAD

# The line shape: `bootstrap-<n> <40-hex sha>`. The sha is the fact and the
# name is a name, so the name is checked for shape alone.
LINE_RE='^bootstrap-([0-9]+)[[:space:]]+([0-9a-f]{40})$'

# read_ref -- the pin lines of $ref, in file order, comments and blanks gone.
read_ref() {
    if [ ! -f "$ref" ]; then
        die "no ref file: $ref"
    fi
    grep -v '^[[:space:]]*#' "$ref" | grep -v '^[[:space:]]*$' || true
}

# verify -- every rule the ref file must obey, each failure naming its pin.
verify() {
    local lines
    lines=$(read_ref)
    if [ -z "$lines" ]; then
        die "$ref names no pin; the chain needs at least pin 0"
    fi
    local expected=0
    local prev=""
    local line name sha
    while IFS= read -r line; do
        if ! [[ $line =~ $LINE_RE ]]; then
            die "$ref: malformed line '$line'; expected 'bootstrap-<n> <40-hex sha>'"
        fi
        name=bootstrap-${BASH_REMATCH[1]}
        sha=${BASH_REMATCH[2]}
        if [ "${BASH_REMATCH[1]}" != "$expected" ]; then
            die "$ref: $name stands where bootstrap-$expected is expected;" \
                "the pins are numbered from 0 with no gap"
        fi
        # The object must exist and must be a commit. A hand-edited digit
        # usually names no object at all, and this is where that is caught.
        if ! git -C "$root" cat-file -e "$sha^{commit}" 2>/dev/null; then
            die "$name: $sha is no commit of this repository;" \
                "a full clone holds every pin (notes/compiler.md 8)"
        fi
        # And the line must hold the whole sha. `rev-parse` resolves an
        # abbreviation, so without this an abbreviated pin would verify and
        # then name a different commit after the next merge.
        if [ "$(git -C "$root" rev-parse --verify --quiet "$sha^{commit}")" != "$sha" ]; then
            die "$name: $sha is not the full 40-hex sha of the commit it names"
        fi
        if ! git -C "$root" merge-base --is-ancestor "$sha" "$tip"; then
            die "$name: $sha is not an ancestor of $tip;" \
                "a pin is a commit of this repository's history"
        fi
        if [ -n "$prev" ] && ! git -C "$root" merge-base --is-ancestor "$prev" "$sha"; then
            die "$name: $sha does not follow bootstrap-$((expected - 1)) ($prev) in the history"
        fi
        echo "$name: $sha is a commit of the history of $tip"
        prev=$sha
        expected=$((expected + 1))
    done <<EOF
$lines
EOF
}

# extract <sha> <dir> -- the pin's compiler and library, and then its stamp.
#
# The stamp is written last and is named by the sha, so a half-written tree
# leaves no stamp and the build extracts it again. The tree of a commit never
# changes, so a stamp that stands is a tree that is right.
extract() {
    local sha=$1
    local dir=$2
    local lines name found archive
    lines=$(read_ref)
    found=""
    while IFS= read -r line; do
        if [[ $line =~ $LINE_RE ]] && [ "${BASH_REMATCH[2]}" = "$sha" ]; then
            found=bootstrap-${BASH_REMATCH[1]}
        fi
    done <<EOF
$lines
EOF
    if [ -z "$found" ]; then
        die "$sha is named by no line of $ref"
    fi
    name=$found
    rm -rf "$dir"
    mkdir -p "$dir"
    # The archive goes to a file and not through a pipe: the status of a
    # pipeline is the last command's, so a `git archive` that failed would
    # otherwise reach tar as an empty stream and read as an empty tree.
    archive=$(mktemp)
    if ! git -C "$root" archive --format=tar "$sha" src/fort std >"$archive"; then
        rm -f "$archive"
        die "$name: git archive $sha src/fort std failed"
    fi
    tar -x -C "$dir" -f "$archive"
    rm -f "$archive"
    if [ ! -f "$dir/src/fort/main.ft" ]; then
        die "$name: $dir/src/fort/main.ft is missing from the extracted tree"
    fi
    if [ ! -f "$dir/std/rt.ft" ]; then
        die "$name: $dir/std/rt.ft is missing from the extracted tree"
    fi
    : >"$dir/.tree-$sha"
}

# last -- the number of the last pin, which is the compiler that builds HEAD.
last() {
    local lines line number=""
    lines=$(read_ref)
    while IFS= read -r line; do
        if [[ $line =~ $LINE_RE ]]; then
            number=${BASH_REMATCH[1]}
        fi
    done <<EOF
$lines
EOF
    if [ -z "$number" ]; then
        die "$ref names no pin"
    fi
    echo "$number"
}

if [ "$#" -lt 1 ]; then
    usage
    exit 2
fi
command=$1
shift

# At most two positional arguments (extract's sha and directory). They are
# kept in two variables and not in an array: `set -u` and an empty array are
# an error in the bash 3.2 a macOS host still ships.
pos1=""
pos2=""
npos=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --ref)
            if [ "$#" -lt 2 ]; then
                echo "pin.sh: --ref needs an argument" >&2
                usage
                exit 2
            fi
            ref=$2
            shift 2
            ;;
        --tip)
            if [ "$#" -lt 2 ]; then
                echo "pin.sh: --tip needs an argument" >&2
                usage
                exit 2
            fi
            tip=$2
            shift 2
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        -*)
            echo "pin.sh: unknown argument '$1'" >&2
            usage
            exit 2
            ;;
        *)
            npos=$((npos + 1))
            if [ "$npos" -eq 1 ]; then
                pos1=$1
            elif [ "$npos" -eq 2 ]; then
                pos2=$1
            fi
            shift
            ;;
    esac
done

case "$command" in
    verify)
        if [ "$npos" -ne 0 ]; then
            echo "pin.sh: verify takes no positional argument" >&2
            usage
            exit 2
        fi
        verify
        ;;
    extract)
        if [ "$npos" -ne 2 ]; then
            echo "pin.sh: extract takes <sha> and <dir>" >&2
            usage
            exit 2
        fi
        extract "$pos1" "$pos2"
        ;;
    last)
        if [ "$npos" -ne 0 ]; then
            echo "pin.sh: last takes no positional argument" >&2
            usage
            exit 2
        fi
        last
        ;;
    *)
        echo "pin.sh: unknown command '$command'" >&2
        usage
        exit 2
        ;;
esac
