#!/bin/bash
# Check that each workflow passes its environment inputs to CMake.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd -P)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/bin"
trace=$work/trace
export TRACE=$trace

for tool in cmake ctest; do
    cat >"$work/bin/$tool" <<'EOF'
#!/bin/bash
printf '%s' "$(basename "$0")" >>"$TRACE"
printf '\t%s' "$@" >>"$TRACE"
printf '\n' >>"$TRACE"
EOF
    chmod 700 "$work/bin/$tool"
done

PATH="$work/bin:$PATH" \
FORT_BOOTSTRAP_SEED="$work/seed one" FORT_DARWIN_OPT="$work/opt one" \
    "$root/tools/target" darwin workflow
PATH="$work/bin:$PATH" \
FORT_BOOTSTRAP_SEED="$work/seed two" FORT_DARWIN_OPT="$work/opt two" \
    "$root/tools/target" darwin workflow
PATH="$work/bin:$PATH" \
FORT_BOOTSTRAP_SEED="$work/linux seed" FORT_DARWIN_OPT="$work/not-linux" \
    "$root/tools/target" linux workflow
env -u FORT_BOOTSTRAP_SEED -u FORT_DARWIN_OPT PATH="$work/bin:$PATH" \
    "$root/tools/target" linux workflow
PATH="$work/bin:$PATH" \
FORT_BOOTSTRAP_SEED="$work/gate seed" FORT_DARWIN_OPT="$work/gate opt" \
    "$root/tools/darwin" fixpoint

{
    printf 'cmake\t--preset\tdarwin\t-DFORT_BOOTSTRAP_SEED:FILEPATH=%s/seed one\t' "$work"
    printf '%s\n' "-DFORT_OPT:FILEPATH=$work/opt one"
    printf 'cmake\t--build\t--preset\tdarwin\nctest\t--preset\tdarwin\n'
    printf 'cmake\t--preset\tdarwin\t-DFORT_BOOTSTRAP_SEED:FILEPATH=%s/seed two\t' "$work"
    printf '%s\n' "-DFORT_OPT:FILEPATH=$work/opt two"
    printf 'cmake\t--build\t--preset\tdarwin\nctest\t--preset\tdarwin\n'
    printf 'cmake\t--preset\tlinux\t-DFORT_BOOTSTRAP_SEED:FILEPATH=%s/linux seed\n' "$work"
    printf 'cmake\t--build\t--preset\tlinux\nctest\t--preset\tlinux\n'
    printf 'cmake\t--preset\tlinux\n'
    printf 'cmake\t--build\t--preset\tlinux\nctest\t--preset\tlinux\n'
    printf 'cmake\t--preset\tdarwin\t-DFORT_BOOTSTRAP_SEED:FILEPATH=%s/gate seed\t' "$work"
    printf '%s\n' "-DFORT_OPT:FILEPATH=$work/gate opt"
    printf 'cmake\t--build\t--preset\tdarwin\nctest\t--preset\tdarwin\n'
} >"$work/want"
if ! cmp -s "$work/want" "$trace"; then
    echo "target_workflow_test.sh: workflow inputs differ" >&2
    diff -u "$work/want" "$trace" >&2 || true
    exit 1
fi
echo "target workflow: 5 calls refresh 4 seed values and 3 darwin verifier values"
