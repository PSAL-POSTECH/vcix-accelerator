#!/usr/bin/env bash
# The functional face of the tpu example, on Spike. Each program prints what its instructions left in the
# registers, the scratchpad and memory; from-old-spike.sha256 is what the Spike these units came from printed
# (riscv-isa-sim 9f555b4, the units built in), so a PASS says the model does what that Spike did.
# Called by tests/tpu/run.sh. Usage: tests/tpu/functional/run.sh <build-dir> <spike> <pk> [old spike]
#   With an old spike, each program is also run on it and the two outputs are compared byte by byte.
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../../../setup/versions.env
source "$HERE/../../../setup/versions.env"
[ $# -ge 3 ] || { sed -n '2,6p' "${BASH_SOURCE[0]}" >&2; exit 2; }
BUILD=$(cd "$1" && pwd) || exit 2
SPIKE=$2 PK=$3 OLD=${4:-}
MODEL="$BUILD/libtpu.so"
OUT="$BUILD/tpu-functional"
for needed in "$MODEL" "$SPIKE" "$PK" ${OLD:+"$OLD"}; do
  [ -e "$needed" ] || { echo "$needed: not found" >&2; exit 2; }
done
[ -d "$TOOLCHAIN_ROOT/bin" ] && PATH="$TOOLCHAIN_ROOT/bin:$PATH"
failed=0
rm -rf "$OUT" && mkdir -p "$OUT" || exit 2

# Where kernels.S is linked, and the range Spike runs in every lane.
KERNEL_BASE=0x2000000
KERNEL_RANGE=2000000:2100000

clang --target=riscv64 -march=rv64gcv_xsfvcp -c "$HERE/kernels.S" -o "$OUT/kernels.o" || { echo "FAIL  kernels.S does not build"; exit 2; }
for program in special systolic cross_lane dma refused; do
  riscv64-unknown-elf-gcc -O1 -Wall -Werror -static -march=rv64gc -mabi=lp64d -Wl,--section-start=.kernel=$KERNEL_BASE \
    "$HERE/$program.c" "$OUT/kernels.o" -o "$OUT/$program" || { echo "FAIL  $program.c does not build"; exit 2; }
done

# on_model <name> [--base-path=<dir>] -- <program> [arguments]: standard output to $OUT/<name>.out
on_model() {
  local name=$1 options=(); shift
  while [ "$1" != -- ]; do options+=("$1"); shift; done; shift
  timeout 900 "$SPIKE" --extlib="$MODEL" --isa=rv64gcv_zfh_xvcixaccel --machine-config="$HERE/machine.yml" \
    --kernel-addr=$KERNEL_RANGE "${options[@]}" "$PK" "$OUT/$1" "${@:2}" < /dev/null > "$OUT/$name.out" 2> "$OUT/$name.err"
}
# The same machine, as the old Spike takes it.
on_old() {
  local name=$1 options=(); shift
  while [ "$1" != -- ]; do options+=("$1"); shift; done; shift
  timeout 900 "$OLD" --isa=rv64gcv_zfh --varch=vlen:256,elen:64 --vectorlane-size=4 --scratchpad-size=$((512 * 1024)) \
    --scratchpad-base-vaddr=$((0xD0000000)) --kernel-addr=$KERNEL_RANGE "${options[@]}" "$PK" "$OUT/$1" "${@:2}" \
    < /dev/null > "$OUT/$name.old.out" 2> "$OUT/$name.old.err"
}
# The files an indirect transfer wrote under <dir>, in the order they were written, as one.
indices() {
  local n=0
  : > "$2"
  while [ -e "$1/indirect_access/indirect_index$n.raw" ]; do cat "$1/indirect_access/indirect_index$n.raw" >> "$2"; n=$((n + 1)); done
}

# same <name> <expected.sha256> <what> [--base-path] -- <program> [arguments]
same() {
  local name=$1 expected=$2 what=$3 base=() rc; shift 3
  if [ "$1" = --base-path ]; then mkdir -p "$OUT/$name.base/indirect_access"; base=("--base-path=$OUT/$name.base"); shift; fi
  shift
  on_model "$name" "${base[@]}" -- "$@"; rc=$?
  [ ${#base[@]} = 0 ] || indices "$OUT/$name.base" "$OUT/$name.indices"
  local sums; sums=$(cd "$OUT" && grep -F "  $name." "$HERE/$expected" | sha256sum --check --quiet 2>&1)
  if [ "$rc" = 0 ] && [ -z "$sums" ] && grep -Fq "  $name.out" "$HERE/$expected"; then printf 'PASS'; else printf 'FAIL'; failed=1; fi
  echo "  $what (exit $rc, $(wc -c < "$OUT/$name.out") bytes${sums:+, $sums})"
  [ -n "$OLD" ] && [ "$expected" = from-old-spike.sha256 ] || return 0
  if [ ${#base[@]} != 0 ]; then mkdir -p "$OUT/$name.old.base/indirect_access"; base=("--base-path=$OUT/$name.old.base"); fi
  on_old "$name" "${base[@]}" -- "$@"; rc=$?
  [ ${#base[@]} = 0 ] || indices "$OUT/$name.old.base" "$OUT/$name.old.indices"
  if [ "$rc" = 0 ] && cmp -s "$OUT/$name.out" "$OUT/$name.old.out" &&
     { [ ${#base[@]} = 0 ] || cmp -s "$OUT/$name.indices" "$OUT/$name.old.indices"; }; then printf 'PASS'; else printf 'FAIL'; failed=1; fi
  echo "  the old Spike prints the same (exit $rc)"
}

echo "-- as the old Spike did it"
same special from-old-spike.sha256 "the seven special functions: every half, 4096 singles a lane, 1024 doubles a lane for vlog and vatan" -- special
same systolic from-old-spike.sha256 "the systolic array: weights that slide, pops in pieces, every element width" -- systolic
same lanes from-old-spike.sha256 "vlane_idx at every element width" -- cross_lane lanes
same cross from-old-spike.sha256 "the cross-lane unit: transpose, all-gather and broadcast" -- cross_lane plain 4 5 8
same permute from-old-spike.sha256 "the cross-lane unit: permute, with its pattern" -- cross_lane pattern 16
same dma from-old-spike.sha256 "the DMA: 300 descriptors, an mvin and an mvout each, and the indices of the indirect ones" --base-path -- dma 0 300

# The old Spike decodes four operations of the cross-lane unit; the rest were checked once against its source.
echo "-- as the old unit's source did it"
same cross-all from-model.sha256 "the cross-lane unit: every operation without a pattern" -- cross_lane plain 1 2 4 5 6 8 9 10 12 13 14 16 17 18 20 21 22
same permute-all from-model.sha256 "the cross-lane unit: every operation with a pattern" -- cross_lane pattern 1 2 4 5 6 8 9 10 12 13 14 16 17 18 20 21 22

# refused <which> <exit> <text>: the run ends there, with this on standard error.
refused() {
  local rc
  on_model "refused-$1" -- refused "$1"; rc=$?
  if [ "$rc" = "$2" ] && grep -Fq -- "$3" "$OUT/refused-$1.err" && ! grep -Fq "it ran" "$OUT/refused-$1.out"; then printf 'PASS'; else printf 'FAIL'; failed=1; fi
  echo "  refused: $3 (exit $rc)"
}
echo "-- what the model does not carry out"
refused doubles 1 "tpu: vexp: an element of 64 bits is not supported"
refused pop 1 "tpu: systolic pop: 1 elements asked, 0 computed"
refused input 1 "tpu: systolic input push: no weight was pushed before it"
refused operation 1 "tpu: cross-lane push: 0 is not an operation of the unit"
refused overflow 200 "MVIN ERROR: Scratchpad address overflow: 0xd0080000"
refused indices 1 "tpu: mvin: an indirect transfer writes its indices under --base-path, and none was given"

exit $failed
