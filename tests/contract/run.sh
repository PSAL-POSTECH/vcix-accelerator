#!/usr/bin/env bash
# The rules of the interface, one section each: ownership, the machine description,
# the model table, processor state, and the harness the other tests stand on.
# Usage: tests/contract/run.sh [build-dir [spike [pk [gem5.opt]]]]
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/../../scripts/sim.sh"
OWNS_ONE="$BUILD/libowns_one.so"
OWNED_INSN=062541db    # sf.vc.x 0x1, 0x2, 0x3, a0
UNOWNED_INSN=0a2541db  # sf.vc.x 0x2, 0x2, 0x3, a0
failed=0

# report <ok: 0|1> <what>
report() {
  if [ "$1" = 1 ]; then echo "PASS  $2"; else echo "FAIL  $2"; failed=1; fi
}

# program <name> [link options]: $HERE/<name>.S built into $BUILD/<name>; the test ends if it does not build.
program() {
  rv_program "$HERE/$1.S" "$BUILD/$1" "${@:2}" || { echo "FAIL  $1.S does not build"; exit 2; }
}
for name in owned unowned nothing segfault returns3; do program $name; done
program status -nostdlib -Wl,-N,-Ttext=0x80000000,--no-warn-rwx-segments

echo "-- ownership"

# calls <log> <entry>: owns_one's reports of <entry> for the owned instruction.
calls() { grep -Fxc "[model] $2 $OWNED_INSN" "$1"; }

# owned_once <simulator> <program> <exit-code> <want-ok: 0|1>
# Spike: one execute. gem5: one commit, accept and issue at least once (a squashed
# instruction is asked again). Any other call the model reports fails the check.
owned_once() {
  local sim=$1 prog=$2 rc=$3 want_ok=$4
  local log="$BUILD/$prog.$sim.log" ok=1 ended seen other
  local all execute accept issue commit
  all=$(grep -c '^\[model\]' "$log")
  execute=$(calls "$log" execute); accept=$(calls "$log" accept)
  issue=$(calls "$log" issue); commit=$(calls "$log" commit)
  if [ "$sim" = spike ]; then
    seen="execute $execute"; other=$((all - execute))
    [ "$execute" = 1 ] || ok=0
  else
    seen="accept $accept, issue $issue, commit $commit"; other=$((all - accept - issue - commit))
    [ "$commit" = 1 ] && [ "$accept" -ge 1 ] && [ "$issue" -ge 1 ] || ok=0
  fi
  [ "$other" = 0 ] || ok=0
  if [ "$want_ok" = 1 ]; then
    ended="exit $rc"
    [ "$rc" = 0 ] || ok=0
  elif [ "$rc" != 0 ] && "${sim}_illegal" "$log" "$UNOWNED_INSN"; then
    ended="illegal instruction $UNOWNED_INSN, exit $rc"
  else
    ended="not as an illegal instruction $UNOWNED_INSN, exit $rc"
    ok=0
  fi
  report $ok "$sim $prog ($ended; owned: $seen; other calls: $other)"
}

for prog in owned unowned; do
  want=1; [ "$prog" = unowned ] && want=0

  spike_run "$OWNS_ONE" "" "$BUILD/$prog" > "$BUILD/$prog.spike.log" 2>&1
  owned_once spike $prog $? $want

  gem5_run "$BUILD/m5out-$prog" "$OWNS_ONE" "" "$BUILD/$prog" > "$BUILD/$prog.gem5.log" 2>&1
  owned_once gem5 $prog $? $want
done

echo "-- machine description"
SHOWS_CONFIG="$BUILD/libshows_config.so"

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
  spike_run "$SHOWS_CONFIG" "$HERE/$name.yml" "$BUILD/nothing" > "$BUILD/config-$name.spike.log" 2>&1
  handed spike $name $?

  gem5_run "$BUILD/m5out-config-$name" "$SHOWS_CONFIG" "$HERE/$name.yml" "$BUILD/nothing" > "$BUILD/config-$name.gem5.log" 2>&1
  handed gem5 $name $?
done

# gem5 only: the Spike adapter does not refuse a non-mapping top level yet.
gem5_run "$BUILD/m5out-config-not_a_mapping" "$SHOWS_CONFIG" "$HERE/not_a_mapping.yml" "$BUILD/nothing" \
  > "$BUILD/config-not_a_mapping.gem5.log" 2>&1; rc=$?
configured=$(grep -c '^\[config\]' "$BUILD/config-not_a_mapping.gem5.log")
ok=0; [ "$rc" = 1 ] && [ "$configured" = 0 ] && ok=1
report $ok "gem5 refuses not_a_mapping.yml (exit $rc, model configured with $configured values)"

