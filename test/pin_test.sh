#!/bin/bash
# tools/pin.sh reads the chain of pinned commits that builds the compiler
# (notes/compiler.md 8). A check that cannot fail is worse than no check, so
# every rule it states is broken here on a ref file of this test's own and the
# script must exit 1 and name the pin.
#
# The repository's own tools/bootstrap.ref is verified as well, which is what
# makes this the ctest `pin-verify`: a pin that left the history, or a sha a
# hand edited, fails here and at configure time.
#
# Usage: pin_test.sh, from the top of the worktree.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
pin=$root/tools/pin.sh
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

failures=0
checks=0

head_sha=$(git -C "$root" rev-parse HEAD)
pin0=$(grep -E '^bootstrap-0 ' "$root/tools/bootstrap.ref" | awk '{print $2}')
# The repository's first commit: a commit that exists, that is an ancestor of
# everything, and that is never a pin. It stands for "some other commit" in the
# cases below, so they hold whatever HEAD is when the suite runs.
root_commit=$(git -C "$root" rev-list --max-parents=0 HEAD | tail -n 1)

# expect_ok <name> <args...> -- the command must exit 0.
expect_ok() {
    local name=$1
    shift
    checks=$((checks + 1))
    if "$@" >"$work/out" 2>"$work/err"; then
        return 0
    fi
    echo "pin_test.sh: $name: expected exit 0, got $?" >&2
    cat "$work/err" >&2
    failures=$((failures + 1))
}

# expect_fail <name> <text> <args...> -- the command must exit 1 and its
# stderr must hold <text>. The text is what names the pin, so a rule that
# fired for the wrong reason is a failure here too.
expect_fail() {
    local name=$1
    local text=$2
    shift 2
    local status=0
    checks=$((checks + 1))
    "$@" >"$work/out" 2>"$work/err" || status=$?
    if [ "$status" -ne 1 ]; then
        echo "pin_test.sh: $name: expected exit 1, got $status" >&2
        cat "$work/err" >&2
        failures=$((failures + 1))
        return 0
    fi
    if ! grep -qF "$text" "$work/err"; then
        echo "pin_test.sh: $name: stderr does not hold '$text':" >&2
        cat "$work/err" >&2
        failures=$((failures + 1))
    fi
}

# ref <name> <line...> -- a ref file of this test's own, in the work directory.
ref() {
    local name=$1
    shift
    local path=$work/$name.ref
    printf '# a ref file of test/pin_test.sh\n' >"$path"
    local line
    for line in "$@"; do
        printf '%s\n' "$line" >>"$path"
    done
    echo "$path"
}

# The repository's own chain, against HEAD.
expect_ok "the repository's ref file" bash "$pin" verify

# The shape of a line.
expect_fail "a malformed line" "malformed line" \
    bash "$pin" verify --ref "$(ref malformed "bootstrap-0")"
expect_fail "a sha that is not 40 hex" "malformed line" \
    bash "$pin" verify --ref "$(ref short "bootstrap-0 d132989")"
expect_fail "an upper-case sha" "malformed line" \
    bash "$pin" verify --ref "$(ref upper "bootstrap-0 $(echo "$pin0" | tr 'a-f' 'A-F')")"

# An empty chain.
expect_fail "a ref file with no pin" "names no pin" \
    bash "$pin" verify --ref "$(ref empty)"
expect_fail "last over a ref file with no pin" "names no pin" \
    bash "$pin" last --ref "$(ref empty2)"
expect_fail "a missing ref file" "no ref file" \
    bash "$pin" verify --ref "$work/absent.ref"

# The numbering: from 0, with no gap.
expect_fail "a chain that starts at 1" "bootstrap-0 is expected" \
    bash "$pin" verify --ref "$(ref one "bootstrap-1 $pin0")"
expect_fail "a chain with a gap" "bootstrap-1 is expected" \
    bash "$pin" verify --ref "$(ref gap "bootstrap-0 $pin0" "bootstrap-2 $head_sha")"

# A hand-edited sha. The last digit of pin 0 is turned into a different one,
# which is the edit the ticket demands be shown to fire.
edited=${pin0%?}
case ${pin0: -1} in
    0) edited=${edited}1 ;;
    *) edited=${edited}0 ;;
esac
expect_fail "a hand-edited sha" "bootstrap-0: $edited is no commit" \
    bash "$pin" verify --ref "$(ref edited "bootstrap-0 $edited")"

# A commit that exists and is not an ancestor of the revision being built.
# --tip names that revision, so pin 0 held against its own parent breaks the
# rule with no object written into the repository.
expect_fail "a pin outside the history" "is not an ancestor of" \
    bash "$pin" verify --ref "$(ref outside "bootstrap-0 $pin0")" --tip "$pin0~1"

# The chain must run forwards. pin 0 at HEAD and pin 1 at the first commit is
# a chain whose second pin is older than its first.
expect_fail "a chain that runs backwards" "does not follow bootstrap-0" \
    bash "$pin" verify --ref "$(ref backwards "bootstrap-0 $head_sha" "bootstrap-1 $root_commit")"

# extract writes the two trees and stamps them, and it stamps nothing else.
tree=$work/tree
expect_ok "extract" bash "$pin" extract "$pin0" "$tree"
checks=$((checks + 1))
for wanted in "$tree/src/fort/main.ft" "$tree/std/rt.ft" "$tree/.tree-$pin0"; do
    if [ ! -f "$wanted" ]; then
        echo "pin_test.sh: extract: $wanted is missing" >&2
        failures=$((failures + 1))
    fi
done
checks=$((checks + 1))
if [ -d "$tree/src/bootstrap" ] || [ -d "$tree/test" ]; then
    echo "pin_test.sh: extract: the tree holds more than src/fort and std" >&2
    failures=$((failures + 1))
fi
# The tree of a commit never changes, so the stamp is the whole cache key. It
# must be written after the tree, which is what a second extract shows: the
# stamp is newer than nothing else if the tree was not written again.
expect_fail "extract of a sha no line names" "is named by no line" \
    bash "$pin" extract "$root_commit" "$work/other" --ref "$(ref lonely "bootstrap-0 $pin0")"
checks=$((checks + 1))
if [ -e "$work/other/.tree-$root_commit" ]; then
    echo "pin_test.sh: extract: a refused extraction left a stamp" >&2
    failures=$((failures + 1))
fi

# The usage errors exit 2, as every other script of tools/ does.
for bad in "" "nonsense" "verify extra" "extract only-one"; do
    status=0
    checks=$((checks + 1))
    # shellcheck disable=SC2086
    bash "$pin" $bad >"$work/out" 2>"$work/err" || status=$?
    if [ "$status" -ne 2 ]; then
        echo "pin_test.sh: '$bad': expected exit 2, got $status" >&2
        failures=$((failures + 1))
    fi
done

if [ "$failures" -ne 0 ]; then
    echo "pin_test.sh: $failures of $checks checks failed" >&2
    exit 1
fi
echo "pin_test.sh: $checks checks passed"
