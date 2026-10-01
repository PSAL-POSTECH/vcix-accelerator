#!/usr/bin/env bash
# A model that accepts while others are in flight must really overlap on gem5: the
# unit holds no instruction, the model sees what is in flight, and its depth is the limit.
# Usage: tests/pipeline/run.sh <build-dir> <spike> <pk> <gem5.opt>
set -euo pipefail
BUILD=$(realpath "$1"); SPIKE=$2; PK=$3; GEM5=$4
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
MODEL="$BUILD/libpipelined.so"
failed=0

clang --target=riscv64 -march=rv64gcv_xsfvcp -c "$HERE/burst.S" -o "$BUILD/burst.o"
riscv64-unknown-elf-gcc -static "$BUILD/burst.o" -o "$BUILD/burst"

# expect <label> <log> <pattern> <comparison> <count>
expect() {
  local label=$1 log=$2 pattern=$3 op=$4 want=$5 got
  got=$(grep -c "$pattern" "$log" || true)
  if [ "$got" "$op" "$want" ]; then echo "PASS  $label ($got)"
  else echo "FAIL  $label (got $got, want $op $want)"; failed=1; fi
}

VCIX_ACCEL_MODEL="$MODEL" "$SPIKE" --extlib="$BUILD/libvcix_spike.so" \
  --isa=rv64gcv_zfh_xvcixaccel --varch=vlen:256,elen:64 "$PK" "$BUILD/burst" > "$BUILD/burst.spike.log" 2>&1
expect "spike executes all six"                "$BUILD/burst.spike.log" '^\[model\] execute$' -eq 6

"$GEM5" -d "$BUILD/m5out-burst" "$HERE/../../examples/gem5_se.py" --model "$MODEL" "$BUILD/burst" \
  > "$BUILD/burst.gem5.log" 2>&1
expect "gem5 commits all six"                  "$BUILD/burst.gem5.log" '^\[model\] commit '            -eq 6
expect "gem5 issues with three in flight"      "$BUILD/burst.gem5.log" '^\[model\] issue .* pending=3$' -ge 1
expect "gem5 never exceeds the model's depth"  "$BUILD/burst.gem5.log" '^\[model\] issue .* pending=[4-9]' -eq 0

exit $failed
