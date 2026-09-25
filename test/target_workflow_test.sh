#!/bin/bash
# Check host selection and the inputs of each workflow and gate.
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
# The darwin gate ends with the host identity record; the trace stands in
# for it, since the record needs a clean tree and a built compiler.
mkdir -p "$work/tools"
cp "$root/tools/target" "$work/tools/target"
printf '#!/bin/bash\nprintf "identity\\n" >>"$TRACE"\n' >"$work/tools/darwin_host_identity.sh"

FAKE_HOST=Darwin PATH="$work/bin:$PATH" \
    "$root/tools/target" darwin workflow
FAKE_HOST=Linux PATH="$work/bin:$PATH" \
    "$root/tools/target" linux workflow
FAKE_HOST=Darwin PATH="$work/bin:$PATH" \
    "$work/tools/target" darwin gate >/dev/null
FAKE_HOST=Linux PATH="$work/bin:$PATH" \
    "$work/tools/target" linux gate >/dev/null
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

# gate_trace <first preset> <presets...>: the cmake calls of one gate.
gate_trace() {
    local first=$1
    shift
    printf 'cmake\t--preset\t%s\n' "$first"
    printf 'cmake\t--build\t--preset\t%s\t--target\tformat-check\n' "$first"
    printf 'cmake\t--build\t--preset\t%s\t--target\ttidy\n' "$first"
    for preset in "$first" "$@"; do
        if [ "$preset" != "$first" ]; then
            printf 'cmake\t--preset\t%s\n' "$preset"
        fi
        printf 'cmake\t--build\t--preset\t%s\n' "$preset"
        printf 'cmake\t--build\t--preset\t%s\t--target\tcheck-all\n' "$preset"
    done
}
{
    printf 'cmake\t--preset\tdarwin\n'
    printf 'cmake\t--build\t--preset\tdarwin\nctest\t--preset\tdarwin\n'
    printf 'cmake\t--preset\tlinux\n'
    printf 'cmake\t--build\t--preset\tlinux\nctest\t--preset\tlinux\n'
    gate_trace darwin darwin-asan darwin-ubsan
    printf 'identity\n'
    gate_trace debug asan ubsan
} >"$work/want"
if ! cmp -s "$work/want" "$trace"; then
    echo "target_workflow_test.sh: workflow inputs differ" >&2
    diff -u "$work/want" "$trace" >&2 || true
    exit 1
fi
echo "target workflow: 2 workflows and 2 gates traced; 3 non-native calls failed"
