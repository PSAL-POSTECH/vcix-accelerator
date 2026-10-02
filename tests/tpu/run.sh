#!/usr/bin/env bash
# The tpu example: its timing on gem5, then what it computes on Spike (functional/run.sh). Usage: tests/tpu/run.sh [build-dir [spike [pk [gem5.opt]]]]
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/../../scripts/sim.sh"
failed=0
declare -A rc

report() {
  if [ "$1" = "$2" ]; then echo "PASS  $3 ($1)"; else echo "FAIL  $3 ($1, expected $2)"; failed=1; fi
}

on_gem5() {
  gem5_run "$BUILD/m5out-tpu-$1" "$BUILD/libtpu.so" "$3" "$BUILD/tpu-$2" --max-ticks 100000000 \
    > "$BUILD/tpu-$1.gem5.log" 2>&1
  rc[$1]=$?
}

said() {
  local line count=0
  for line in "${@:2}"; do
    [ "$(grep -Fxc "[tpu] $line" "$BUILD/tpu-$1.gem5.log")" = 1 ] && count=$((count + 1))
  done
  echo $count
}

times() { grep -Fxc "[tpu] $2" "$BUILD/tpu-$1.gem5.log"; }

lines() { grep -c "^\[tpu\] $2 " "$BUILD/tpu-$1.gem5.log"; }

for program in sfu one_cycle systolic_stream systolic_full xlu; do
  rv_program "$HERE/$program.S" "$BUILD/tpu-$program" || { echo "FAIL  $program.S does not build"; exit 2; }
done
printf 'tpu_trace: 1\ntpu_sfu_latency_cycles: 4\n' > "$BUILD/tpu-latency-4.yml"
printf 'tpu_sfu_latency_cycles: 0\n' > "$BUILD/tpu-latency-0.yml"

echo "-- special-function unit"
on_gem5 sfu sfu "$HERE/trace.yml"
on_gem5 latency-4 sfu "$BUILD/tpu-latency-4.yml"

burst=$(said sfu "issue vexp: the first" \
  "issue vexp: 1 cycles after the last issue, 1 in flight" \
  "issue vexp: 1 cycles after the last issue, 2 in flight" \
  "issue vexp: 1 cycles after the last issue, 3 in flight" \
  "issue vexp: 1 cycles after the last issue, 4 in flight")
report "exit ${rc[sfu]}, issues $burst, commits $(times sfu "commit vexp: 10 cycles after its issue")" \
  "exit 0, issues 5, commits 5" \
  "gem5 issues five back-to-back vexp one per cycle and commits each 10 cycles after its issue"

chain() {
  local name readers=() commits=()
  for name in vtanh verf vsin vcos vlog vatan; do
    readers+=("issue $name: $2 cycles after the last issue, 0 in flight")
    commits+=("commit $name: $2 cycles after its issue")
  done
  echo "readers $(said "$1" "${readers[@]}"), commits $(said "$1" "${commits[@]}") and $(lines "$1" commit)"
}
report "$(chain sfu 10)" "readers 6, commits 6 and 11" \
  "gem5 issues the reader of a special function's result 10 cycles after it, down a chain of the seven"
report "exit ${rc[latency-4]}, $(chain latency-4 4)" "exit 0, readers 6, commits 6 and 11" \
  "gem5 follows tpu_sfu_latency_cycles: 4 from the machine description"

echo "-- systolic array, 4 lanes: 7 slots, queues of 8"
on_gem5 systolic_stream systolic_stream "$HERE/systolic.yml"
on_gem5 systolic_full systolic_full "$HERE/systolic.yml"

delay=$(said systolic_full "issue systolic weight push: the first, input queue 0, output queue 0" \
  "issue systolic input push: 1 cycles after the last issue, 0 in flight, input queue 0, output queue 0" \
  "issue systolic pop: 11 cycles after the last issue, 0 in flight, input queue 0, output queue 4")
