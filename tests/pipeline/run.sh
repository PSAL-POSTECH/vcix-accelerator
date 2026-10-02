#!/usr/bin/env bash
# A model that accepts while others are in flight must really overlap on gem5, up to its depth.
# Usage: tests/pipeline/run.sh [build-dir [spike [pk [gem5.opt]]]]
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/../../scripts/sim.sh"
MODEL="$BUILD/libpipelined.so"
failed=0

rv_program "$HERE/burst.S" "$BUILD/burst"

# expect <label> <log> <pattern> <comparison> <count>
expect() {
  local label=$1 log=$2 pattern=$3 op=$4 want=$5 got
  got=$(grep -c "$pattern" "$log" || true)
  if [ "$got" "$op" "$want" ]; then echo "PASS  $label ($got)"
  else echo "FAIL  $label (got $got, want $op $want)"; failed=1; fi
}

spike_run "$MODEL" "" "$BUILD/burst" > "$BUILD/burst.spike.log" 2>&1
expect "spike executes all six"                "$BUILD/burst.spike.log" '^\[model\] execute$' -eq 6
expect "spike calls nothing else"              "$BUILD/burst.spike.log" '^\[model\] \(issue\|commit\) ' -eq 0

gem5_run "$BUILD/m5out-burst" "$MODEL" "" "$BUILD/burst" > "$BUILD/burst.gem5.log" 2>&1
expect "gem5 commits all six"                  "$BUILD/burst.gem5.log" '^\[model\] commit '            -eq 6
expect "gem5 issues with three in flight"      "$BUILD/burst.gem5.log" '^\[model\] issue .* pending=3$' -ge 1
expect "gem5 never calls execute"              "$BUILD/burst.gem5.log" '^\[model\] execute$' -eq 0
expect "gem5 never exceeds the model's depth"  "$BUILD/burst.gem5.log" '^\[model\] issue .* pending=[4-9]' -eq 0

exit $failed
