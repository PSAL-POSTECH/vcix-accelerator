#!/usr/bin/env bash
# What the other tests rely on, tested by itself: a program that does not build
# fails its test and leaves no ELF behind for a later step to run.
# Usage: tests/harness/run.sh [build-dir [spike [pk [gem5.opt]]]]
# Each defaults to what setup/setup.sh produced; see scripts/sim.sh.
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/../../scripts/sim.sh"
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

exit $failed
