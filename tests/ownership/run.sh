#!/usr/bin/env bash
# An instruction the model does not own must end as an illegal instruction on both
# simulators with no call to the model; an owned one is executed/committed exactly once.
# Usage: tests/ownership/run.sh <build-dir> <spike> <pk> <gem5.opt>
set -uo pipefail
BUILD=$(realpath "$1"); SPIKE=$2; PK=$3; GEM5=$4
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
MODEL="$BUILD/libowns_one.so"
OWNED_INSN=062541db
failed=0

for prog in owned unowned; do
  clang --target=riscv64 -march=rv64gcv_xsfvcp -c "$HERE/$prog.S" -o "$BUILD/$prog.o"
  riscv64-unknown-elf-gcc -static "$BUILD/$prog.o" -o "$BUILD/$prog"
done

# check <label> <log> <exit-code> <want-ok: 0|1> <entry the simulator calls once per instruction>
check() {
  local label=$1 log=$2 rc=$3 want_ok=$4 entry=$5
  local once unowned
  once=$(grep -c "^\[model\] $entry $OWNED_INSN" "$log")
  unowned=$(grep '^\[model\]' "$log" | grep -vc " $OWNED_INSN")
  local ok=1
  [ "$once" = 1 ] && [ "$unowned" = 0 ] || ok=0
  if [ "$want_ok" = 1 ]; then [ "$rc" = 0 ] || ok=0; else [ "$rc" != 0 ] || ok=0; fi
  if [ "$ok" = 1 ]; then echo "PASS  $label (exit $rc, $entry of owned $once, calls for unowned $unowned)"
  else echo "FAIL  $label (exit $rc, $entry of owned $once, calls for unowned $unowned)"; failed=1; fi
}

for prog in owned unowned; do
  want=1; [ "$prog" = unowned ] && want=0

  VCIX_ACCEL_MODEL="$MODEL" "$SPIKE" --extlib="$BUILD/libvcix_spike.so" \
    --isa=rv64gcv_zfh_xvcixaccel --varch=vlen:256,elen:64 "$PK" "$BUILD/$prog" > "$BUILD/$prog.spike.log" 2>&1
  check "spike $prog" "$BUILD/$prog.spike.log" $? $want execute

  "$GEM5" -d "$BUILD/m5out-$prog" "$HERE/../../examples/gem5_se.py" --model "$MODEL" "$BUILD/$prog" \
    > "$BUILD/$prog.gem5.log" 2>&1
  check "gem5  $prog" "$BUILD/$prog.gem5.log" $? $want commit
done

exit $failed
