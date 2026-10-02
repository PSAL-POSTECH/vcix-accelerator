#!/usr/bin/env bash
# Cut what setup.sh built down to what is used, for the image only.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=versions.env
source "$HERE/versions.env"

KEEP="$(mktemp -d)"
mkdir -p "$KEEP/$(dirname "$GEM5_TARGET")"
mv "$GEM5_BIN" "$KEEP/$GEM5_TARGET"
mv "$GEM5_ROOT/configs" "$KEEP/configs"
rm -rf "$GEM5_ROOT"
mv "$KEEP" "$GEM5_ROOT"
chmod 755 "$GEM5_ROOT"

find "$SPIKE_BUILD" \( -name '*.o' -o -name '*.gch' \) -delete
rm -f "$SPIKE_BUILD/libspike_main.a" "$SPIKE_BUILD/libspike_dasm.a"
test -f "$SPIKE_BUILD/libriscv.a"
rm -rf "$SPIKE_ROOT/.git" "$PK_ROOT" "$SCONS_VENV"

for tool in clang-check clang-repl clang-refactor clang-rename clang-scan-deps \
            clang-extdef-mapping clang-linker-wrapper clang-offload-bundler \
            clang-offload-packager diagtool qemu-riscv32 qemu-riscv64; do
  rm -f "$TOOLCHAIN_ROOT/bin/$tool"
done

# An x86-64 ELF, by its header: the magic number, then e_machine 0x3e at byte 18.
host_elf() {
  [ "$(od -An -tx1 -N4 "$1" | tr -d ' ')" = 7f454c46 ] \
    && [ "$(od -An -tx1 -j18 -N2 "$1" | tr -d ' ')" = 3e00 ]
}

strip_tree() {
  local f
  while IFS= read -r -d '' f; do
    if host_elf "$f"; then strip --strip-debug "$f"; fi
  done < <(find "$@" -type f -print0)
}

strip_tree "$GEM5_ROOT/build" "$SPIKE_BUILD" "$SPIKE_PREFIX" \
  "$TOOLCHAIN_ROOT/bin" "$TOOLCHAIN_ROOT/libexec"
strip --strip-debug "$SPIKE_BUILD"/*.a "$SPIKE_PREFIX"/lib/*.a

du -sh "$VCIX_ENV_ROOT"/*
