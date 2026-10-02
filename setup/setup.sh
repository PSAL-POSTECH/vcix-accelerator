#!/usr/bin/env bash
# Build the environment this repository runs in, from the pins in versions.env:
# the RISC-V toolchain, the proxy kernel, Spike and gem5 -- then this repository.
# Run from anywhere; system.sh (the system packages, as root) comes first.
#
#     ./setup/setup.sh                 everything                 (~15 min at -j 24)
#     ./setup/setup.sh -j 8            cap build parallelism      (default: nproc)
#     ./setup/setup.sh spike repo      only those steps
#
# Steps, in order: toolchain, pk, spike, gem5, repo. Idempotent -- a step whose
# pin is already built does nothing, and an interrupted build resumes.
#
# Everything lands under $VCIX_ENV_ROOT (default /opt/vcix-env); set it, or any
# single location in versions.env, to build somewhere else. The run scripts read
# the same file, so they find what this produced.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(dirname "$HERE")"
# shellcheck source=versions.env
source "$HERE/versions.env"

JOBS=$(nproc)
STEPS=()

while [ $# -gt 0 ]; do
  case "$1" in
    -j) JOBS="$2"; shift 2 ;;
    -j*) JOBS="${1#-j}"; shift ;;
    -h|--help) sed -n '2,15p' "${BASH_SOURCE[0]}" | sed 's/^# \?//'; exit 0 ;;
    -*) echo "unknown flag: $1" >&2; exit 2 ;;
    toolchain|pk|spike|gem5|repo) STEPS+=("$1"); shift ;;
    *) echo "unknown step: $1 (toolchain, pk, spike, gem5, repo)" >&2; exit 2 ;;
  esac
done
[ ${#STEPS[@]} -eq 0 ] && STEPS=(toolchain pk spike gem5 repo)

log() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
want() { for s in "${STEPS[@]}"; do [ "$s" = "$1" ] && return 0; done; return 1; }

fetch_commit() {
  local dir="$1" repo="$2" sha="$3"
  mkdir -p "$dir"
  if [ ! -d "$dir/.git" ]; then
    git -C "$dir" init -q
    git -C "$dir" remote add origin "$repo"
  fi
  git -C "$dir" remote set-url origin "$repo"
  if [ "$(git -C "$dir" rev-parse -q --verify HEAD 2>/dev/null || true)" != "$sha" ]; then
    git -C "$dir" fetch -q --depth 1 origin "$sha"
    git -C "$dir" checkout -q FETCH_HEAD
  fi
  echo "  $dir @ $(git -C "$dir" rev-parse HEAD)"
}

if want toolchain; then
  log "RISC-V toolchain"
  STAMP="$TOOLCHAIN_ROOT/.vcix-toolchain"
  if [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$TOOLCHAIN_SHA256" ]; then
    echo "  already at $TOOLCHAIN_SHA256"
  else
    TMP="$(mktemp -d)"
    trap 'rm -rf "$TMP"' EXIT
    curl -fSL --retry 3 -o "$TMP/toolchain.tar.gz" "$TOOLCHAIN_URL"
    echo "$TOOLCHAIN_SHA256  $TMP/toolchain.tar.gz" | sha256sum -c -
    rm -rf "$TOOLCHAIN_ROOT"; mkdir -p "$TOOLCHAIN_ROOT"
    tar -xzf "$TMP/toolchain.tar.gz" -C "$TOOLCHAIN_ROOT" --strip-components=1 --no-same-owner
    rm -rf "$TMP"; trap - EXIT
    echo "$TOOLCHAIN_SHA256" > "$STAMP"
  fi
  "$TOOLCHAIN_ROOT/bin/clang" --version | head -1
  "$TOOLCHAIN_ROOT/bin/riscv64-unknown-elf-gcc" --version | head -1
fi

if want pk; then
  log "riscv-pk"
  fetch_commit "$PK_ROOT" "$PK_REPO" "$PK_SHA"
  mkdir -p "$PK_ROOT/build"; cd "$PK_ROOT/build"
  export PATH="$TOOLCHAIN_ROOT/bin:$PATH"
  [ -f Makefile ] || ../configure --prefix="$PK_PREFIX" --host=riscv64-unknown-elf
  make -j"$JOBS"
  make install
  test -f "$PK_BIN" || { echo "pk did not land at $PK_BIN" >&2; exit 1; }
  echo "  $PK_BIN"
fi

if want spike; then
  log "spike"
  fetch_commit "$SPIKE_ROOT" "$SPIKE_REPO" "$SPIKE_SHA"
  mkdir -p "$SPIKE_BUILD"; cd "$SPIKE_BUILD"
  [ -f Makefile ] || ../configure --prefix="$SPIKE_PREFIX"
  make -j"$JOBS"
  make install
  test -x "$SPIKE_BIN" || { echo "spike did not land at $SPIKE_BIN" >&2; exit 1; }
  test -f "$SPIKE_BUILD/libriscv.a" || { echo "no libriscv.a in $SPIKE_BUILD" >&2; exit 1; }
  echo "  $SPIKE_BIN"
fi

if want gem5; then
  log "gem5 @ $GEM5_BRANCH"
  fetch_commit "$GEM5_ROOT" "$GEM5_REPO" "$GEM5_SHA"
  STAMP="$GEM5_BIN.vcix-built"
  WANT="$GEM5_SHA scons-$SCONS_VERSION $PYTHON"
  if [ -x "$GEM5_BIN" ] && [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$WANT" ]; then
    echo "  already built"
  else
    [ -x "$SCONS_VENV/bin/python" ] || "$PYTHON" -m venv "$SCONS_VENV"
    "$SCONS_VENV/bin/pip" install -q "scons==$SCONS_VERSION"
    cd "$GEM5_ROOT"
    PYTHON_CONFIG="$PYTHON-config" "$SCONS_VENV/bin/scons" "$GEM5_TARGET" -j"$JOBS" --ignore-style
    echo "$WANT" > "$STAMP"
  fi
  test -x "$GEM5_BIN" || { echo "gem5 did not land at $GEM5_BIN" >&2; exit 1; }
  echo "  $GEM5_BIN"
fi

if want repo; then
  log "vcix-accelerator"
  "$REPO/scripts/build.sh" -j "$JOBS"
fi

log "done -- now run: tests/run.sh"
