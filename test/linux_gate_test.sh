#!/bin/bash
# Run the counted merge gate on a linux host.
set -euo pipefail

if [ "$#" -ne 0 ] || [ "$(uname -s)" != Linux ]; then
    echo "linux gate: requires a linux host and no arguments" >&2
    exit 2
fi

root=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$root"
echo "selected target: linux"

configure_preset() {
    cmake --preset "$1"
}

echo "linux gate: configure debug"
configure_preset debug
for step in format-check tidy; do
    echo "linux gate: $step"
    cmake --build --preset debug --target "$step"
done
for preset in debug asan ubsan; do
    if [ "$preset" != debug ]; then
        echo "linux gate: configure $preset"
        configure_preset "$preset"
    fi
    echo "linux gate: build $preset"
    cmake --build --preset "$preset"
    echo "linux gate: check-all $preset"
    cmake --build --preset "$preset" --target check-all
done
echo "tools/target linux gate: green"
