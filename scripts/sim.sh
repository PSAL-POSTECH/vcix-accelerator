# shellcheck shell=bash
# Sourced by the run scripts: which simulators to use, and how each one is
# started. Reads the caller's positional arguments
#
#     [build-dir [spike [pk [gem5.opt]]]]
#
# and falls back, for each one not given, to the environment variable (BUILD,
# SPIKE, PK, GEM5) and then to what setup/setup.sh produced. How a model and a
# machine description reach each simulator is written here and nowhere else.

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=../setup/versions.env
source "$REPO/setup/versions.env"

BUILD="${1:-${BUILD:-$REPO/build}}"
SPIKE="${2:-${SPIKE:-$SPIKE_BIN}}"
PK="${3:-${PK:-$PK_BIN}}"
GEM5="${4:-${GEM5:-$GEM5_BIN}}"

for needed in "$BUILD/libvcix_spike.so" "$SPIKE" "$PK" "$GEM5"; do
  [ -e "$needed" ] && continue
  echo "$needed: not found -- run setup/setup.sh, or pass [build-dir [spike [pk [gem5.opt]]]]" >&2
  exit 2
done
BUILD="$(realpath "$BUILD")"

# The toolchain the setup unpacked, when there is one; otherwise clang and
# riscv64-unknown-elf-gcc come from the caller's PATH.
[ -d "$TOOLCHAIN_ROOT/bin" ] && PATH="$TOOLCHAIN_ROOT/bin:$PATH"

# rv_program <source.S> <elf>
# clang assembles the sf.vc.* mnemonics; the GNU assembler does not know them.
rv_program() {
  clang --target=riscv64 -march=rv64gcv_xsfvcp -c "$1" -o "$2.o"
  riscv64-unknown-elf-gcc -static "$2.o" -o "$2"
}

# spike_run <model.so> <machine.yml, or ""> <elf>
spike_run() {
  local settings=("VCIX_ACCEL_MODEL=$1")
  [ -n "$2" ] && settings+=("VCIX_ACCEL_CONFIG=$2")
  env "${settings[@]}" "$SPIKE" --extlib="$BUILD/libvcix_spike.so" \
    --isa=rv64gcv_zfh_xvcixaccel --varch=vlen:256,elen:64 "$PK" "$3"
}

# gem5_run <output-dir> <model.so> <machine.yml, or ""> <elf>
gem5_run() {
  local description=()
  [ -n "$3" ] && description=(--config "$3")
  "$GEM5" -d "$1" "$REPO/examples/gem5_se.py" --model "$2" "${description[@]}" "$4"
}
