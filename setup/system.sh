#!/usr/bin/env bash
# The system packages the environment needs, on $BASE_IMAGE (Ubuntu 22.04).
# Needs root; setup.sh does not. The Dockerfile runs this, so a machine set up
# by hand and the image get the same list.
#
#     sudo ./setup/system.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=versions.env
source "$HERE/versions.env"

export DEBIAN_FRONTEND=noninteractive

# $PYTHON is newer than the distribution's own; deadsnakes carries it.
apt-get update
apt-get install -y --no-install-recommends software-properties-common ca-certificates gnupg
add-apt-repository -y ppa:deadsnakes/ppa
apt-get update

# What each line is for:
#   building anything      build-essential git curl m4 pkg-config
#   this repository        cmake ninja-build
#   gem5                   $PYTHON (embedded), zlib, protobuf, tcmalloc, hdf5, png
#   spike                  boost (regex, system, asio headers), and dtc at run time
apt-get install -y --no-install-recommends \
  build-essential git curl m4 pkg-config \
  cmake ninja-build \
  "$PYTHON" "$PYTHON-dev" "$PYTHON-venv" \
  zlib1g-dev libprotobuf-dev protobuf-compiler libgoogle-perftools-dev \
  libhdf5-serial-dev libpng-dev \
  libboost-dev libboost-regex-dev libboost-system-dev device-tree-compiler
rm -rf /var/lib/apt/lists/*

# PyYAML for the interpreter gem5 embeds. That interpreter ships without pip, so
# a throwaway venv's pip installs into its site-packages.
SITE="$("$PYTHON" -c 'import site; print(site.getsitepackages()[0])')"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
"$PYTHON" -m venv "$TMP/venv"
"$TMP/venv/bin/pip" install -q --no-cache-dir --upgrade --target "$SITE" "PyYAML==$PYYAML_VERSION"
"$PYTHON" -c 'import yaml; print("PyYAML", yaml.__version__, "for", yaml.__file__)'
