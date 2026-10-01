#!/usr/bin/env bash
# Both simulators hand a model the same machine description, and it is the one
# include/vcix_accel.h describes at vcix_config: text as written, YAML null and
# non-scalars absent, an empty file an empty description, the first document.
# For each <name>.yml the model's report must equal <name>.expected on both.
# Usage: tests/config/run.sh [build-dir [spike [pk [gem5.opt]]]]
# Each defaults to what setup/setup.sh produced; see scripts/sim.sh.
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/../../scripts/sim.sh"
MODEL="$BUILD/libshows_config.so"
failed=0

rv_program "$HERE/nothing.S" "$BUILD/nothing" || { echo "FAIL  nothing.S does not build"; exit 2; }

# report <ok: 0|1> <what>
report() {
  if [ "$1" = 1 ]; then echo "PASS  $2"; else echo "FAIL  $2"; failed=1; fi
}

# handed <simulator> <name> <exit-code>: the run succeeded and the model's
# report, shows_config.cc's "[config] ..." lines, is exactly <name>.expected.
handed() {
  local sim=$1 name=$2 rc=$3 ok=0
  local log="$BUILD/config-$name.$sim.log"
  grep '^\[config\]' "$log" > "$log.report"
  [ "$rc" = 0 ] && diff "$HERE/$name.expected" "$log.report" > "$log.diff" && ok=1
  report $ok "$sim $name.yml (exit $rc)"
  [ "$ok" = 1 ] || cat "$log.diff"
}

for name in values empty two_documents; do
  spike_run "$MODEL" "$HERE/$name.yml" "$BUILD/nothing" > "$BUILD/config-$name.spike.log" 2>&1
  handed spike $name $?

  gem5_run "$BUILD/m5out-config-$name" "$MODEL" "$HERE/$name.yml" "$BUILD/nothing" > "$BUILD/config-$name.gem5.log" 2>&1
  handed gem5 $name $?
done

# A top level that is not a mapping is refused before the model is configured.
# gem5 only: the Spike adapter does not refuse every such file yet.
gem5_run "$BUILD/m5out-config-not_a_mapping" "$MODEL" "$HERE/not_a_mapping.yml" "$BUILD/nothing" \
  > "$BUILD/config-not_a_mapping.gem5.log" 2>&1; rc=$?
configured=$(grep -c '^\[config\]' "$BUILD/config-not_a_mapping.gem5.log")
ok=0; [ "$rc" = 1 ] && [ "$configured" = 0 ] && ok=1
report $ok "gem5 refuses not_a_mapping.yml (exit $rc, model configured with $configured values)"

exit $failed
