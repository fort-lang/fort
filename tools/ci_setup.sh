#!/bin/bash
# tools/ci_setup.sh <linux|darwin> [lint]: install the tools a CI job needs.
#
# The GitHub runners provide git, python3, cmake and node. This script adds the LLVM
# toolchain that configure and the tests need. With `lint`, it also adds clang-format 18
# and clang-tidy 18, because the lint configuration is that of clang 18.
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

setup_linux() {
    local packages=(clang clang-18 llvm-18 libclang-rt-18-dev ninja-build)
    if [ "$lint" = yes ]; then
        packages+=(clang-format-18 clang-tidy-18)
    fi
    sudo apt-get update -q
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends \
        "${packages[@]}"

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

    opt-18 --version | grep -q 'LLVM version 18\.'
}

setup_darwin() {
    # The dev Mac links Homebrew llvm@19, so `clang` and the default FORT_OPT,
    # /opt/homebrew/bin/opt, are LLVM 19. The runner gets the same links.
    # An auto-update and a check of the dependents of each installed formula cost
    # minutes in every job and change nothing that the job uses.
    export HOMEBREW_NO_AUTO_UPDATE=1
    export HOMEBREW_NO_INSTALLED_DEPENDENTS_CHECK=1
    brew install llvm@19 ninja
    brew link --overwrite --force llvm@19
    if [ "$lint" = yes ]; then
        # cmake/dev_targets.cmake finds the keg-only llvm@18 tools by their prefix.
        brew install llvm@18
    fi

    /opt/homebrew/bin/opt --version | grep -q 'LLVM version 19\.'
    /opt/homebrew/bin/clang --version | grep -q 'clang version 19\.'
    # The presets name `clang`, which must resolve to the Homebrew one, as on the dev Mac.
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
