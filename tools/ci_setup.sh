#!/bin/bash
# tools/ci_setup.sh <linux|darwin> [lint]: install the tools a CI job needs.
#
# The GitHub runners provide git, python3, cmake and node. This script adds LLVM 18, the
# one LLVM of every host: it compiles, verifies with `opt`, and lints, because the lint
# configuration is that of clang 18. With `lint`, it also adds clang-format 18 and
# clang-tidy 18 on Linux; Homebrew llvm@18 holds them on Darwin.
#
# On Linux the script writes ASAN_SYMBOLIZER_PATH to $GITHUB_ENV when that file is set,
# so the later steps of the job see it.
set -eu

usage() {
    echo "usage: ci_setup.sh <linux|darwin> [lint]" >&2
}

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    usage
    exit 2
fi
host=$1
lint=no
if [ "$#" -eq 2 ]; then
    if [ "$2" != lint ]; then
        usage
        exit 2
    fi
    lint=yes
fi

# retry <attempts> <command...>: run the command at most <attempts> times and stop at the
# first success. Wait RETRY_DELAY seconds (default 10) between attempts. Return the last
# exit status.
retry() {
    local attempts=$1 attempt=1 status
    shift
    while true; do
        status=0
        "$@" || status=$?
        if [ "$status" -eq 0 ]; then
            return 0
        fi
        if [ "$attempt" -ge "$attempts" ]; then
            echo "ci_setup.sh: '$*' failed $attempt times" >&2
            return "$status"
        fi
        echo "ci_setup.sh: '$*' exited $status, attempt $attempt of $attempts" >&2
        attempt=$((attempt + 1))
        sleep "${RETRY_DELAY:-10}"
    done
}

# The Acquire options end each stalled connection after 30 seconds and retry it 3 times.
# They do not end a slow download, so `timeout -k 10 <bound>` bounds each apt-get call:
# TERM after <bound> seconds, KILL 10 seconds later. timeout runs under sudo, so it
# signals apt-get itself and a killed call leaves no apt-get that holds the apt lock.
APT_OPTIONS=(-o Acquire::Retries=3 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30)

# apt_get <bound> <argument...>: run apt-get with APT_OPTIONS for at most <bound> seconds.
apt_get() {
    local bound=$1
    shift
    sudo DEBIAN_FRONTEND=noninteractive timeout -k 10 "$bound" apt-get "${APT_OPTIONS[@]}" "$@"
}

# apt_fetch <bound> <package...>: run `apt-get update`, then download the packages and
# their dependencies to the apt cache in at most <bound> seconds. The update may fail or
# only warn: an old index often still holds the packages, so the download decides the
# status.
apt_fetch() {
    local bound=$1
    shift
    apt_get 60 update -q || true
    apt_get "$bound" install -y -q --no-install-recommends --download-only "$@"
}

# apt_unpack <package...>: install the packages from the apt cache. A timeout during an
# unpack leaves dpkg interrupted, and then apt refuses to run. So each apt_unpack first runs
# `dpkg --configure -a` for at most 60 seconds. That command does nothing on a clean system.
# Each download comes before the first unpack, so no download needs it.
apt_unpack() {
    sudo timeout -k 10 60 dpkg --configure -a || true
    apt_get 120 install -y -q --no-install-recommends "$@"
}

# skip_azure_mirror: remove the azure mirror from the mirror list of the runner image.
# The list names http://azure.archive.ubuntu.com, then https://archive.ubuntu.com, then
# https://security.ubuntu.com. apt goes to the next mirror only when a mirror fails, not
# when it is slow. On a list with no azure line this function does nothing.
# APT_MIRROR_LIST names the list; a test sets it.
skip_azure_mirror() {
    local list=${APT_MIRROR_LIST:-/etc/apt/apt-mirrors.txt}
    if [ -f "$list" ] && grep -q 'azure\.archive\.ubuntu\.com' "$list"; then
        sudo sed -i '/azure\.archive\.ubuntu\.com/d' "$list"
        echo "ci_setup.sh: the mirror list is now:" >&2
        cat "$list" >&2
    fi
}

