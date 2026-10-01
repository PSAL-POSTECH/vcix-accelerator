#!/usr/bin/env bash
# Cut what setup.sh built down to what is used, for the image only: a tree set
# up by hand keeps everything, because there it is rebuilt and debugged.
#
#   gem5        the binary and configs/ stay; the source and the 6 GB build tree go
#   spike       the source and build/ stay (the adapter is built against them);
#               the object files, the precompiled header and the two archives
#               that only repeat the others go
#   pk, scons   the source and the venv go; pk is installed, scons has done its job
#   toolchain   the compilers, binutils and libraries stay; clang's extra tools
#               and qemu go
#   every host executable and archive loses its debug sections -- gem5.opt alone
#   goes from 1 GB to 70 MB. Symbol tables stay.
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

# libspike_main.a and libspike_dasm.a are each one object file plus whole copies
# of the other archives -- 400 MB apiece that strip cannot reach and no link
# uses. libriscv.a, which the adapter links, stays.
find "$SPIKE_BUILD" \( -name '*.o' -o -name '*.gch' \) -delete
rm -f "$SPIKE_BUILD/libspike_main.a" "$SPIKE_BUILD/libspike_dasm.a"
test -f "$SPIKE_BUILD/libriscv.a"
rm -rf "$SPIKE_ROOT/.git" "$PK_ROOT" "$SCONS_VENV"

# From the toolchain, what nothing here compiles, assembles or links with: the
# clang tooling and the user-mode emulators, 600 MB that stripping does not shrink.
for tool in clang-check clang-repl clang-refactor clang-rename clang-scan-deps \
            clang-extdef-mapping clang-linker-wrapper clang-offload-bundler \
            clang-offload-packager diagtool qemu-riscv32 qemu-riscv64; do
  rm -f "$TOOLCHAIN_ROOT/bin/$tool"
done

# An x86-64 ELF, by its header: the magic number, then e_machine 0x3e at byte 18.
# The toolchain's RISC-V libraries are not, and the host strip must not see them.
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
