#!/bin/bash
# Build the source compiler chain for one selected target (D14.7, D19.5).
set -eu

usage() {
    cat <<'EOF' >&2
usage: bootstrap.sh --target linux|darwin --seed <fort> [--preset <preset>]
                    [--cc <clang>] [--opt <opt>] [--stage3]
EOF
}

target_name=""
seed=""
preset=debug
cc=""
opt=""
stage3=no
while [ "$#" -gt 0 ]; do
    case "$1" in
    --target | --seed | --preset | --cc | --opt)
        if [ "$#" -lt 2 ]; then
            echo "bootstrap.sh: $1 needs an argument" >&2
            usage
            exit 2
        fi
        case "$1" in
        --target) target_name=$2 ;;
        --seed) seed=$2 ;;
        --preset) preset=$2 ;;
        --cc) cc=$2 ;;
        --opt) opt=$2 ;;
        esac
        shift 2
        ;;
    --stage3)
        stage3=yes
        shift
        ;;
    -h | --help)
        usage
        exit 0
        ;;
    *)
        echo "bootstrap.sh: unknown argument '$1'" >&2
        usage
        exit 2
        ;;
    esac
done

case "$target_name" in
linux | darwin) ;;
*)
    echo "bootstrap.sh: --target must be linux or darwin" >&2
    usage
    exit 2
    ;;
esac
if [ -z "$seed" ]; then
    echo "bootstrap.sh: --seed is required" >&2
    usage
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd -P)
build=$root/build/$preset
chain_args=("$target_name" "$seed" "$build")
if [ -n "$cc" ]; then chain_args+=(--cc "$cc"); fi
if [ -n "$opt" ]; then chain_args+=(--opt "$opt"); fi
bash "$root/tools/bootstrap_chain.sh" "${chain_args[@]}"

if [ "$stage3" != yes ]; then
    echo "stage2: $build/stage2/fort"
    echo "use --stage3 to test the fixed point"
    exit 0
fi

if [ -z "$cc" ]; then
    if [ "$target_name" = linux ]; then cc=clang; else cc=/usr/bin/clang; fi
fi
if [ -z "$opt" ]; then
    if [ "$target_name" = linux ]; then opt=opt-18; else opt=/opt/homebrew/bin/opt; fi
fi
if [ "$target_name" = linux ]; then
    target=x86_64-linux-gnu
else
    target=$(sed -n 's/^default triple: //p' "$build/seed.identity")
fi
last=$(bash "$root/tools/pin.sh" last)
exec bash "$root/tools/fixpoint.sh" "$build" \
    --bootstrap "$build/pin/$last/fort" \
    --entry "$build/bootstrap-head/entry/main.ft" \
    --source-root "$root/src/fort" \
    --std "$build/bootstrap-head/std" \
    --cc "$cc" --target "$target" --opt "$opt"
