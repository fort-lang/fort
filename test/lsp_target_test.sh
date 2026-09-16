#!/bin/bash
# Check that the LSP build uses the verified seed target.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd -P)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
compiler=$work/fort
trace=$work/trace
cat >"$compiler" <<'EOF'
#!/bin/bash
set -eu
printf '%s\n' "$@" >"$TRACE"
target=""
output=""
while [ "$#" -gt 0 ]; do
    case "$1" in
    --target) target=$2; shift 2 ;;
    -o) output=$2; shift 2 ;;
    *) shift ;;
    esac
done
[ "$target" = "$EXPECT_TARGET" ] || exit 70
[ -n "$output" ] || exit 71
printf '#!/bin/bash\nexit 0\n' >"$output"
chmod 700 "$output"
EOF
chmod 700 "$compiler"

target=arm64-apple-macosx15.0.0
printf 'default triple: %s\n' "$target" >"$work/seed.identity"
TRACE=$trace EXPECT_TARGET=$target bash "$root/tools/build_lsp.sh" \
    "$compiler" "$work/seed.identity" "$work/std root" "$work/clang" \
    "$work/fort-lsp" "$work/main.ft" "$work/src" "$work/src/fort"
if [ ! -x "$work/fort-lsp" ] ||
   [ "$(grep -A1 -x -- '--target' "$trace" | tail -n 1)" != "$target" ]; then
    echo "lsp_target_test.sh: LSP build did not use the seed target" >&2
    exit 1
fi

printf 'seed target: darwin\n' >"$work/seed.identity"
if TRACE=$trace EXPECT_TARGET=$target bash "$root/tools/build_lsp.sh" \
    "$compiler" "$work/seed.identity" "$work/std" "$work/clang" \
    "$work/bad-lsp" "$work/main.ft" "$work/src" 2>"$work/error"; then
    echo "lsp_target_test.sh: missing target was accepted" >&2
    exit 1
fi
grep -q 'must contain one default triple' "$work/error"
echo "LSP target: 1 alternate seed target passed; 1 missing target was rejected"
