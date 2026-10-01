#!/usr/bin/env bash
# Runs every test, on both simulators: each tests/*/run.sh, in order. Exits
# non-zero if any of them does.
# Usage: tests/run.sh [build-dir [spike [pk [gem5.opt]]]]
# Each defaults to what setup/setup.sh produced; see scripts/sim.sh.
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/../scripts/sim.sh"
failed=()

for test in "$HERE"/*/run.sh; do
  name=$(basename "$(dirname "$test")")
  echo "== $name"
  "$test" "$BUILD" "$SPIKE" "$PK" "$GEM5" || failed+=("$name")
done

echo
if [ ${#failed[@]} -eq 0 ]; then echo "all tests passed"; else echo "FAILED: ${failed[*]}"; exit 1; fi
