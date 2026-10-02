#!/usr/bin/env bash
# The print_args example on both simulators: it runs, and prints what it is given. Usage: tests/print_args/run.sh [build-dir [spike [pk [gem5.opt]]]]
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/../../scripts/sim.sh"
LOG="$BUILD/print_args.log"
INSNS=9
failed=0

"$REPO/examples/print_args/run.sh" "$BUILD" "$SPIKE" "$PK" "$GEM5" > "$LOG" 2>&1; rc=$?

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

no_vector=$(grep -E '^\[execute\] insn=(062541db|0e22b1db) ' "$LOG" | grep -Evc ' v[0-9]+\[')
two_vectors=$(grep -Ec '^\[execute\] insn=2620825b .* v2\[0\.\.1\]=0x3f800000,0x40000000 v1\[0\.\.1\]=0x41200000,0x41300000$' "$LOG")
ok=0; [ "$no_vector" = 2 ] && [ "$two_vectors" = 1 ] && ok=1
report $ok "spike shows the registers the encoding names (no vector operand: $no_vector of 2; vs2 and vs1: $two_vectors of 1)"

latency_of() {
  sed -n "s/^\[issue  \] insn=$1 .* cycle=\([0-9]*\) ready=\([0-9]*\)\$/\1 \2/p" "$LOG" | tail -n 1 | {
    read -r cycle ready && echo $((ready - cycle))
  }
}
m1=$(latency_of 2c2081db); m2=$(latency_of 2c23025b); custom1=$(latency_of 00b5002b)
ok=0; [ "$m1" = 7 ] && [ "$m2" = 14 ] && [ "$custom1" = 7 ] && ok=1
report $ok "gem5 latency follows LMUL only with a vector operand (m1 ${m1:-none}, m2 ${m2:-none}, custom-1 under m2 ${custom1:-none})"

[ "$failed" = 0 ] || { echo "---- $LOG"; cat "$LOG"; }
exit $failed
