#!/bin/bash
# Print the compiler target from one verified seed identity.
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: seed_target.sh <seed-identity>" >&2
    exit 2
fi
identity=$1
if [ ! -f "$identity" ]; then
    echo "seed_target.sh: seed identity is missing: $identity" >&2
    exit 2
fi
count=$(grep -c '^default triple: ' "$identity" || true)
triple=$(sed -n 's/^default triple: //p' "$identity")
if [ "$count" -ne 1 ]; then
    echo "seed_target.sh: seed identity must contain one default triple" >&2
    exit 1
fi
case "$triple" in
x86_64-unknown-linux-gnu)
    echo x86_64-linux-gnu
    ;;
arm64-apple-macosx*)
    if ! [[ "$triple" =~ ^arm64-apple-macosx[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        echo "seed_target.sh: unsupported seed target '$triple'" >&2
        exit 1
    fi
    echo "$triple"
    ;;
*)
    echo "seed_target.sh: unsupported seed target '$triple'" >&2
    exit 1
    ;;
esac
