#!/bin/bash
# Check host selection and the cmake calls of each workflow and gate.
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
FAKE_HOST=Darwin PATH="$work/bin:$PATH" \
    "$root/tools/target" darwin workflow
FAKE_HOST=Linux PATH="$work/bin:$PATH" \
    "$root/tools/target" linux workflow
FAKE_HOST=Darwin PATH="$work/bin:$PATH" \
    "$root/tools/target" darwin gate >/dev/null
FAKE_HOST=Linux PATH="$work/bin:$PATH" \
    "$root/tools/target" linux gate >/dev/null
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
if FAKE_HOST=Linux PATH="$work/bin:$PATH" \
    "$root/tools/target" darwin gate >/dev/null 2>&1; then
    echo "target workflow: accepted a Darwin gate on Linux" >&2
    exit 1
fi

# The gate is the `gate` target of the base preset: it configures that preset
# and builds the target. The sanitizer workflows and the identity record run
# inside the target, so they leave no trace here.
{
    printf 'cmake\t--workflow\t--preset\tdarwin\n'
    printf 'cmake\t--workflow\t--preset\tdebug\n'
    printf 'cmake\t--preset\tdarwin\n'
    printf 'cmake\t--build\t--preset\tdarwin\t--target\tgate\n'
    printf 'cmake\t--preset\tdebug\n'
    printf 'cmake\t--build\t--preset\tdebug\t--target\tgate\n'
} >"$work/want"
if ! cmp -s "$work/want" "$trace"; then
    echo "target_workflow_test.sh: workflow inputs differ" >&2
    diff -u "$work/want" "$trace" >&2 || true
    exit 1
fi
echo "target workflow: 2 workflows and 2 gates traced; 3 non-native calls failed"
