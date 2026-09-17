#!/bin/bash
# The source compiler owns compile-time configuration. The C bootstrap stays frozen.
# D14.1, D21.1
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: stage2_cfg_test.sh <stage2> <std-dir>" >&2
    exit 2
fi

fort=$1
std=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

printf 'string VALUE = $cfg(value);\n' >"$work/value.ft"

expect_usage() {
    local want=$1
    shift
    local status=0
    "$fort" "$@" "$work/value.ft" >"$work/out" 2>"$work/err" || status=$?
    if [ "$status" -ne 2 ] || ! grep -Fq "$want" "$work/err"; then
        echo "stage2_cfg_test.sh: expected usage error: $want" >&2
        cat "$work/err" >&2
        exit 1
    fi
}

# Four malformed or duplicate forms are four usage cases.
expect_usage "invalid --cfg assignment 'missing'" --cfg missing
expect_usage "invalid --cfg key 'bad-key'" --cfg bad-key=value
expect_usage "empty --cfg list entry" --cfg a=b,,c=d
expect_usage "duplicate --cfg key 'same'" --cfg same=one --cfg same=one

# The compiler owns all three target keys.
for key in target_os target_arch target_abi; do
    expect_usage "cannot override compiler configuration key '$key'" --cfg "$key=value"
done

# Three user pairs across two options reach the entry module and one import.
mkdir "$work/modules"
printf '%s\n' \
    'import dep.SHARED;' \
    'string LOCAL = $cfg(shared);' \
    'string EMPTY = $cfg(empty);' \
    'string THIRD = $cfg(third);' \
    'fn main() i32 {' \
    '    println(LOCAL, " ", SHARED, " ", EMPTY.len, " ", THIRD);' \
    '    return 0;' \
    '}' \
    >"$work/modules/main.ft"
printf '%s\n' 'string SHARED = $cfg(shared);' >"$work/modules/dep.ft"
"$fort" --std-dir "$std" --cfg shared=hello,empty= --cfg third=three=parts \
    -o "$work/program" "$work/modules/main.ft"
if [ "$("$work/program")" != "hello hello 0 three=parts" ]; then
    echo "stage2_cfg_test.sh: the module closure did not share user configuration" >&2
    exit 1
fi

# Both target forms supply the three documented values. A wrong value makes an
# array length zero and turns the check red.
printf '%s\n' \
    'string WANT_OS = $cfg(want_os);' \
    'string WANT_ARCH = $cfg(want_arch);' \
    'string WANT_ABI = $cfg(want_abi);' \
    'i32[($cfg(target_os) == WANT_OS ? 1 : 0)] OS = {0};' \
    'i32[($cfg(target_arch) == WANT_ARCH ? 1 : 0)] ARCH = {0};' \
    'i32[($cfg(target_abi) == WANT_ABI ? 1 : 0)] ABI = {0};' \
    >"$work/target.ft"

"$fort" --check --std-dir "$std" --target x86_64-linux-gnu \
    --cfg want_os=linux,want_arch=x86_64,want_abi= "$work/target.ft"
"$fort" --check --std-dir "$std" --target arm64-apple-macosx15.0.0 \
    --cfg want_os=macos,want_arch=aarch64,want_abi= "$work/target.ft"

# These modes preserve the source form and do not read the configuration map.
tokens=$("$fort" --tokens "$work/value.ft")
ast=$("$fort" --ast "$work/value.ft")
if ! grep -Fq '0 "$cfg" $cfg' <<<"$tokens"; then
    echo 'stage2_cfg_test.sh: --tokens omitted the $cfg token' >&2
    exit 1
fi
if [ "$ast" != '(module (var VALUE (type (string)) (cfg value)))' ]; then
    echo 'stage2_cfg_test.sh: --ast did not preserve $cfg(value)' >&2
    echo "$ast" >&2
    exit 1
fi

# A near match is an error at `$`. It must not split into `$cfg` and an identifier.
for spelling in '$' '$c' '$cf' '$cfgx'; do
    printf '%s\n' "$spelling" >"$work/boundary.ft"
    status=0
    "$fort" --tokens "$work/boundary.ft" >"$work/out" 2>"$work/err" || status=$?
    want="$work/boundary.ft:1:1: error: unexpected character '$'"
    if [ "$status" -ne 1 ] || [ "$(grep -Fxc "$want" "$work/err")" -ne 1 ]; then
        echo "stage2_cfg_test.sh: expected a boundary error for $spelling" >&2
        cat "$work/err" >&2
        exit 1
    fi
    case $(tail -n 1 "$work/out") in
    *" end of file") ;;
    *)
        echo "stage2_cfg_test.sh: boundary tokens have no end-of-file token" >&2
        exit 1
        ;;
    esac
done

echo "stage2 accepts 3 user pairs across 2 --cfg options"
echo "stage2 rejects 4 invalid lists and 3 reserved-key overrides"
echo 'stage2 supplies 6 target values and holds 4 $cfg lexical boundaries'
