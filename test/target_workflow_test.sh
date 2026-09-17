#!/bin/bash
# Check host selection and the Darwin verifier input of each workflow.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd -P)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/bin"
trace=$work/trace
export TRACE=$trace

for tool in cmake ctest; do
    printf '#!/bin/bash\nprintf "%%s" "$(basename "$0")" >>"$TRACE"\nprintf "\\t%%s" "$@" >>"$TRACE"\nprintf "\\n" >>"$TRACE"\n' \
        >"$work/bin/$tool"
    chmod 700 "$work/bin/$tool"
done
cat >"$work/bin/uname" <<'EOF'
#!/bin/bash
printf '%s\n' "$FAKE_HOST"
EOF
chmod 700 "$work/bin/uname"

FAKE_HOST=Darwin FORT_DARWIN_OPT="$work/opt one" \
    PATH="$work/bin:$PATH" "$root/tools/target" darwin workflow
FAKE_HOST=Darwin PATH="$work/bin:$PATH" \
    "$root/tools/target" darwin workflow
FAKE_HOST=Linux PATH="$work/bin:$PATH" \
    "$root/tools/target" linux workflow
if FAKE_HOST=Darwin PATH="$work/bin:$PATH" \
    "$root/tools/target" linux workflow >/dev/null 2>&1; then
    echo "target workflow: accepted linux on Darwin" >&2
    exit 1
fi
if FAKE_HOST=Linux PATH="$work/bin:$PATH" \
    "$root/tools/target" darwin workflow >/dev/null 2>&1; then
    echo "target workflow: accepted Darwin on Linux" >&2
    exit 1
fi

{
    printf 'cmake\t--preset\tdarwin\t%s\n' "-DFORT_OPT:FILEPATH=$work/opt one"
    printf 'cmake\t--build\t--preset\tdarwin\nctest\t--preset\tdarwin\n'
    printf 'cmake\t--preset\tdarwin\n'
    printf 'cmake\t--build\t--preset\tdarwin\nctest\t--preset\tdarwin\n'
    printf 'cmake\t--preset\tlinux\n'
    printf 'cmake\t--build\t--preset\tlinux\nctest\t--preset\tlinux\n'
} >"$work/want"
if ! cmp -s "$work/want" "$trace"; then
    echo "target_workflow_test.sh: workflow inputs differ" >&2
    diff -u "$work/want" "$trace" >&2 || true
    exit 1
fi
echo "target workflow: 3 native calls passed; 2 non-native calls failed"
