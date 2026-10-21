#!/usr/bin/env bash
# vcix_timeline on both simulators: the file it writes for tpuv6e, tpu, copier and print_args runs, against stats.txt,
# the models' own traces, runs without it and two instances. Usage: tests/timeline/run.sh [build-dir [spike [pk [gem5.opt]]]]
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/../../scripts/sim.sh"
OUT="$BUILD/timeline"
failed=0
rm -rf "$OUT" && mkdir -p "$OUT" || exit 2

report() {
  if [ "$1" = "$2" ]; then echo "PASS  $3 ($1)"; else echo "FAIL  $3 ($1, expected $2)"; failed=1; fi
}

check() { python3 "$HERE/check.py" "$@"; }

# committed <run>: the sum of the run's vcix.committed.* counts
committed() { awk '$1 ~ /\.vcix\.committed\./ { n += $2 } END { print n + 0 }' "$OUT/m5out-$1/stats.txt"; }

# primary <run>: each unit's primary port and admitted, as stats.txt gives them
primary() {
  sed -n 's/^.*\.vcix\.\([^.]*\)\.admitted *\([0-9]*\) *# [^ ]* admitted at port \([^ ]*\) .*$/\1.\3=\2/p' \
    "$OUT/m5out-$1/stats.txt" | sort | tr '\n' ' ' | sed 's/ $//'
}

# measured <run>: the lines of stats.txt a recording could change, vcix.* and simTicks and numCycles
measured() { grep -E '\.vcix\.|^simTicks |\.numCycles ' "$OUT/m5out-$1/stats.txt"; }

# said <log>: what gem5 printed, but for the lines that name the run's time, host or paths
said() { grep -Ev '^gem5 (started|executing)|^command line:|config\.dot' "$1"; }

# on_both <run> <model> <machine.yml> <program>: gem5 and Spike, each with vcix_timeline and without
on_both() {
  { cat "$3"; echo "vcix_timeline: $OUT/$1.gem5.timeline"; } > "$OUT/$1.gem5.yml"
  { cat "$3"; echo "vcix_timeline: $OUT/$1.spike.timeline"; } > "$OUT/$1.spike.yml"
  timeout 300 "$GEM5" -d "$OUT/m5out-$1" "$REPO/examples/gem5_se.py" --model "$BUILD/lib$2.so" --config "$OUT/$1.gem5.yml" \
    --max-ticks 1000000000 "$4" > "$OUT/$1.gem5.log" 2>&1
  echo -n "gem5 exit $?"
  timeout 300 "$GEM5" -d "$OUT/m5out-$1-off" "$REPO/examples/gem5_se.py" --model "$BUILD/lib$2.so" --config "$3" \
    --max-ticks 1000000000 "$4" > "$OUT/$1-off.gem5.log" 2>&1
  echo -n " $?"
  spike_run "$BUILD/lib$2.so" "$OUT/$1.spike.yml" "$4" < /dev/null > "$OUT/$1.spike.log" 2>&1
  echo -n ", Spike exit $?"
  spike_run "$BUILD/lib$2.so" "$3" "$4" < /dev/null > "$OUT/$1-off.spike.log" 2>&1
  echo " $?"
}

# unchanged <run>: whether gem5's measured stats and output, and Spike's output, are the same without vcix_timeline
unchanged() {
  local gem5=differ output=differ spike=differ
  cmp -s <(measured "$1") <(measured "$1-off") && gem5=same
  cmp -s <(said "$OUT/$1.gem5.log") <(said "$OUT/$1-off.gem5.log") && output=same
  cmp -s "$OUT/$1.spike.log" "$OUT/$1-off.spike.log" && spike=same
  echo "stats $gem5 ($(measured "$1" | wc -l) lines), gem5 output $output, Spike output $spike"
}

# lines <file>: its line count
lines() { wc -l < "$1" | tr -d ' '; }

# json <run> <sim>: whether every line of the run's timeline is JSON
json() { check json "$OUT/$1.$2.timeline"; }

