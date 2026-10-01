#!/usr/bin/env bash
# An instruction the model does not own must end as an illegal instruction on both
# simulators with no call to the model; an owned one is executed/committed exactly once,
# and each simulator calls only its own face.
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

# calls <log> <entry>: the model's reports of <entry> for the owned instruction.
# The line is owns_one.cc's own: "[model] <entry> <instruction bits>".
calls() { grep -Fxc "[model] $2 $OWNED_INSN" "$1"; }

# check <simulator> <program> <exit-code> <want-ok: 0|1>
# Every call the model reports must be one this simulator's face makes, for the
# owned instruction: on Spike one `execute` and nothing else; on gem5 one
# `commit`, and `accept` and `issue` at least once each (gem5 may ask again
# for an instruction it squashed). A call through the other face, a second
# execute or commit, or any call for another instruction is in `other`.
# A program that must not run to the end must end as an illegal instruction, on
# the unowned instruction; dying any other way is a failure.
check() {
  local sim=$1 prog=$2 rc=$3 want_ok=$4
  local log="$BUILD/$prog.$sim.log" ok=1 ended seen other
  local all execute accept issue commit
  all=$(grep -c '^\[model\]' "$log")
  execute=$(calls "$log" execute); accept=$(calls "$log" accept)
  issue=$(calls "$log" issue); commit=$(calls "$log" commit)
  if [ "$sim" = spike ]; then
    seen="execute $execute"; other=$((all - execute))
    [ "$execute" = 1 ] || ok=0
  else
    seen="accept $accept, issue $issue, commit $commit"; other=$((all - accept - issue - commit))
    [ "$commit" = 1 ] && [ "$accept" -ge 1 ] && [ "$issue" -ge 1 ] || ok=0
  fi
  [ "$other" = 0 ] || ok=0
  if [ "$want_ok" = 1 ]; then
    ended="exit $rc"
    [ "$rc" = 0 ] || ok=0
  elif [ "$rc" != 0 ] && "${sim}_illegal" "$log" "$UNOWNED_INSN"; then
    ended="illegal instruction $UNOWNED_INSN, exit $rc"
  else
    ended="not as an illegal instruction $UNOWNED_INSN, exit $rc"
    ok=0
  fi
  if [ "$ok" = 1 ]; then echo "PASS  $sim $prog ($ended; owned: $seen; other calls: $other)"
  else echo "FAIL  $sim $prog ($ended; owned: $seen; other calls: $other)"; failed=1; fi
}

for prog in owned unowned; do
  want=1; [ "$prog" = unowned ] && want=0

  spike_run "$MODEL" "" "$BUILD/$prog" > "$BUILD/$prog.spike.log" 2>&1
  check spike $prog $? $want

  gem5_run "$BUILD/m5out-$prog" "$MODEL" "" "$BUILD/$prog" > "$BUILD/$prog.gem5.log" 2>&1
  check gem5 $prog $? $want
done

exit $failed
