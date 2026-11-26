#!/usr/bin/env bash
# Build the .deb for Debian, Ubuntu, Kubuntu and Linux Mint.
#
# Run it in Debian 12, the oldest supported system: a package built there
# installs on Debian 12 and 13 and on Ubuntu 24.04 and newer (Ubuntu's
# renamed "t64" Qt packages also answer to the Debian names). Built on a newer
# system it would ask for a newer C library than Debian 12 has.
#
#   docker run --rm -v "$PWD:/src" -w /src debian:12 \
#       packaging/build-deb.sh 0.1.150 dist
#
# Writes <output dir>/paint-with-me_amd64.deb and deb.txt (what it was built on).
set -euo pipefail

VER="${1:?usage: build-deb.sh <version> <output dir>}"
OUT="${2:?usage: build-deb.sh <version> <output dir>}"
HERE="$(cd "$(dirname "$0")" && pwd)"

export DEBIAN_FRONTEND=noninteractive
apt-get update -q
apt-get install -y -q --no-install-recommends \
    build-essential cmake ninja-build qt6-base-dev libgl1-mesa-dev zlib1g-dev \
    dpkg-dev file fonts-dejavu-core

"$HERE/build-and-test.sh" build-deb "$VER"
(cd build-deb && cpack -G DEB)

mkdir -p "$OUT"
cp build-deb/paint-with-me_"$VER"_amd64.deb "$OUT/paint-with-me_amd64.deb"
echo "Depends: $(dpkg-deb -f "$OUT/paint-with-me_amd64.deb" Depends)"

. /etc/os-release
QT=$(dpkg-query -W -f='${Version}' qt6-base-dev | sed 's/[+~-].*//')
echo "built on $PRETTY_NAME with Qt $QT" > "$OUT/deb.txt"
cat "$OUT/deb.txt"
