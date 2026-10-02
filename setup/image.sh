#!/usr/bin/env bash
# The container image of the environment: what it is called, and how it is built.
# Usage: setup/image.sh ref | exists | build [-j N] [--push]
set -euo pipefail
shopt -s inherit_errexit

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(dirname "$HERE")"
# shellcheck source=versions.env
source "$HERE/versions.env"
DOCKER="${DOCKER:-docker}"

image_tag() {
  local tree dirty=""
  tree="$(git -C "$REPO" rev-parse HEAD:setup)"
  [ -z "$(git -C "$REPO" status --porcelain -- setup)" ] || dirty=-dirty
  echo "env-${tree:0:12}$dirty"
}

REF="$IMAGE_REPO:$(image_tag)"

case "${1:-}" in
  tag) echo "${REF##*:}" ;;
  ref) echo "$REF" ;;
  exists) "$DOCKER" buildx imagetools inspect "$REF" > /dev/null ;;
  build)
    shift
    JOBS=$(nproc); OUTPUT=--load
    while [ $# -gt 0 ]; do
      case "$1" in
        -j) JOBS="$2"; shift 2 ;;
        --push) OUTPUT=--push; shift ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
      esac
    done
    case "$OUTPUT$REF" in
      --push*-dirty) echo "setup/ has uncommitted changes; commit them before publishing" >&2; exit 1 ;;
    esac
    "$DOCKER" buildx build "$OUTPUT" -t "$REF" -f "$HERE/Dockerfile" \
      --build-arg BASE_IMAGE="$BASE_IMAGE" \
      --build-arg VCIX_ENV_ROOT="$VCIX_ENV_ROOT" \
      --build-arg JOBS="$JOBS" \
      "$HERE"
    echo "$REF"
    ;;
  *) sed -n '2,3p' "${BASH_SOURCE[0]}" | sed 's/^# \?//' >&2; exit 2 ;;
esac
