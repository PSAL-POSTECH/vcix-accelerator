#!/usr/bin/env bash
# Runs vcix.S with the print_args model on Spike, then on gem5.
# Usage: examples/print_args/run.sh <build-dir> <spike> <pk> <gem5.opt>
# clang assembles the sf.vc.* mnemonics; the GNU assembler here does not know them.
set -euo pipefail
BUILD=$(realpath "$1"); SPIKE=$2; PK=$3; GEM5=$4
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
MODEL="$BUILD/libprint_args.so"
CONFIG="$HERE/machine.yml"

clang --target=riscv64 -march=rv64gcv_xsfvcp -c "$HERE/vcix.S" -o "$BUILD/vcix.o"
riscv64-unknown-elf-gcc -static "$BUILD/vcix.o" -o "$BUILD/vcix"

echo "== spike: functional face"
VCIX_ACCEL_MODEL="$MODEL" VCIX_ACCEL_CONFIG="$CONFIG" "$SPIKE" --extlib="$BUILD/libvcix_spike.so" \
  --isa=rv64gcv_zfh_xvcixaccel --varch=vlen:256,elen:64 "$PK" "$BUILD/vcix"

echo "== gem5: timing face"
"$GEM5" -d "$BUILD/m5out" "$HERE/../gem5_se.py" --model "$MODEL" --config "$CONFIG" "$BUILD/vcix" 2>/dev/null \
  | grep -E '^\[config|^\[issue|^\[commit|^exit:'
