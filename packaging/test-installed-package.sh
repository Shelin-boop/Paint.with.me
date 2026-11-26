#!/usr/bin/env bash
# Install a built package on a fresh system with its own package manager
# (which also fetches Qt and everything else it needs), start the program,
# check its menu entry, icon and plugins, then remove it again.
#
#   docker run --rm -v "$PWD:/src" -w /src ubuntu:24.04 \
#       packaging/test-installed-package.sh dist/paint-with-me_amd64.deb
set -euo pipefail

PKG="$(readlink -f "${1:?usage: test-installed-package.sh <package file>}")"
APP_ID=io.github.shelin_boop.PaintWithMe
. /etc/os-release
echo "== $(basename "$PKG") on $PRETTY_NAME"

case "$PKG" in
    *.deb)
        export DEBIAN_FRONTEND=noninteractive
        apt-get update -q
        apt-get install -y -q "$PKG"
        remove() { apt-get remove -y -q paint-with-me; }
        ;;
    *.rpm)
        if command -v dnf > /dev/null; then
            dnf install -y "$PKG"
            remove() { dnf remove -y paint-with-me; }
        else
            # Packages downloaded from GitHub are not signed by openSUSE.
            zypper --non-interactive install --allow-unsigned-rpm "$PKG"
            remove() { zypper --non-interactive remove paint-with-me; }
        fi
        ;;
    *.pkg.tar.zst)
        pacman -Sy --noconfirm --needed archlinux-keyring
        pacman -Su --noconfirm
        pacman -U --noconfirm "$PKG"
        remove() { pacman -R --noconfirm paint-with-me; }
        ;;
    *)
        echo "Unknown package type: $PKG" >&2
        exit 1
        ;;
esac

fail() { echo "FAILED: $*" >&2; exit 1; }

echo "== Version: $(paint-with-me --version)"

# Starts and keeps running (offscreen: no screen needed). timeout's exit
# code 124 means it was still running after 10 s.
code=0
QT_QPA_PLATFORM=offscreen timeout 10 paint-with-me > /tmp/pwm-start.log 2>&1 || code=$?
cat /tmp/pwm-start.log
[ "$code" = 124 ] || fail "the program exited early with code $code"

test -f "/usr/share/applications/$APP_ID.desktop" || fail "no menu entry"
test -f "/usr/share/icons/hicolor/256x256/apps/$APP_ID.png" || fail "no icon"
test -f "/usr/share/metainfo/$APP_ID.metainfo.xml" || fail "no AppStream data"

# Qt plugins the program loads at run time (installed as dependencies).
# Found by the shell itself: some minimal systems have no `find`.
# /usr/lib*/qt6: Fedora, openSUSE, Arch; /usr/lib/*/qt6: Debian, Ubuntu.
plugin() { compgen -G "/usr/lib*/qt6/plugins/$1" > /dev/null || compgen -G "/usr/lib/*/qt6/plugins/$1" > /dev/null; }
plugin 'imageformats/libqwebp.so' || fail "no WebP support"
plugin 'imageformats/libqtiff.so' || fail "no TIFF support"
plugin 'platforms/libqwayland*.so' || fail "no Wayland support"

remove
! command -v paint-with-me > /dev/null || fail "still installed after removing"
test ! -e "/usr/share/applications/$APP_ID.desktop" || fail "menu entry left behind"
echo "== Installed, started and removed cleanly"
