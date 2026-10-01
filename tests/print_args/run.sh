#!/usr/bin/env bash
# The print_args example on both simulators: each is configured from machine.yml,
# and each of the program's 9 accelerator instructions reaches the model once --
# `execute` on Spike, `commit` on gem5. gem5 may ask `latency` again for an
# instruction it squashed, so `issue` is at least 9, not exactly 9.
# Usage: tests/print_args/run.sh [build-dir [spike [pk [gem5.opt]]]]
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/../../scripts/sim.sh"
LOG="$BUILD/print_args.log"
INSNS=9
failed=0

"$REPO/examples/print_args/run.sh" "$BUILD" "$SPIKE" "$PK" "$GEM5" > "$LOG" 2>&1; rc=$?

# report <ok: 0|1> <what>
report() {
  if [ "$1" = 1 ]; then echo "PASS  $2"; else echo "FAIL  $2"; failed=1; fi
}
lines() { grep -c -- "$1" "$LOG"; }

ok=0; [ "$rc" = 0 ] && ok=1
report $ok "print_args runs on both simulators (exit $rc)"

configured=$(lines '^\[config \] print_args_latency_cycles=7 vpu_num_lanes=4$')
ok=0; [ "$configured" = 2 ] && ok=1
report $ok "both read machine.yml ($configured of 2)"

executed=$(lines '^\[execute\] ')
ok=0; [ "$executed" = $INSNS ] && ok=1
report $ok "spike executes each instruction once ($executed of $INSNS)"

issued=$(lines '^\[issue  \] '); committed=$(lines '^\[commit \] '); exited=$(lines '^exit: ')
ok=0; [ "$issued" -ge $INSNS ] && [ "$committed" = $INSNS ] && [ "$exited" = 1 ] && ok=1
report $ok "gem5 issues and commits each instruction (issue $issued, commit $committed of $INSNS, exit line $exited)"

[ "$failed" = 0 ] || { echo "---- $LOG"; cat "$LOG"; }
exit $failed
