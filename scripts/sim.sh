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
BUILD="$(cd "$BUILD" && pwd)" || { echo "$BUILD: cannot resolve the build directory" >&2; exit 2; }

# The toolchain the setup unpacked, when there is one; otherwise clang and
# riscv64-unknown-elf-gcc come from the caller's PATH.
[ -d "$TOOLCHAIN_ROOT/bin" ] && PATH="$TOOLCHAIN_ROOT/bin:$PATH"

# rv_program <source.S> <elf> [link options]: clang assembles (sf.vc.* mnemonics), gcc links.
# Earlier output is removed first, so a failed build leaves no ELF to run.
rv_program() {
  rm -f "$2" "$2.o" || return
  clang --target=riscv64 -march=rv64gcv_xsfvcp -c "$1" -o "$2.o" || return
  riscv64-unknown-elf-gcc -static "${@:3}" "$2.o" -o "$2" || return
}

# spike_run <model.so> <machine.yml, or ""> <elf>
spike_run() {
  local settings=("VCIX_ACCEL_MODEL=$1")
  [ -n "$2" ] && settings+=("VCIX_ACCEL_CONFIG=$2")
  env "${settings[@]}" "$SPIKE" --extlib="$BUILD/libvcix_spike.so" \
    --isa=rv64gcv_zfh_xvcixaccel --varch=vlen:256,elen:64 "$PK" "$3"
}

# gem5_run <output-dir> <model.so> <machine.yml, or ""> <elf> [fixture options]
gem5_run() {
  local description=()
  [ -n "$3" ] && description=(--config "$3")
  "$GEM5" -d "$1" "$REPO/examples/gem5_se.py" --model "$2" "${description[@]}" "${@:5}" "$4"
}

# gem5_bare_run <output-dir> <model.so> <elf>: bare metal in M mode, where a program can write mstatus.
gem5_bare_run() {
  "$GEM5" -d "$1" "$REPO/tests/contract/gem5_bare.py" --model "$2" "$3"
}

# Whether a run ended as an illegal instruction; the exit code alone cannot say.
# <log> [insn as 8 hex digits]

# spike_illegal: pk's message and the instruction in its trap-frame dump.
spike_illegal() {
  local insn=${2:-'[0-9a-f]{8}'}
  grep -Fxq 'An illegal instruction was executed!' "$1" &&
    grep -Eq "^pc [0-9a-f]+ va/inst 0*$insn sr [0-9a-f]+\$" "$1"
}

# gem5_illegal: the gem5 branch's panic for an instruction no model owns.
gem5_illegal() {
  local insn=${2:-'[0-9a-f]{8}'}
  grep -Eq "panic: Illegal instruction 0x[0-9a-f]*$insn at pc .*: no VCIX accelerator model owns it\$" "$1"
}