report "exit ${rc[systolic_full]}, lines $delay" "exit 0, lines 3" \
  "a pop of four is issued 4 + 7 cycles after the push of four, as the fourth element is in the output queue"

pushes=("issue systolic input push: the first, input queue 0, output queue 0")
for entries in 1 2 3 4 5 6; do
  pushes+=("issue systolic input push: 1 cycles after the last issue, 0 in flight, input queue $entries, output queue 0")
done
for entries in 1 3 5; do
  pushes+=("issue systolic input push: 2 cycles after the last issue, 0 in flight, input queue 6, output queue $entries")
done
stream="$(said systolic_stream "${pushes[@]}") of $(lines systolic_stream issue)"
report "exit ${rc[systolic_stream]}, lines $stream" "exit 0, lines 10 of 10" \
  "pushes of two go in one per cycle while the input queue has room, then one every two cycles, as the array drains"

full=$(said systolic_full \
  "issue systolic weight push: 1 cycles after the last issue, 0 in flight, input queue 1, output queue 8" \
  "issue systolic pop: 1 cycles after the last issue, 0 in flight, input queue 1, output queue 8" \
  "issue systolic weight push: 1 cycles after the last issue, 0 in flight, input queue 0, output queue 5")
report "lines $full of $(lines systolic_full issue)" "lines 3 of 11" \
  "with the output queue full the array stops, and moves again in the cycle after a pop"

echo "-- cross-lane unit, 4 lanes"
on_gem5 xlu xlu "$HERE/systolic.yml"

report "exit ${rc[xlu]}, lines $(said xlu "issue xlu_pop: 11 cycles after the last issue, 0 in flight")" \
  "exit 0, lines 1" \
  "after two pushes of four, the pop after the first is issued 4 + 8 - 1 cycles later: the XU's pass"
report "lines $(said xlu "issue xlu_pop: 19 cycles after the last issue, 0 in flight")" "lines 1" \
  "an all-gather of depth four takes the XU's 4 + 4 - 1 and the post-RPU's 2 x 4 + 4 cycles: 19"
report "lines $(said xlu "issue xlu_pop: 26 cycles after the last issue, 0 in flight") of $(lines xlu issue)" \
  "lines 1 of 12" \
  "a push enters during a pass of 27 cycles, one cycle after the pop that started it, and the next pop waits for the pass"

echo "-- one-cycle instructions"
on_gem5 one_cycle one_cycle "$HERE/trace.yml"

one_cycle=()
for name in xlu_push vlane_idx xlu_push_pattern compute xlu_pop dma_config_desc mvin mvin2 mvin3 mvout; do
  one_cycle+=("commit $name: 1 cycles after its issue")
done
drained=$(times one_cycle "issue xlu_push: 10 cycles after the last issue, 0 in flight")
commits="$(said one_cycle "${one_cycle[@]}") of $(lines one_cycle commit)"
report "exit ${rc[one_cycle]}, behind an empty model $drained, commits $commits" \
  "exit 0, behind an empty model 1, commits 10 of 11" \
  "gem5 commits each instruction of tpu::Misc, and a push and the first pop of tpu::Xlu, one cycle after its issue"

echo "-- the model"
on_gem5 latency-0 sfu "$BUILD/tpu-latency-0.yml"
why=$(grep -Fc "tpu: machine description: tpu_sfu_latency_cycles: '0' is not a pipeline depth: at least 1" \
  "$BUILD/tpu-latency-0.gem5.log")
report "exit ${rc[latency-0]}, reason given $why" "exit 1, reason given 1" "gem5 stops on tpu_sfu_latency_cycles: 0"

spike_run "$BUILD/libtpu.so" "" "$BUILD/tpu-sfu" > "$BUILD/tpu-sfu.spike.log" 2>&1
report "exit $?" "exit 0" "spike runs sfu.S to the program's exit"

echo "-- what it computes"
"$HERE/functional/run.sh" "$BUILD" "$SPIKE" "$PK" || failed=1

exit $failed
