#!/usr/bin/env bash
# Two model libraries whose model classes have the same global name, loaded into
# one process as a simulator loads them, must each answer with a table of their
# own. Runs on the host: no simulator is involved.
# Usage: tests/two_models/run.sh [build-dir [spike [pk [gem5.opt]]]]
# Each defaults to what setup/setup.sh produced; see scripts/sim.sh.
set -uo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$HERE/../../scripts/sim.sh"
failed=0

echo "built with hidden visibility, as this repository builds a model:"
"$BUILD/load_two" "$BUILD/libsame_name_1.so" "$BUILD/libsame_name_2.so" || failed=1

echo "built with the compiler's default visibility:"
"$BUILD/load_two" "$BUILD/libsame_name_default_1.so" "$BUILD/libsame_name_default_2.so" || failed=1

exit $failed
