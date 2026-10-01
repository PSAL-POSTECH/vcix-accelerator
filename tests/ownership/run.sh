#!/usr/bin/env bash
# An instruction the model does not own must end as an illegal instruction on both
# simulators with no call to the model; an owned one is executed/committed exactly once.
# Usage: tests/ownership/run.sh [build-dir [spike [pk [gem5.opt]]]]
# Each defaults to what setup/setup.sh produced; see scripts/sim.sh.
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/../../scripts/sim.sh"
MODEL="$BUILD/libowns_one.so"
OWNED_INSN=062541db    # sf.vc.x 0x1, 0x2, 0x3, a0
UNOWNED_INSN=0a2541db  # sf.vc.x 0x2, 0x2, 0x3, a0
failed=0

for prog in owned unowned; do
  rv_program "$HERE/$prog.S" "$BUILD/$prog" || { echo "FAIL  $prog.S does not build"; exit 2; }
done

# check <simulator> <program> <exit-code> <want-ok: 0|1> <entry the simulator calls once per instruction>
# A program that must not run to the end must end as an illegal instruction, on
# the unowned instruction; dying any other way is a failure.
check() {
  local sim=$1 prog=$2 rc=$3 want_ok=$4 entry=$5
  local log="$BUILD/$prog.$sim.log" once unowned ended
  once=$(grep -c "^\[model\] $entry $OWNED_INSN" "$log")
  unowned=$(grep '^\[model\]' "$log" | grep -vc " $OWNED_INSN")
  local ok=1
  [ "$once" = 1 ] && [ "$unowned" = 0 ] || ok=0
  if [ "$want_ok" = 1 ]; then
    ended="exit $rc"
    [ "$rc" = 0 ] || ok=0
  elif [ "$rc" != 0 ] && "${sim}_illegal" "$log" "$UNOWNED_INSN"; then
    ended="illegal instruction $UNOWNED_INSN, exit $rc"
  else
    ended="not as an illegal instruction $UNOWNED_INSN, exit $rc"
    ok=0
  fi
  if [ "$ok" = 1 ]; then echo "PASS  $sim $prog ($ended, $entry of owned $once, calls for unowned $unowned)"
  else echo "FAIL  $sim $prog ($ended, $entry of owned $once, calls for unowned $unowned)"; failed=1; fi
}

for prog in owned unowned; do
  want=1; [ "$prog" = unowned ] && want=0

  spike_run "$MODEL" "" "$BUILD/$prog" > "$BUILD/$prog.spike.log" 2>&1
  check spike $prog $? $want execute

  gem5_run "$BUILD/m5out-$prog" "$MODEL" "" "$BUILD/$prog" > "$BUILD/$prog.gem5.log" 2>&1
  check gem5 $prog $? $want commit
done

exit $failed
