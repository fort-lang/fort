#!/bin/bash
# Check that the LSP build passes no --target: the compiler builds for its own target.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd -P)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
compiler=$work/fort
trace=$work/trace
cat >"$compiler" <<'FAKE'
#!/bin/bash
set -eu
printf '%s\n' "$@" >"$TRACE"
output=""
while [ "$#" -gt 0 ]; do
    case "$1" in
    -o) output=$2; shift 2 ;;
    *) shift ;;
    esac
done
[ -n "$output" ] || exit 71
printf '#!/bin/bash\nexit 0\n' >"$output"
chmod 700 "$output"
FAKE
chmod 700 "$compiler"

TRACE=$trace bash "$root/tools/build_lsp.sh" \
    "$compiler" "$work/std root" "$work/clang" \
    "$work/fort-lsp" "$work/main.ft" "$work/src" "$work/src/fort"
if [ ! -x "$work/fort-lsp" ] || grep -q -x -- '--target' "$trace"; then
    echo "lsp_target_test.sh: the LSP build passed --target" >&2
    exit 1
fi
grep -q -x -- "$work/std root" "$trace"
grep -q -x -- "$work/src/fort" "$trace"
echo "LSP target: the build passes the standard root and search roots, and no --target"