# number <simulator> <key> <value> <exit-code> <what the model reads, or "" when the run must stop>
number() {
  local sim=$1 key=$2 written=$3 rc=$4 want=$5 ok=0 got said
  local log="$BUILD/config-number.$sim.log"
  got=$(grep -c "^\[number\] $key = " "$log")
  if [ -n "$want" ]; then
    [ "$rc" = 0 ] && grep -Fxq "[number] $key = $want" "$log" && ok=1
    report $ok "$sim $key: $written is $want (exit $rc)"
  else
    said=$(grep -Fc "shows_config: machine description: $key: '$written' " "$log")
    [ "$rc" = 1 ] && [ "$got" = 0 ] && [ "$said" = 1 ] && ok=1
    report $ok "$sim $key: $written stops the run (exit $rc, reported $said, number handed on $got)"
  fi
}

# key|value as written|what it is read as, nothing when the run must stop. count is decimal:
# fallback 5; not a number, negative, trailing text, leading zero, too large. base is hex:
# fallback 0x1000; no 0x, no digits, not hex digits, a capital X, too large.
NUMBERS=('count|8|8' 'count|~|5' 'count|abc|' 'count|-1|' 'count|8 cycles|' 'count|010|' 'count|18446744073709551616|'
         'base|0x80001000|0x80001000' 'base|0xffffffffffffffff|0xffffffffffffffff' 'base|~|0x1000' 'base|80001000|'
         'base|0x|' 'base|0x80zz|' 'base|0X80|' 'base|0x10000000000000000|')
for case in "${NUMBERS[@]}"; do
  IFS='|' read -r key written want <<< "$case"
  printf '%s: %s\n' "$key" "$written" > "$BUILD/config-number.yml"

  spike_run "$SHOWS_CONFIG" "$BUILD/config-number.yml" "$BUILD/nothing" > "$BUILD/config-number.spike.log" 2>&1
  number spike "$key" "$written" $? "$want"

  gem5_run "$BUILD/m5out-config-number" "$SHOWS_CONFIG" "$BUILD/config-number.yml" "$BUILD/nothing" \
    > "$BUILD/config-number.gem5.log" 2>&1
  number gem5 "$key" "$written" $? "$want"
done

echo "-- model table"

# Two libraries whose model classes share a global name; load_two prints its own PASS/FAIL lines.
echo "built with hidden visibility, as this repository builds a model:"
"$BUILD/load_two" "$BUILD/libsame_name_1.so" "$BUILD/libsame_name_2.so" || failed=1
echo "built with the compiler's default visibility:"
"$BUILD/load_two" "$BUILD/libsame_name_default_1.so" "$BUILD/libsame_name_default_2.so" || failed=1

# probe <what> <want exit code> <want issued instructions> <timing_probe arguments...>
probe() {
  local what=$1 want_rc=$2 want_issued=$3 rc issued ok=0
  shift 3
  "$BUILD/timing_probe" "$@" > "$BUILD/probe.log" 2>&1; rc=$?
  issued=$(grep -c '^insn [0-9]*: issued at ' "$BUILD/probe.log")
  [ "$rc" = "$want_rc" ] && [ "$issued" = "$want_issued" ] && ok=1
  report $ok "probe: $what (exit $rc, want $want_rc; issued $issued, want $want_issued)"
}

probe "an owned instruction, three times"      0 3 "$OWNS_ONE" $OWNED_INSN 3
probe "a table with configure and reset NULL"  0 2 "$BUILD/libno_configure.so" $OWNED_INSN 2
probe "an instruction the model does not own"  1 0 "$OWNS_ONE" $UNOWNED_INSN 3
probe "a model of another ABI version"         1 0 "$BUILD/libother_abi.so" $OWNED_INSN 3
probe "instruction 'zz' is refused"            2 0 "$OWNS_ONE" zz 3
probe "count 'abc' is refused"                 2 0 "$OWNS_ONE" $OWNED_INSN abc
probe "lmul-log2 '40' is refused"              2 0 "$OWNS_ONE" $OWNED_INSN 3 40

# A model built hidden, as CMake builds one, calls its own f16_to_f32 and not the
# simulator's, and its allocations survive the simulator's allocator.
for sim in spike gem5; do
  log="$BUILD/own_symbols.$sim.log"
  if [ $sim = spike ]; then spike_run "$BUILD/libown_symbols.so" "" "$BUILD/owned" > "$log" 2>&1
  else gem5_run "$BUILD/m5out-own_symbols" "$BUILD/libown_symbols.so" "" "$BUILD/owned" > "$log" 2>&1; fi; rc=$?
  own=$(grep -Fxc '[model] f16_to_f32(0x3c00) = 0x0badc0de' "$log"); allocated=$(grep -c '^\[model\] allocated, ' "$log")
  ok=0; [ "$rc" = 0 ] && [ "$own" = 1 ] && [ "$allocated" -ge 2 ] && ok=1
  report $ok "$sim: a model's own definition is the one it calls (exit $rc, own $own of 1, allocated $allocated times)"
