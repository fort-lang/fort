#!/bin/bash
# Break the target and file-format boundaries of the source bootstrap chain.
set -eu

if [ "$#" -ne 0 ]; then
    echo "usage: bootstrap_chain_test.sh" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd -P)
chain=$root/tools/bootstrap_chain.sh
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
work=$(cd "$work" && pwd -P)

fake_seed=$work/fort
cat >"$fake_seed" <<'EOF'
#!/bin/bash
set -eu
output=""
emit=no
while [ "$#" -gt 0 ]; do
    case "$1" in
    -S) emit=yes; shift ;;
    -o) output=$2; shift 2 ;;
    --std-dir | --cc | --target | -I) shift 2 ;;
    --release) shift ;;
    *) shift ;;
    esac
done
[ -n "$output" ] || exit 2
if [ -n "${FAKE_TRACE:-}" ]; then
    printf '%s\t%s\t%s\n' "$emit" "$0" "$output" >>"$FAKE_TRACE"
fi
mkdir -p "$(dirname "$output")"
if [ "$emit" = yes ]; then
    triple=$FAKE_TRIPLE
    case "$0" in
    */pin/* | */stage2/*) triple=${FAKE_STAGE_TRIPLE:-$FAKE_TRIPLE} ;;
    esac
    printf 'target triple = "%s"\n' "$triple" >"$output"
else
    cp "$0" "$output"
    chmod 700 "$output"
fi
EOF
chmod 700 "$fake_seed"

fake_opt=$work/opt
cat >"$fake_opt" <<'EOF'
#!/bin/bash
exit 0
EOF
chmod 700 "$fake_opt"

fake_file=$work/file
cat >"$fake_file" <<'EOF'
#!/bin/bash
printf '%s\n' "$FAKE_FORMAT"
EOF
chmod 700 "$fake_file"

checks=0
failures=0

expect_chain() {
    local name=$1
    local triple=$2
    local format=$3
    local build=$work/$name
    checks=$((checks + 1))
    if ! FAKE_TRIPLE=$triple FAKE_FORMAT=$format \
        bash "$chain" "$name" "$fake_seed" "$build" \
            --cc "$fake_seed" --opt "$fake_opt" --file "$fake_file" \
            >"$work/$name.out" 2>"$work/$name.err"; then
        echo "bootstrap_chain_test.sh: $name chain failed" >&2
        cat "$work/$name.err" >&2
        failures=$((failures + 1))
        return
    fi
    checks=$((checks + 1))
    if [ ! -x "$build/pin/0/fort" ] || [ ! -x "$build/stage2/fort" ]; then
        echo "bootstrap_chain_test.sh: $name chain omitted a compiler stage" >&2
        failures=$((failures + 1))
    fi
    checks=$((checks + 1))
    if [ "$(tail -n +2 "$build/bootstrap-stages.tsv" | wc -l | tr -d ' ')" -ne 2 ] ||
       ! awk -F '\t' -v triple="$triple" -v format="$format" \
           'NR > 1 && ($3 != triple || $4 != format) { bad = 1 } END { exit bad }' \
           "$build/bootstrap-stages.tsv"; then
        echo "bootstrap_chain_test.sh: $name stage record differs" >&2
        cat "$build/bootstrap-stages.tsv" >&2
        failures=$((failures + 1))
    fi
}

expect_chain linux x86_64-unknown-linux-gnu \
    'ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV)'
expect_chain darwin arm64-apple-macosx26.6.2 \
    'Mach-O 64-bit executable arm64'

# A custom two-pin ref proves that each source stage builds only the next one.
baseline=$(awk '/^baseline / { print $2 }' "$root/tools/bootstrap.seed")
head_sha=$(git -C "$root" rev-parse HEAD)
multi_ref=$work/multi.ref
printf 'bootstrap-0 %s\nbootstrap-1 %s\n' "$baseline" "$head_sha" >"$multi_ref"
multi_trace=$work/multi.trace
checks=$((checks + 1))
if ! FAKE_TRACE=$multi_trace FAKE_TRIPLE=x86_64-unknown-linux-gnu \
    FAKE_FORMAT='ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV)' \
    bash "$chain" linux "$fake_seed" "$work/multi" \
        --cc "$fake_seed" --opt "$fake_opt" --file "$fake_file" \
        --ref "$multi_ref" --seed-ref "$root/tools/bootstrap.seed" \
        >"$work/multi.out" 2>"$work/multi.err"; then
    echo "bootstrap_chain_test.sh: the two-pin chain failed" >&2
    cat "$work/multi.err" >&2
    failures=$((failures + 1))
elif [ ! -x "$work/multi/pin/1/fort" ] ||
     [ "$(tail -n +2 "$work/multi/bootstrap-stages.tsv" | wc -l | tr -d ' ')" -ne 3 ]; then
    echo "bootstrap_chain_test.sh: the two-pin chain skipped a stage" >&2
    failures=$((failures + 1))
fi

checks=$((checks + 1))
awk -F '\t' '$1 == "no" { print $2 "\t" $3 }' "$multi_trace" >"$work/compile.trace"
printf '%s\t%s\n%s\t%s\n%s\t%s\n' \
    "$work/multi/seed/fort" "$work/multi/pin/0/fort" \
    "$work/multi/pin/0/fort" "$work/multi/pin/1/fort" \
    "$work/multi/pin/1/fort" "$work/multi/stage2/fort" >"$work/expected.trace"
if ! cmp -s "$work/expected.trace" "$work/compile.trace"; then
    echo "bootstrap_chain_test.sh: a source stage skipped the next source stage" >&2
    diff -u "$work/expected.trace" "$work/compile.trace" >&2 || true
    failures=$((failures + 1))
fi

checks=$((checks + 1))
trace=$work/third.trace
status=0
FAKE_TRACE=$trace FAKE_TRIPLE=unused FAKE_FORMAT=unused \
    bash "$chain" freebsd "$fake_seed" "$work/third" \
    >"$work/third.out" 2>"$work/third.err" || status=$?
if [ "$status" -ne 2 ] || [ -e "$trace" ]; then
    echo "bootstrap_chain_test.sh: a third target ran the seed or did not exit 2" >&2
    failures=$((failures + 1))
fi

checks=$((checks + 1))
status=0
FAKE_TRIPLE=x86_64-unknown-linux-gnu \
FAKE_FORMAT='Mach-O 64-bit executable arm64' \
    bash "$chain" linux "$fake_seed" "$work/wrong-format" \
        --cc "$fake_seed" --opt "$fake_opt" --file "$fake_file" \
        >"$work/wrong-format.out" 2>"$work/wrong-format.err" || status=$?
if [ "$status" -ne 1 ] ||
   ! grep -q 'wrong file format' "$work/wrong-format.err" ||
   [ -e "$work/wrong-format/bootstrap-stages.tsv" ]; then
    echo "bootstrap_chain_test.sh: a darwin file entered the linux chain" >&2
    failures=$((failures + 1))
fi

checks=$((checks + 1))
status=0
FAKE_TRIPLE=x86_64-unknown-linux-gnu \
FAKE_STAGE_TRIPLE=arm64-apple-macosx26.6.2 \
FAKE_FORMAT='ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV)' \
    bash "$chain" linux "$fake_seed" "$work/wrong-triple" \
        --cc "$fake_seed" --opt "$fake_opt" --file "$fake_file" \
        >"$work/wrong-triple.out" 2>"$work/wrong-triple.err" || status=$?
if [ "$status" -ne 1 ] ||
   ! grep -q "bootstrap-0 has default triple 'arm64-apple-macosx26.6.2'" \
       "$work/wrong-triple.err" ||
   [ -e "$work/wrong-triple/bootstrap-stages.tsv" ]; then
    echo "bootstrap_chain_test.sh: a darwin triple entered the linux chain" >&2
    failures=$((failures + 1))
fi

checks=$((checks + 1))
status=0
FAKE_TRIPLE=arm64-apple-macosx26.6.2 \
FAKE_FORMAT='ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV)' \
    bash "$chain" darwin "$fake_seed" "$work/wrong-darwin-format" \
        --cc "$fake_seed" --opt "$fake_opt" --file "$fake_file" \
        >"$work/wrong-darwin-format.out" \
        2>"$work/wrong-darwin-format.err" || status=$?
if [ "$status" -ne 1 ] ||
   ! grep -q 'wrong file format' "$work/wrong-darwin-format.err" ||
   [ -e "$work/wrong-darwin-format/bootstrap-stages.tsv" ]; then
    echo "bootstrap_chain_test.sh: a linux file entered the darwin chain" >&2
    failures=$((failures + 1))
fi

checks=$((checks + 1))
if grep -n 'tools/vm' "$chain" "$root/tools/darwin" \
    "$root/test/darwin_fixpoint_test.sh" "$root/test/darwin_gate_test.sh" \
    "$root/test/darwin_core_test.sh" "$root/test/darwin_net_test.sh" \
    >"$work/vm-lines"; then
    echo "bootstrap_chain_test.sh: a darwin chain path calls tools/vm" >&2
    cat "$work/vm-lines" >&2
    failures=$((failures + 1))
fi

# The language target runs two differential tools that read oracle/std.
# This exact edge makes CMake extract that tree before it starts the target.
checks=$((checks + 1))
if ! grep -F -x -q \
    'add_dependencies(check-lang fort fort_std fort_stage2 fort_lsp fort_oracle_tree)' \
    "$root/CMakeLists.txt"; then
    echo "bootstrap_chain_test.sh: check-lang can start before fort_oracle_tree" >&2
    failures=$((failures + 1))
fi

if [ "$failures" -ne 0 ]; then
    echo "bootstrap_chain_test.sh: $failures of $checks checks failed" >&2
    exit 1
fi
echo "bootstrap_chain_test.sh: $checks checks passed"
