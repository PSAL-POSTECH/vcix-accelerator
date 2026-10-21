#!/usr/bin/env bash
# The fsa example: when each instruction fires on gem5, against FSA's RTL schedule (plan_sim.py); O on Spike, against
# FSA's golden model (golden.py); and the programs both refuse. Usage: tests/fsa/run.sh [build-dir [spike [pk [gem5.opt]]]]
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/../../scripts/sim.sh"
failed=0
MODEL="$BUILD/libfsa.so"
OUT="$BUILD/fsa"
KERNEL_BASE=0x2000000
KERNEL_RANGE=2000000:2100000
rm -rf "$OUT" && mkdir -p "$OUT" || exit 2

report() {
  if [ "$1" = "$2" ]; then echo "PASS  $3 ($1)"; else echo "FAIL  $3 ($1, expected $2)"; failed=1; fi
}

# fires <log> <N>: each instruction's firing cycle: its commit, less its head (ExecutionPlan's conflict-free cycle + 1)
# and the cycle from the end of its head to its commit; issues are counted from the first.
fires() {
  awk -v n="$2" '
    function head(k) { return k == "load_stationary" ? n : k == "attn_score" ? 2 * n + 12 : k == "attn_value" ? 2 * n - 1 : k == "reciprocal" ? 17 : n + 1 }
    /^\[tpu\] issue fsa/ { k = $4; sub(":", "", k); t = $5 == "the" ? 0 : t + $5; kind[i] = k; at[i++] = t }
    /^\[tpu\] commit fsa/ && $4 != "pop:" { printf "%s%d", (j ? " " : ""), at[j] + $5 - head(kind[j]) - 1; j++ }
    END { print "" }' "$1"
}
# on_gem5 <program> <machine.yml>
on_gem5() {
  gem5_run "$OUT/m5out-$1" "$MODEL" "$2" "$OUT/$1" --max-ticks 100000000 > "$OUT/$1.gem5.log" 2>&1
}
# on_spike <program> <machine.yml>: standard output to $OUT/<program>.out, the rest to .err
on_spike() {
  timeout 900 "$SPIKE" --extlib="$MODEL" --isa=rv64gcv_zfh_xvcixaccel --machine-config="$2" --kernel-addr=$KERNEL_RANGE \
    "$PK" "$OUT/$1" < /dev/null > "$OUT/$1.out" 2> "$OUT/$1.err"
}

for program in tiles1 tiles3 tiles2_n16 no_load no_score no_reciprocal two_pops; do
  rv_program "$HERE/$program.S" "$OUT/$program" || { echo "FAIL  $program.S does not build"; exit 2; }
done
clang --target=riscv64 -march=rv64gcv_xsfvcp -c "$HERE/kernels.S" -o "$OUT/kernels.o" || { echo "FAIL  kernels.S does not build"; exit 2; }
for n in 4 8; do
  riscv64-unknown-elf-gcc -O1 -Wall -Werror -static -march=rv64gc -mabi=lp64d -DN=$n -DTILES=3 \
    -Wl,--section-start=.kernel=$KERNEL_BASE "$HERE/attention.c" "$OUT/kernels.o" -o "$OUT/attention_n$n" ||
    { echo "FAIL  attention.c does not build"; exit 2; }
done

echo "-- when each instruction fires, on gem5: plan_sim.py's cycles, from ExecutionPlan.scala"
printf 'tpu_trace: 1\nvpu_num_lanes: 4\n' > "$OUT/n4.yml"
printf 'tpu_trace: 1\nvpu_num_lanes: 16\nvpu_vector_length_bits: 256\n' > "$OUT/n16.yml"
on_gem5 tiles1 "$OUT/n4.yml"
report "$(fires "$OUT/tiles1.gem5.log" 4)" "0 4 24 37 55" \
  "N=4, one tile: load_stationary, attn_score, attn_value, reciprocal, attn_lse_norm"
on_gem5 tiles3 "$OUT/n4.yml"
report "$(fires "$OUT/tiles3.gem5.log" 4)" "0 4 24 33 37 57 66 70 90 103 121" \
  "N=4, three tiles: a tile every 33 cycles, 5N + 13, the next load_stationary waiting for the attn_score two before"
on_gem5 tiles2_n16 "$OUT/n16.yml"
report "$(fires "$OUT/tiles2_n16.gem5.log" 16)" "0 16 60 93 109 153 202 220" \
  "N=16, two tiles: a tile every 93 cycles, 5N + 13"

echo "-- O on Spike: FSA's golden model's, bit for bit"
for n in 4 8; do
  on_spike attention_n$n "$HERE/n$n.yml"
  rc=$?
  report "exit $rc, $(cmp -s "$OUT/attention_n$n.out" "$HERE/attention-n$n.expected" && echo same || echo differs)" "exit 0, same" \
    "N=$n, three tiles of keys: attention.c's O against attention-n$n.expected"
done

echo "-- programs both refuse: gem5 never takes the instruction, Spike ends there with the reason"
refused() {
  on_gem5 "$1" "$OUT/n4.yml"
  local g=$? issued
  issued=$(grep -c '^\[tpu\] issue' "$OUT/$1.gem5.log")
  on_spike "$1" "$HERE/n4.yml"
  local s=$?
  report "gem5 exit $g after $issued issues, Spike exit $s, reason given $(grep -Fxc "tpu: $3" "$OUT/$1.err")" \
    "gem5 exit 1 after $2 issues, Spike exit 1, reason given 1" "$1.S: $3"
}
refused no_load 0 "fsa attn_score: no load_stationary since the last attn_score"
refused no_score 1 "fsa attn_value: no attn_score since the last attn_value"
refused no_reciprocal 3 "fsa attn_lse_norm: no reciprocal and attn_value before it"
refused two_pops 6 "fsa pop: 4 rows asked, 0 normalized"

exit $failed
