#!/usr/bin/env bash
# What the other tests rely on, tested by itself: a program that does not build
# fails its test and leaves no ELF behind for a later step to run, a run that
# dies some other way is not taken for an illegal instruction, and both
# simulators exit with the program's exit code.
# Usage: tests/harness/run.sh [build-dir [spike [pk [gem5.opt]]]]
# Each defaults to what setup/setup.sh produced; see scripts/sim.sh.
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/../../scripts/sim.sh"
MODEL="$BUILD/libowns_one.so"  # any model will do: these programs run no accelerator instruction
failed=0

# report <ok: 0|1> <what>
report() {
  if [ "$1" = 1 ]; then echo "PASS  $2"; else echo "FAIL  $2"; failed=1; fi
}

# An earlier run left an object and an ELF under the same name; then the
# assembler cannot run at all (a clang that fails cleanly removes its own
# output; one that is missing or broken does not). The stale object must not be
# linked, nor the stale ELF kept.
rv_program "$HERE/returns3.S" "$BUILD/stale" || { echo "FAIL  returns3.S does not build"; exit 2; }
mkdir -p "$BUILD/no-assembler"
printf '#!/bin/sh\nexit 1\n' > "$BUILD/no-assembler/clang"
chmod +x "$BUILD/no-assembler/clang"
PATH="$BUILD/no-assembler:$PATH" rv_program "$HERE/returns3.S" "$BUILD/stale"; rc=$?
ok=0; [ "$rc" != 0 ] && [ ! -e "$BUILD/stale" ] && ok=1
report $ok "a program whose assembler fails does not build and leaves no ELF (exit $rc)"

# A segfault exits non-zero on both simulators, as an illegal instruction does.
rv_program "$HERE/segfault.S" "$BUILD/segfault" || { echo "FAIL  segfault.S does not build"; exit 2; }

spike_run "$MODEL" "" "$BUILD/segfault" > "$BUILD/segfault.spike.log" 2>&1; rc=$?
ok=0; [ "$rc" != 0 ] && ! spike_illegal "$BUILD/segfault.spike.log" && ok=1
report $ok "spike: a segfault is not an illegal instruction (exit $rc)"

gem5_run "$BUILD/m5out-segfault" "$MODEL" "" "$BUILD/segfault" > "$BUILD/segfault.gem5.log" 2>&1; rc=$?
ok=0; [ "$rc" != 0 ] && ! gem5_illegal "$BUILD/segfault.gem5.log" && ok=1
report $ok "gem5:  a segfault is not an illegal instruction (exit $rc)"

# The program's exit code is the simulator's, and a gem5 run that ends before
# the program does is a failure whatever the program would have returned.
rv_program "$HERE/returns3.S" "$BUILD/returns3" || { echo "FAIL  returns3.S does not build"; exit 2; }
rv_program "$HERE/../ownership/owned.S" "$BUILD/returns0" || { echo "FAIL  owned.S does not build"; exit 2; }

spike_run "$MODEL" "" "$BUILD/returns3" > "$BUILD/returns3.spike.log" 2>&1; rc=$?
ok=0; [ "$rc" = 3 ] && ok=1
report $ok "spike exits with the program's exit code (exit $rc, program returns 3)"

gem5_run "$BUILD/m5out-returns3" "$MODEL" "" "$BUILD/returns3" > "$BUILD/returns3.gem5.log" 2>&1; rc=$?
ok=0; [ "$rc" = 3 ] && ok=1
report $ok "gem5  exits with the program's exit code (exit $rc, program returns 3)"

gem5_run "$BUILD/m5out-cut-short" "$MODEL" "" "$BUILD/returns0" --max-ticks 1000000 > "$BUILD/cut-short.gem5.log" 2>&1; rc=$?
ok=0; [ "$rc" = 1 ] && ok=1
report $ok "gem5  fails when the simulation ends before the program does (exit $rc, program returns 0)"

exit $failed
