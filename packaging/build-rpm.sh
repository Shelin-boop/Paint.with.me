#!/usr/bin/env bash
# Build the .rpm for Fedora or openSUSE (whichever system it runs on).
#
# Run it in the oldest supported release, so the package installs there and
# on every newer one (it asks for Qt and C library versions by what it uses):
#
#   docker run --rm -v "$PWD:/src" -w /src fedora:43 \
#       packaging/build-rpm.sh 0.1.150 dist
#   docker run --rm -v "$PWD:/src" -w /src opensuse/leap:16.0 \
#       packaging/build-rpm.sh 0.1.150 dist
#
# Writes <output dir>/paint-with-me-fedora.x86_64.rpm or
# paint-with-me-opensuse.x86_64.rpm, and fedora.txt / opensuse.txt.
set -euo pipefail

VER="${1:?usage: build-rpm.sh <version> <output dir>}"
OUT="${2:?usage: build-rpm.sh <version> <output dir>}"
HERE="$(cd "$(dirname "$0")" && pwd)"

. /etc/os-release
case " ${ID:-} ${ID_LIKE:-} " in
    *" fedora "*)
        SYSTEM=fedora
        # The image format plugins are for the tests (WebP and TIFF are tested).
        dnf install -y gcc-c++ cmake ninja-build qt6-qtbase-devel zlib-devel \
            mesa-libGL-devel qt6-qtimageformats rpm-build dejavu-sans-fonts file
        # Loaded at run time, so not found automatically: WebP and TIFF,
        # native Wayland.
        REQUIRES="qt6-qtimageformats, qt6-qtwayland"
        QT=$(rpm -q --qf '%{VERSION}' qt6-qtbase)
        ;;
    *" suse "* | *" opensuse "*)
        SYSTEM=opensuse
        zypper --non-interactive install gcc-c++ cmake ninja \
            qt6-core-devel qt6-gui-devel qt6-widgets-devel qt6-printsupport-devel \
            qt6-concurrent-devel qt6-network-devel qt6-opengl-devel \
            qt6-openglwidgets-devel zlib-devel Mesa-libGL-devel qt6-imageformats \
            rpm-build dejavu-fonts file
        REQUIRES="qt6-imageformats, qt6-wayland"
        QT=$(rpm -q --qf '%{VERSION}' qt6-core-devel)
        ;;
    *)
        echo "This script builds on Fedora or openSUSE, not on ${PRETTY_NAME:-this system}." >&2
        exit 1
        ;;
esac

"$HERE/build-and-test.sh" build-rpm "$VER"
(cd build-rpm && cpack -G RPM -D CPACK_RPM_PACKAGE_REQUIRES="$REQUIRES")

mkdir -p "$OUT"
FILE="$OUT/paint-with-me-$SYSTEM.x86_64.rpm"
cp build-rpm/paint-with-me-"$VER"-1.x86_64.rpm "$FILE"
echo "Requires:"; rpm -qpR "$FILE" | grep -v '^rpmlib(' | sed 's/^/  /'

echo "built on $PRETTY_NAME with Qt $QT" > "$OUT/$SYSTEM.txt"
cat "$OUT/$SYSTEM.txt"
