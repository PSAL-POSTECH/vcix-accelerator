#!/usr/bin/env bash
# The tpu model's port statistics in gem5's dumps: m5op windows, their boundaries, and utilization. gem5 only.
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
    "$(stat "$1" "$2" vcix.systolic.pop.admitted) $(stat "$1" "$2" vcix.committed::systolic_input_push)"
}

# consistent <program> <dump>: how many of the systolic array's ports have cycles = numCycles and the utilization formula
consistent() {
  local port admitted capacity cycles utilization agree=0
  local num_cycles=$(stat "$1" "$2" system.cpu.numCycles)
  for port in input issue weight_push pop; do
    admitted=$(stat "$1" "$2" "vcix.systolic.$port.admitted")
    capacity=$(stat "$1" "$2" "vcix.systolic.$port.capacity")
    cycles=$(stat "$1" "$2" "vcix.systolic.$port.cycles")
    utilization=$(stat "$1" "$2" "vcix.systolic.$port.utilization")
    [ "$cycles" = "$num_cycles" ] || continue
    awk -v a="$admitted" -v c="$capacity" -v y="$cycles" -v u="$utilization" \
      'BEGIN { d = u - a / (c * y); exit !(d < 5e-7 && d > -5e-7) }' && agree=$((agree + 1))
  done
  [ "$(stat "$1" "$2" vcix.systolic.utilization)" = "$(stat "$1" "$2" vcix.systolic.input.utilization)" ] &&
    agree=$((agree + 1))
  echo $agree
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
    "dump $dump: each systolic port's cycles is the CPU's numCycles, its utilization admitted / (capacity * cycles), the unit's its input's"
done
report "$(counts markers 1)" "12 6 12 3" "reset, three pushes of four and three pops, dump: 12 rows in, 6 issued, 12 popped, 3 pushes"
report "$(counts markers 2)" "0 0 0 0" "reset, then dump at once: nothing, and the push right after the dump is not in it"
report "$(counts markers 3)" "4 2 4 1" \
  "the push after the dump and its pop, then dumpreset: dumped at the marker, not 2000 ns later as a0 asks"
report "$(counts markers 4)" "8 4 8 2" "two pushes and their pops between two dumpresets, the second also with a0 and a1 set"
report "$(counts markers 5)" "0 0 0 0" "after the last dumpreset, to the end of the run: nothing"

echo "-- utilization"
on_gem5 rows
one=$(stat rows 1 vcix.systolic.input.utilization)
window=$(stat rows 1 system.cpu.numCycles)
report "exit ${rc[rows]}, rows $(stat rows 1 vcix.systolic.input.admitted), $one is 1 / $window" \
  "exit 0, rows 1, $(awk -v y="$window" 'BEGIN { printf "%.6f", 1 / y }') is 1 / $window" \
  "one row alone in its window: the input port's utilization is one row over the window's cycles"
many=$(stat rows 2 vcix.systolic.input.utilization)
report "rows $(stat rows 2 vcix.systolic.input.admitted), $(awk -v u="$many" -v o="$one" 'BEGIN { print (u > o && u <= 1) ? "between" : "outside" }')" \
  "rows 64, between" "64 rows back to back: more of the window used than by one row, never more than one row a cycle ($many)"
for dump in 1 2; do
  report "$(consistent rows $dump) of 5" "5 of 5" "rows, dump $dump: cycles, utilization and the unit's utilization agree"
done

exit $failed
