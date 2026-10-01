#!/usr/bin/env bash
# Runs vcix.S with the print_args model on Spike, then on gem5.
# Usage: examples/print_args/run.sh [build-dir [spike [pk [gem5.opt]]]]
# Each defaults to what setup/setup.sh produced; see scripts/sim.sh.
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/../../scripts/sim.sh"
MODEL="$BUILD/libprint_args.so"
CONFIG="$HERE/machine.yml"

rv_program "$HERE/vcix.S" "$BUILD/vcix"

echo "== spike: functional face"
spike_run "$MODEL" "$CONFIG" "$BUILD/vcix"

echo "== gem5: timing face"
gem5_run "$BUILD/m5out" "$MODEL" "$CONFIG" "$BUILD/vcix" 2>/dev/null \
  | grep -E '^\[config|^\[issue|^\[commit|^exit:'
