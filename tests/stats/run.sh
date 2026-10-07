#!/usr/bin/env bash
# The tpu model's port statistics in gem5's dumps: m5op windows, their boundaries, and utilized cycles. gem5 only.
# Usage: tests/stats/run.sh [build-dir [spike [pk [gem5.opt]]]]
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/../../scripts/sim.sh"
failed=0
declare -A rc

report() {
  if [ "$1" = "$2" ]; then echo "PASS  $3 ($1)"; else echo "FAIL  $3 ($1, expected $2)"; failed=1; fi
}

# on_gem5 <program>: runs it on the tpu model with a 4 x 4 array and queues of 8
on_gem5() {
  gem5_run "$BUILD/m5out-stats-$1" "$BUILD/libtpu.so" "$HERE/../tpu/systolic.yml" "$BUILD/stats-$1" \
    --max-ticks 100000000 > "$BUILD/stats-$1.gem5.log" 2>&1
  rc[$1]=$?
}

# stat <program> <dump> <name>: the value of the stat named <name>, or ending in .<name>, in the dump-th dump
stat() {
  awk -v want="$3" -v n="$2" '/Begin Simulation Statistics/ { s++ }
    s == n && ($1 == want || substr($1, length($1) - length(want)) == "." want) { print $2; exit }' \
    "$BUILD/m5out-stats-$1/stats.txt"
}

dumps() { grep -c 'Begin Simulation Statistics' "$BUILD/m5out-stats-$1/stats.txt"; }

# counts <program> <dump>: rows in, instructions issued, rows popped, pushes committed at the systolic array
counts() {
  echo "$(stat "$1" "$2" vcix.systolic.input.admitted) $(stat "$1" "$2" vcix.systolic.issue.admitted)" \
    "$(stat "$1" "$2" vcix.systolic.pop.admitted) $(stat "$1" "$2" vcix.committed.systolic_input_push)"
}

# consistent <program> <dump>: how many of the systolic array's ports have cycles = numCycles and utilized_cycles =
# admitted / capacity, plus one if the unit's utilized_cycles is its input port's
consistent() {
  local port admitted capacity cycles utilized agree=0
  local num_cycles=$(stat "$1" "$2" system.cpu.numCycles)
  for port in input issue weight_push pop; do
    admitted=$(stat "$1" "$2" "vcix.systolic.$port.admitted")
    capacity=$(stat "$1" "$2" "vcix.systolic.$port.capacity")
    cycles=$(stat "$1" "$2" "vcix.systolic.$port.cycles")
    utilized=$(stat "$1" "$2" "vcix.systolic.$port.utilized_cycles")
    [ "$cycles" = "$num_cycles" ] || continue
    awk -v a="$admitted" -v c="$capacity" -v u="$utilized" \
      'BEGIN { d = u - a / c; exit !(d < 5e-7 && d > -5e-7) }' && agree=$((agree + 1))
  done
  [ "$(stat "$1" "$2" vcix.systolic.utilized_cycles)" = "$(stat "$1" "$2" vcix.systolic.input.utilized_cycles)" ] &&
    agree=$((agree + 1))
  echo $agree
}

# utilized <program> <dump>: the utilized cycles of the input, issue and pop ports, as numbers
utilized() {
  local p
  for p in input issue pop; do printf '%g ' "$(stat "$1" "$2" "vcix.systolic.$p.utilized_cycles")"; done
}

for program in markers rows; do
  rv_program "$HERE/$program.S" "$BUILD/stats-$program" || { echo "FAIL  $program.S does not build"; exit 2; }
done

echo "-- m5op windows"
on_gem5 markers
report "exit ${rc[markers]}, dumps $(dumps markers)" "exit 0, dumps 5" \
  "four markers and the end of the run dump five times: dumpreset's 1000 ns period in a1 is ignored"
for dump in 1 2 3 4 5; do
  report "$(consistent markers $dump) of 5" "5 of 5" \
    "dump $dump: each systolic port's cycles is the CPU's numCycles, its utilized_cycles admitted / capacity, the unit's its input's"
done
report "$(counts markers 1)" "12 6 12 3" "reset, three pushes of four and three pops, dump: 12 rows in, 6 issued, 12 popped, 3 pushes"
report "$(counts markers 2)" "0 0 0 0" "reset, then dump at once: nothing, and the push right after the dump is not in it"
report "$(counts markers 3)" "4 2 4 1" \
  "the push after the dump and its pop, then dumpreset: dumped at the marker, not 2000 ns later as a0 asks"
report "$(counts markers 4)" "8 4 8 2" "two pushes and their pops between two dumpresets, the second also with a0 and a1 set"
report "$(counts markers 5)" "0 0 0 0" "after the last dumpreset, to the end of the run: nothing"
report "$(utilized markers 1)" "12 6 1.5 " \
  "dump 1 in utilized cycles: 12 rows at 1 a cycle, 6 instructions at 1 a cycle, 12 rows popped at 8 a cycle"
report "$(for d in 1 2 3 4 5; do utilized markers $d; done | awk '{ for (i = 1; i <= NF; i++) s[i % 3] += $i }
  END { printf "%g %g %g", s[1], s[2], s[0] }')" "24 12 3" \
  "the five dumps' utilized cycles add up to the run's: 24 rows in, 12 instructions, 24 rows popped at 8 a cycle"
report "$(for p in input issue weight_push pop; do printf '%s ' "$(stat markers 2 "vcix.systolic.$p.primary")"; done)" "1 0 0 0 " \
  "dump 2, right after a reset: each port's primary flag is still printed, 1 only on the input port"

echo "-- utilized cycles"
on_gem5 rows
report "exit ${rc[rows]}, rows $(stat rows 1 vcix.systolic.input.admitted), $(utilized rows 1)" \
  "exit 0, rows 1, 1 2 0.125 " \
  "one row alone in its window: one utilized cycle at the input, two at the issue (its push and its pop), an eighth at the pop"
window=$(stat rows 2 system.cpu.numCycles)
report "rows $(stat rows 2 vcix.systolic.input.admitted), $(utilized rows 2 | awk -v y="$window" '{ print ($1 <= y) ? "within" : "beyond" }')" \
  "rows 64, within" "64 rows back to back: 64 utilized cycles at the input, never more than the window's $window cycles"
report "$(utilized rows 2)" "64 32 8 " \
  "16 pushes of 4 rows and 16 pops: 64 utilized cycles in, 32 at the issue, 64 rows popped at 8 a cycle in 8"
for dump in 1 2; do
  report "$(consistent rows $dump) of 5" "5 of 5" "rows, dump $dump: cycles, utilized_cycles and the unit's utilized_cycles agree"
done

exit $failed