setup_linux() {
    local packages=(clang-18 llvm-18 libclang-rt-18-dev ninja-build)
    if [ "$lint" = yes ]; then
        packages+=(clang-format-18 clang-tidy-18)
    fi
    # The azure mirror of the runner image is fast in most regions: the 95 MB of the
    # packages take 1 to 4 seconds. In region westus3 on Ubuntu 26.04 its libllvm18
    # download stalled for 6 minutes and more. So the first download gets 30 seconds.
    # After a failure, the script skips the azure mirror and downloads from
    # archive.ubuntu.com, at most 2 times. A mirror outage once made `apt-get update`
    # hang until GitHub cancelled the job after about 45 minutes, so each apt-get call has
    # a bound.
    local start=$SECONDS
    if ! apt_fetch 30 "${packages[@]}"; then
        echo "ci_setup.sh: the download from the first mirror failed or took more than 30 s" >&2
        skip_azure_mirror
        retry 2 apt_fetch 60 "${packages[@]}"
    fi
    retry 2 apt_unpack "${packages[@]}"
    echo "ci_setup.sh: apt installed the packages in $((SECONDS - start)) s"

    # The clang 18 sanitizer runtimes cannot map their shadow memory under the runner
    # kernel's default ASLR entropy. 28 bits is the value they support.
    sudo sysctl -q -w vm.mmap_rnd_bits=28

    # The runtime tests abort on purpose. Apport handles each core dump for about a
    # second, so write cores to a plain file, which `ulimit -c 0` in a test step then
    # suppresses.
    sudo systemctl disable --now apport.service >/dev/null 2>&1 || true
    sudo sysctl -q -w kernel.core_pattern=core

    local symbolizer=/usr/lib/llvm-18/bin/llvm-symbolizer
    if [ ! -x "$symbolizer" ]; then
        echo "ci_setup.sh: $symbolizer not found" >&2
        exit 1
    fi
    if [ -n "${GITHUB_ENV:-}" ]; then
        echo "ASAN_SYMBOLIZER_PATH=$symbolizer" >>"$GITHUB_ENV"
    fi

    # The presets, the driver's default --cc and the test harness name `clang`. The link
    # in /usr/local/bin, which precedes /usr/bin in PATH, makes that name clang 18
    # whatever the image's default clang is.
    sudo ln -sf /usr/bin/clang-18 /usr/local/bin/clang

    opt-18 --version | grep -q 'LLVM version 18\.'
    clang --version | grep -q 'clang version 18\.'
}

setup_darwin() {
    # Homebrew llvm@18 is linked into /opt/homebrew/bin, so `clang` and the default
    # FORT_OPT, /opt/homebrew/bin/opt, are LLVM 18. The runner image links an LLVM of its
    # own. The script unlinks every linked LLVM first: then `brew install` gives no
    # warning, and no tool of another LLVM stays in /opt/homebrew/bin.
    # cmake/dev_targets.cmake finds clang-format and clang-tidy under
    # /opt/homebrew/opt/llvm@18/bin.
    # An auto-update and a check of the dependents of each installed formula cost
    # minutes in every job and change nothing that the job uses.
    export HOMEBREW_NO_AUTO_UPDATE=1
    export HOMEBREW_NO_INSTALLED_DEPENDENTS_CHECK=1
    local formula
    for formula in $(brew list --formula | grep -E '^llvm(@[0-9]+)?$'); do
        brew unlink "$formula" >/dev/null
    done
    brew install llvm@18 ninja
    brew link --overwrite --force llvm@18

    /opt/homebrew/bin/opt --version | grep -q 'LLVM version 18\.'
    /opt/homebrew/bin/clang --version | grep -q 'clang version 18\.'
    # The presets name `clang`, which must resolve to Homebrew's.
    if [ "$(command -v clang)" != /opt/homebrew/bin/clang ] && [ -n "${GITHUB_PATH:-}" ]; then
        echo /opt/homebrew/bin >>"$GITHUB_PATH"
    fi
}

case "$host" in
linux) setup_linux ;;
darwin) setup_darwin ;;
*)
    usage
    exit 2
    ;;
esac
