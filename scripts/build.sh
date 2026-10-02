#!/usr/bin/env bash
# Configure and build this repository against the Spike the setup produced.
# Usage: scripts/build.sh [-j N] [build-dir]      (default: <repo>/build)
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=../setup/versions.env
source "$REPO/setup/versions.env"

JOBS=$(nproc)
if [ "${1:-}" = -j ]; then JOBS="$2"; shift 2; fi
BUILD="${1:-$REPO/build}"

test -f "$SPIKE_BUILD/libriscv.a" \
  || { echo "no Spike build at $SPIKE_BUILD -- run setup/setup.sh, or set SPIKE_ROOT" >&2; exit 1; }

cmake -G Ninja -S "$REPO" -B "$BUILD" -DSPIKE_SRC="$SPIKE_ROOT" -DSPIKE_BUILD="$SPIKE_BUILD"
cmake --build "$BUILD" -j "$JOBS"