printf 'vpu_num_lanes: 4\nvpu_spad_size_kb_per_lane: 128\nvpu_vector_length_bits: 256\n' > "$OUT/machine.yml"
{ cat "$OUT/machine.yml"; printf 'tpu_trace: 1\ntpu_mxu_queue_entries: 16\n'; } > "$OUT/mxu_rate.yml"
{ cat "$OUT/machine.yml"; grep -v '^#' "$REPO/tests/tpu/xlu.yml"; } > "$OUT/xlu.yml"
rv_program "$REPO/tests/tpuv6e/mxu_rate.S" "$OUT/mxu_rate" || { echo "FAIL  mxu_rate.S does not build"; exit 2; }
rv_program "$REPO/tests/tpu/xlu.S" "$OUT/xlu" || { echo "FAIL  xlu.S does not build"; exit 2; }
rv_program "$REPO/examples/print_args/vcix.S" "$OUT/vcix" || { echo "FAIL  vcix.S does not build"; exit 2; }
riscv64-unknown-elf-gcc -O1 -Wall -Werror -static -march=rv64gcv -mabi=lp64d -DWAIT -DTIMING \
  "$REPO/examples/rtl/copier/copy.c" -o "$OUT/copy" || { echo "FAIL  copier/copy.c does not build"; exit 2; }

# uarch <run> <model> <machine.yml> <program> <insns>: the checks of a vcix_uarch model's run, <insns> it owns
uarch() {
  local run=$1 model=$2 insns=$5 sim
  report "$(on_both "$run" "$model" "$3" "$4")" "gem5 exit 0 0, Spike exit 0 0" "$run on $model: both simulators run it, with vcix_timeline and without"
  for sim in gem5 spike; do
    local f="$OUT/$run.$sim.timeline"
    report "$(json "$run" $sim)" "$(lines "$f") of $(lines "$f") lines JSON" "$run, $sim: every line of the timeline is JSON"
    report "$(check header "$f" "$OUT/m5out-$run/stats.txt")" \
      "model $model; $(primary "$run" | wc -w) units, $(grep -c '\.vcix\.committed\.' "$OUT/m5out-$run/stats.txt") encodings, as stats.txt names them" \
      "$run, $sim: the header names the model, its units, their primary ports and its encodings as gem5's stats.txt does"
    report "$(check issues "$f" "$OUT/m5out-$run/stats.txt"), stats.txt $(committed "$run")" \
      "$insns kept, committed in issue order; per encoding as stats.txt, stats.txt $insns" \
      "$run, $sim: the issues no squash dropped ($(check squashed "$f") dropped) are the program's $insns, each committed once in order, as many of each encoding as vcix.committed.*"
    report "$(check ports "$f" "$OUT/m5out-$run/stats.txt")" \
      "0 cycles of use below 0, each port's use sums to its final admitted; primary $(primary "$run")" \
      "$run, $sim: per-cycle port use from the state lines sums to each port's admitted, the primary port's to gem5's vcix.<unit>.admitted"
    report "$(check live "$f" | sed 's/^[0-9]* state lines/state lines/')" "state lines, each id named is in flight or its command queued" \
      "$run, $sim: every id in ready or a queue's waiting was issued earlier and is neither committed nor squashed, every id in a queue's commands issued and not squashed ($(check live "$f" | cut -d' ' -f1) state lines)"
  done
  report "$(check order "$OUT/$run.gem5.timeline" | md5sum | cut -c1-8)" "$(check order "$OUT/$run.spike.timeline" | md5sum | cut -c1-8)" \
    "$run: gem5's kept issues and Spike's name the same instructions in the same order"
  report "$(head -1 "$OUT/$run.spike.timeline" | md5sum | cut -c1-8), $(grep -c '^{"e":"spike"' "$OUT/$run.spike.timeline") spike event" \
    "$(head -1 "$OUT/$run.gem5.timeline" | md5sum | cut -c1-8), 1 spike event" \
    "$run: Spike's header is gem5's, and Spike's file says it is serialized"
  report "$(unchanged "$run")" "stats same ($(measured "$run" | wc -l) lines), gem5 output same, Spike output same" \
    "$run: recording changes nothing gem5 measures or prints, nor what Spike prints"
}

echo "-- tpuv6e, mxu_rate.S: the matrix unit's pipeline, queues of 16"
uarch mxu_rate tpuv6e "$OUT/mxu_rate.yml" "$OUT/mxu_rate" 16
report "$(check trace "$OUT/mxu_rate.gem5.timeline" "$OUT/mxu_rate.gem5.log")" \
  "$(grep -c '^\[tpu\] issue ' "$OUT/mxu_rate.gem5.log") issues, names and gaps as the model's trace" \
  "mxu_rate, gem5: the timeline's issues have the names and gaps of tpu_trace's issue lines, in order"

echo "-- tpu, xlu.S: the cross-lane unit's queue of commands"
uarch xlu tpu "$OUT/xlu.yml" "$OUT/xlu" 13
report "$(check trace "$OUT/xlu.gem5.timeline" "$OUT/xlu.gem5.log")" \
  "$(grep -c '^\[tpu\] issue ' "$OUT/xlu.gem5.log") issues, names and gaps as the model's trace" \
  "xlu, gem5: the timeline's issues have the names and gaps of tpu_trace's issue lines, in order"