done

# gem5 only: the Spike adapter does not check configure or the table yet.
gem5_run "$BUILD/m5out-no_configure" "$BUILD/libno_configure.so" "" "$BUILD/owned" > "$BUILD/no_configure.gem5.log" 2>&1; rc=$?
ok=0; [ "$rc" = 0 ] && ok=1
report $ok "gem5 takes a table with configure and reset NULL (exit $rc)"

gem5_run "$BUILD/m5out-null_table" "$BUILD/libnull_table.so" "" "$BUILD/owned" > "$BUILD/null_table.gem5.log" 2>&1; rc=$?
said=$(grep -c 'fatal: .*libnull_table.so: vcix_accel_model() returned no table$' "$BUILD/null_table.gem5.log")
ok=0; [ "$rc" = 1 ] && [ "$said" = 1 ] && ok=1
report $ok "gem5 refuses a library that hands over no table (exit $rc, said so $said of 1)"

echo "-- processor state"

# gem5 only, on bare metal: status.S ends with one bit per failed check. With FS off the
# model must see no commit of sf.vc.v.fv (2c2552db) and one of sf.vc.v.xv (2825c1db).
gem5_bare_run "$BUILD/m5out-status" "$BUILD/libprint_args.so" "$BUILD/status" > "$BUILD/status.gem5.log" 2>&1; rc=$?
float=$(grep -c '^\[commit \] insn=2c2552db ' "$BUILD/status.gem5.log"); integer=$(grep -c '^\[commit \] insn=2825c1db ' "$BUILD/status.gem5.log")
ok=0; [ "$rc" = 0 ] && [ "$float" = 0 ] && [ "$integer" = 1 ] && ok=1
report $ok "gem5: writing vd dirties VS, and FS off refuses only a form that reads f[rs1] (exit $rc; commits: fv $float of 0, xv $integer of 1)"

echo "-- harness"

# An earlier run left an ELF under the same name and the assembler now fails:
# the stale ELF must not survive to be run.
rv_program "$HERE/returns3.S" "$BUILD/stale" || { echo "FAIL  returns3.S does not build"; exit 2; }
mkdir -p "$BUILD/no-assembler"
printf '#!/bin/sh\nexit 1\n' > "$BUILD/no-assembler/clang"
chmod +x "$BUILD/no-assembler/clang"
PATH="$BUILD/no-assembler:$PATH" rv_program "$HERE/returns3.S" "$BUILD/stale"; rc=$?
ok=0; [ "$rc" != 0 ] && [ ! -e "$BUILD/stale" ] && ok=1
report $ok "a program whose assembler fails does not build and leaves no ELF (exit $rc)"

# A segfault exits non-zero on both simulators, as an illegal instruction does.
spike_run "$OWNS_ONE" "" "$BUILD/segfault" > "$BUILD/segfault.spike.log" 2>&1; rc=$?
ok=0; [ "$rc" != 0 ] && ! spike_illegal "$BUILD/segfault.spike.log" && ok=1
report $ok "spike: a segfault is not an illegal instruction (exit $rc)"

gem5_run "$BUILD/m5out-segfault" "$OWNS_ONE" "" "$BUILD/segfault" > "$BUILD/segfault.gem5.log" 2>&1; rc=$?
ok=0; [ "$rc" != 0 ] && ! gem5_illegal "$BUILD/segfault.gem5.log" && ok=1
report $ok "gem5:  a segfault is not an illegal instruction (exit $rc)"

# The program's exit code is the simulator's; a gem5 run cut short fails whatever the program returns.
spike_run "$OWNS_ONE" "" "$BUILD/returns3" > "$BUILD/returns3.spike.log" 2>&1; rc=$?
ok=0; [ "$rc" = 3 ] && ok=1
report $ok "spike exits with the program's exit code (exit $rc, program returns 3)"

gem5_run "$BUILD/m5out-returns3" "$OWNS_ONE" "" "$BUILD/returns3" > "$BUILD/returns3.gem5.log" 2>&1; rc=$?
ok=0; [ "$rc" = 3 ] && ok=1
report $ok "gem5  exits with the program's exit code (exit $rc, program returns 3)"

gem5_run "$BUILD/m5out-cut-short" "$OWNS_ONE" "" "$BUILD/owned" --max-ticks 1000000 > "$BUILD/cut-short.gem5.log" 2>&1; rc=$?
ok=0; [ "$rc" = 1 ] && ok=1
report $ok "gem5  fails when the simulation ends before the program does (exit $rc, program returns 0)"

exit $failed
