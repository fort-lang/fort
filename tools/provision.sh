#!/bin/bash
# Provision the fort development VM (Ubuntu 24.04 arm64). Runs as root from
# the Vagrantfile; idempotent, so `tools/vm provision` can re-run it.
#
# Expects FORT_HOST_REPO: the absolute host path of the repository root, which
# is symlinked to /vagrant so worktree .git files (absolute host paths)
# resolve inside the guest.
set -eux

export DEBIAN_FRONTEND=noninteractive

if [ -z "${FORT_HOST_REPO:-}" ]; then
    echo "provision.sh: FORT_HOST_REPO is not set" >&2
    exit 1
fi

# ---- packages ---------------------------------------------------------------
apt-get update
apt-get install -y --no-install-recommends \
    build-essential clang clang-tidy clang-format llvm libclang-rt-18-dev \
    cmake ninja-build ccache gdb gdb-multiarch python3 git file shellcheck \
    gcc-x86-64-linux-gnu binutils-x86-64-linux-gnu libc6-dev-amd64-cross \
    qemu-user-static binfmt-support

# ---- environment for every login shell --------------------------------------
symbolizer=$(command -v llvm-symbolizer || true)
if [ -z "$symbolizer" ]; then
    symbolizer=$(find /usr/lib -path '/usr/lib/llvm-*/bin/llvm-symbolizer' | sort -V | tail -n 1)
fi
if [ -z "$symbolizer" ]; then
    echo "provision.sh: llvm-symbolizer not found (is the llvm package installed?)" >&2
    exit 1
fi
symbolizer=$(readlink -f "$symbolizer")
cat > /etc/profile.d/fort.sh <<EOF
# Written by tools/provision.sh (fort).
# qemu-x86_64 finds the x86-64 dynamic loader and libraries under this prefix.
export QEMU_LD_PREFIX=/usr/x86_64-linux-gnu
# Sanitizer reports are symbolized with the installed llvm-symbolizer.
export ASAN_SYMBOLIZER_PATH=$symbolizer
EOF

# ---- core dumps -----------------------------------------------------------------
# Ubuntu pipes every core dump to apport, and a piped core_pattern ignores
# `ulimit -c 0`, so each SIGABRT cost about a second; the runtime's tests
# abort dozens of times. Disable apport and dump to a plain file, which the
# limit then suppresses.
systemctl disable --now apport.service || true
cat > /etc/sysctl.d/60-fort-core.conf <<'EOF'
# Written by tools/provision.sh (fort): no apport, so ulimit -c 0 is honored.
kernel.core_pattern = core
EOF
sysctl -q -p /etc/sysctl.d/60-fort-core.conf

# ---- host repository path -----------------------------------------------------
if [ -L "$FORT_HOST_REPO" ]; then
    test "$(readlink "$FORT_HOST_REPO")" = /vagrant
elif [ -e "$FORT_HOST_REPO" ]; then
    echo "provision.sh: $FORT_HOST_REPO exists and is not a symlink" >&2
    exit 1
else
    mkdir -p "$(dirname "$FORT_HOST_REPO")"
    ln -s /vagrant "$FORT_HOST_REPO"
fi

# ---- smoke tests (fail provisioning loudly) ---------------------------------
# shellcheck disable=SC1091
. /etc/profile.d/fort.sh
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Cross compile for x86-64 and run under qemu via binfmt_misc.
echo 'int main(void){return 42;}' | x86_64-linux-gnu-gcc -x c - -o "$tmp/x"
status=0
"$tmp/x" || status=$?
test "$status" = 42
file "$tmp/x" | grep -q 'x86-64'
grep -q enabled /proc/sys/fs/binfmt_misc/qemu-x86_64

# Every sanitizer the presets use links and runs natively.
for s in address memory thread undefined; do
    echo 'int main(void){return 0;}' | clang -fsanitize=$s -x c - -o "$tmp/s_$s"
    "$tmp/s_$s"
done

# The lint configuration targets clang 18.
clang-tidy --version | grep -q 'version 18\.'
clang-format --version | grep -q 'version 18\.'

# Core dumps go nowhere near apport.
test "$(cat /proc/sys/kernel/core_pattern)" = core

# git resolves the host repository path.
test "$(readlink -f "$FORT_HOST_REPO")" = /vagrant

echo "provision.sh: ok"
