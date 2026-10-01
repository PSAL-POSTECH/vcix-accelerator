#!/usr/bin/env bash
# tools/timing_probe is a caller of the model table like the adapters, and is
# held to the same contract (include/vcix_accel.h): it refuses another ABI
# version and an instruction the model does not own, takes a table whose
# configure and reset are NULL, and rejects arguments that are not numbers.
# Runs on the host: no simulator is involved.
# Usage: tests/probe/run.sh [build-dir [spike [pk [gem5.opt]]]]
# Each defaults to what setup/setup.sh produced; see scripts/sim.sh.
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/../../scripts/sim.sh"
PROBE="$BUILD/timing_probe"
OWNS_ONE="$BUILD/libowns_one.so"
OWNED_INSN=062541db    # as in tests/ownership
UNOWNED_INSN=0a2541db
failed=0

# probe <what> <want exit code> <want issued instructions> <timing_probe arguments...>
# An issued instruction is one line of the probe's own: "insn <n>: issued at ...".
probe() {
  local what=$1 want_rc=$2 want_issued=$3 rc issued
  shift 3
  "$PROBE" "$@" > "$BUILD/probe.log" 2>&1; rc=$?
  issued=$(grep -c '^insn [0-9]*: issued at ' "$BUILD/probe.log")
  if [ "$rc" = "$want_rc" ] && [ "$issued" = "$want_issued" ]; then echo "PASS  $what (exit $rc, issued $issued)"
  else echo "FAIL  $what (exit $rc, want $want_rc; issued $issued, want $want_issued)"; failed=1; fi
}

probe "an owned instruction, three times"            0 3 "$OWNS_ONE" $OWNED_INSN 3
probe "the count defaults to four"                   0 4 "$OWNS_ONE" $OWNED_INSN
probe "the instruction may be written with 0x"       0 1 "$OWNS_ONE" 0x$OWNED_INSN 1 3
probe "a table with configure and reset NULL"        0 2 "$BUILD/libno_configure.so" $OWNED_INSN 2

probe "an instruction the model does not own"        1 0 "$OWNS_ONE" $UNOWNED_INSN 3
probe "a model of another ABI version"               1 0 "$BUILD/libother_abi.so" $OWNED_INSN 3
probe "a library that is not there"                  1 0 "$BUILD/libno_such_model.so" $OWNED_INSN 3

probe "no instruction given"                         2 0 "$OWNS_ONE"
probe "too many arguments"                           2 0 "$OWNS_ONE" $OWNED_INSN 1 0 0
for bad in zz 0x "" 1ffffffff -1 "62541db "; do
  probe "instruction '$bad' is refused"               2 0 "$OWNS_ONE" "$bad" 3
done
for bad in abc "" -1 3x 1e3 99999999999999999999; do
  probe "count '$bad' is refused"                     2 0 "$OWNS_ONE" $OWNED_INSN "$bad"
done
for bad in 40 4 -4 x "" 1.5; do
  probe "lmul-log2 '$bad' is refused"                 2 0 "$OWNS_ONE" $OWNED_INSN 3 "$bad"
done

exit $failed
