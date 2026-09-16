#!/bin/bash
# Check that each linux gate configure refreshes the supplied seed.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd -P)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/bin" "$work/tools" "$work/test"
cp "$root/tools/target" "$work/tools/target"
cp "$root/test/linux_gate_test.sh" "$work/test/linux_gate_test.sh"
chmod 700 "$work/tools/target" "$work/test/linux_gate_test.sh"

cat >"$work/bin/uname" <<'EOF'
#!/bin/bash
echo Linux
EOF
cat >"$work/bin/cmake" <<'EOF'
#!/bin/bash
set -eu
printf 'cmake' >>"$TRACE"
printf '\t%s' "$@" >>"$TRACE"
printf '\n' >>"$TRACE"
if [ "${1:-}" = --preset ]; then
    preset=$2
    seed=""
    for argument in "$@"; do
        case "$argument" in
        -DFORT_BOOTSTRAP_SEED:FILEPATH=*) seed=${argument#*=} ;;
        esac
    done
    if [ -n "$seed" ]; then
        mkdir -p "build/$preset"
        printf 'FORT_BOOTSTRAP_SEED:FILEPATH=%s\n' "$seed" \
            >"build/$preset/CMakeCache.txt"
    fi
fi
mkdir -p build/debug
printf 'seed target: linux\n' >build/debug/seed.identity
EOF
chmod 700 "$work/bin/uname" "$work/bin/cmake"

for preset in debug asan ubsan; do
    mkdir -p "$work/build/$preset"
    printf 'FORT_BOOTSTRAP_SEED:FILEPATH=\n' >"$work/build/$preset/CMakeCache.txt"
done
trace=$work/trace
seed="$work/seed path/fort"
TRACE=$trace PATH="$work/bin:$PATH" FORT_BOOTSTRAP_SEED=$seed \
    "$work/tools/target" linux gate >"$work/gate.out"

configure_count=$(grep -F -x -c -- "-DFORT_BOOTSTRAP_SEED:FILEPATH=$seed" \
    <(tr '\t' '\n' <"$trace") || true)
if [ "$configure_count" -ne 3 ]; then
    echo "linux_gate_seed_test.sh: $configure_count of 3 configures received the seed" >&2
    exit 1
fi
for preset in debug asan ubsan; do
    if ! grep -F -x -q "FORT_BOOTSTRAP_SEED:FILEPATH=$seed" \
        "$work/build/$preset/CMakeCache.txt"; then
        echo "linux_gate_seed_test.sh: $preset kept its stale seed" >&2
        exit 1
    fi
done
echo "linux gate seed: 3 of 3 configures refreshed 3 stale caches"
