#!/usr/bin/env bash
# Both simulators must hand a model the same machine description (vcix_config in
# include/vcix_accel.h), and a malformed number must stop the run.
# Usage: tests/config/run.sh [build-dir [spike [pk [gem5.opt]]]]
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

# handed <simulator> <name> <exit-code>: the model's report equals <name>.expected.
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

# gem5 only: the Spike adapter does not refuse a non-mapping top level yet.
gem5_run "$BUILD/m5out-config-not_a_mapping" "$MODEL" "$HERE/not_a_mapping.yml" "$BUILD/nothing" \
  > "$BUILD/config-not_a_mapping.gem5.log" 2>&1; rc=$?
configured=$(grep -c '^\[config\]' "$BUILD/config-not_a_mapping.gem5.log")
ok=0; [ "$rc" = 1 ] && [ "$configured" = 0 ] && ok=1
report $ok "gem5 refuses not_a_mapping.yml (exit $rc, model configured with $configured values)"

# number <simulator> <value> <exit-code> <the number, or "" when the run must stop>
number() {
  local sim=$1 written=$2 rc=$3 want=$4 ok=0 got said text
  local log="$BUILD/config-number.$sim.log"
  got=$(grep -c '^\[number\] ' "$log")
  if [ -n "$want" ]; then
    [ "$rc" = 0 ] && grep -Fxq "[number] count = $want" "$log" && ok=1
    report $ok "$sim count: $written is $want (exit $rc)"
  else
    text=${written#\"}; text=${text%\"}   # the cases below quote with "..." only, and escape nothing
    said=$(grep -Fc "shows_config: machine description: count: '$text' " "$log")
    [ "$rc" = 1 ] && [ "$got" = 0 ] && [ "$said" = 1 ] && ok=1
    report $ok "$sim count: $written stops the run (exit $rc, reported $said, number handed on $got)"
  fi
}

# Each value as it is written after "count: ", and what it must be read as.
NUMBERS=(
  '8|8' '0|0' '"8"|8' '18446744073709551615|18446744073709551615' '~|5'
  'abc|' 'true|' '""|' '-1|' '+1|' '1e3|' '7.9|' '010|' '8 cycles|' '0x10|' '" 8"|' '18446744073709551616|'
)
for case in "${NUMBERS[@]}"; do
  written=${case%|*}; want=${case##*|}
  printf 'count: %s\n' "$written" > "$BUILD/config-number.yml"

  spike_run "$MODEL" "$BUILD/config-number.yml" "$BUILD/nothing" > "$BUILD/config-number.spike.log" 2>&1
  number spike "$written" $? "$want"

  gem5_run "$BUILD/m5out-config-number" "$MODEL" "$BUILD/config-number.yml" "$BUILD/nothing" \
    > "$BUILD/config-number.gem5.log" 2>&1
  number gem5 "$written" $? "$want"
done

exit $failed
