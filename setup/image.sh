#!/usr/bin/env bash
# The container image of the environment: what it is called, and how it is built.
#
#     ./setup/image.sh ref               the image for this checkout's setup/
#     ./setup/image.sh exists            exit 0 if the registry already has it
#     ./setup/image.sh build [-j N]      build it locally
#     ./setup/image.sh build --push      build it and publish it
#
# The tag is the git tree hash of setup/: the pin file, the Dockerfile and the
# scripts the Dockerfile runs are the whole build context, so the tag changes
# exactly when the image would, and a checkout always names the image it needs.
# CI builds only when `exists` says no. Uncommitted changes under setup/ give a
# -dirty tag, which is never pushed.
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
  *) sed -n '2,7p' "${BASH_SOURCE[0]}" | sed 's/^# \?//' >&2; exit 2 ;;
esac