queued=$(grep -c '"commands":\[\[' "$OUT/xlu.gem5.timeline")/$(grep -c '"commands":\[\[' "$OUT/xlu.spike.timeline")
report "$(case $queued in 0/* | */0) echo none ;; *) echo some ;; esac) on both" "some on both" \
  "xlu: state lines hold queued commands on both simulators ($queued on gem5/Spike), so the check of their ids has some to check"

echo "-- copier, copy.c -DWAIT -DTIMING: commands in a queue of 4, a memory port"
uarch copy copier "$OUT/machine.yml" "$OUT/copy" 3

echo "-- print_args, vcix.S: a hand-written model, no ports"
report "$(on_both print_args print_args "$REPO/examples/print_args/machine.yml" "$OUT/vcix")" "gem5 exit 0 0, Spike exit 0 0" \
  "print_args: both simulators run it, with vcix_timeline and without"
for sim in gem5 spike; do
  f="$OUT/print_args.$sim.timeline"
  report "$(json print_args $sim)" "$(lines "$f") of $(lines "$f") lines JSON" "print_args, $sim: every line of the timeline is JSON"
  report "$(check header "$f" "$OUT/m5out-print_args/stats.txt")" "model print_args; 0 units, 2 encodings, as stats.txt names them" \
    "print_args, $sim: the header names the model and its encodings as stats.txt does, and no units, as stats.txt prints none"
done
report "$(check issues "$OUT/print_args.gem5.timeline" "$OUT/m5out-print_args/stats.txt"), stats.txt $(committed print_args)" \
  "9 kept, committed in issue order; per encoding as stats.txt, stats.txt 9" \
  "print_args, gem5: the issues are vcix.S's 9, committed once each in order, as vcix.committed.* counts them"
report "$(check order "$OUT/print_args.gem5.timeline")" "vcix,vcix,vcix,vcix,vcix,vcix,vcix,vcix,custom1" \
  "print_args, gem5: in vcix.S's order, eight custom-2 then the custom-1"
report "$(check trace "$OUT/print_args.gem5.timeline" "$OUT/print_args.gem5.log")" \
  "$(grep -c '^\[issue  \] ' "$OUT/print_args.gem5.log") issues, cycles and latencies as the model's trace" \
  "print_args, gem5: each issue's cycle and latency are the ones print_args prints as cycle= and ready="
report "$(check ports "$OUT/print_args.gem5.timeline" "$OUT/m5out-print_args/stats.txt")" \
  "0 cycles of use below 0, each port's use sums to its final admitted; primary " \
  "print_args, gem5: no ports in the header or the state lines, no unit in stats.txt"
report "$(grep -c '"e"' "$OUT/print_args.spike.timeline") events" "0 events" \
  "print_args, Spike: a hand-written model is only executed on Spike, never issued, so its file is the header alone"
report "$(unchanged print_args)" "stats same ($(measured print_args | wc -l) lines), gem5 output same, Spike output same" \
  "print_args: recording changes nothing gem5 measures or prints, nor what Spike prints"

echo "-- print_args on gem5 with --units 2: one instance per unit, each its own file"
printf 'vcix_timeline: %s\n' "$OUT/units.timeline" > "$OUT/units.yml"
timeout 300 "$GEM5" -d "$OUT/m5out-units" "$REPO/examples/gem5_se.py" --model "$BUILD/libprint_args.so" --config "$OUT/units.yml" \
  --units 2 --max-ticks 1000000000 "$OUT/vcix" > "$OUT/units.gem5.log" 2>&1
report "exit $?, $(check json "$OUT/units.timeline"); $(check json "$OUT/units.timeline.1"); commits $(( $(grep -c '"e":"commit"' "$OUT/units.timeline") + $(grep -c '"e":"commit"' "$OUT/units.timeline.1") ))" \
  "exit 0, $(lines "$OUT/units.timeline") of $(lines "$OUT/units.timeline") lines JSON; $(lines "$OUT/units.timeline.1") of $(lines "$OUT/units.timeline.1") lines JSON; commits $(grep -c '^\[commit ' "$OUT/units.gem5.log")" \
  "print_args, gem5 --units 2: the second instance writes units.timeline.1, both files JSON, their commits gem5's"

[ "$failed" = 0 ] || echo "---- outputs in $OUT"
exit $failed
