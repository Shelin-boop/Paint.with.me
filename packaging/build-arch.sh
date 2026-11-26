#!/usr/bin/env bash
# Build the Arch Linux package (also for EndeavourOS, which uses Arch's own
# repositories). Arch has no releases, so it is built on the current state:
#
#   docker run --rm -v "$PWD:/src" -w /src archlinux:latest \
#       packaging/build-arch.sh 0.1.150 dist
#
# Writes <output dir>/paint-with-me-arch-x86_64.pkg.tar.zst and arch.txt.
set -euo pipefail

VER="${1:?usage: build-arch.sh <version> <output dir>}"
OUT="${2:?usage: build-arch.sh <version> <output dir>}"
SRC="$(cd "$(dirname "$0")/.." && pwd)"

pacman -Sy --noconfirm --needed archlinux-keyring
pacman -Su --noconfirm --needed base-devel cmake ninja \
    qt6-base qt6-imageformats qt6-wayland ttf-dejavu

# makepkg refuses to run as root.
id builder > /dev/null 2>&1 || useradd -m builder
WORK="$(mktemp -d)"
cp "$SRC/packaging/arch/PKGBUILD" "$WORK/"
tar -C "$SRC" --exclude=./.git --exclude='./build*' --exclude=./dist \
    --exclude=./AppDir --transform "s,^\.,paint-with-me-$VER," \
    -czf "$WORK/paint-with-me-$VER.tar.gz" .
chown -R builder "$WORK"

QT=$(pacman -Q qt6-base | awk '{print $2}' | sed 's/-.*//')
QT_MIN=$(echo "$QT" | cut -d. -f1,2)
# makepkg builds, runs every test suite (check) and packs.
su builder -c "cd '$WORK' && PWM_VERSION='$VER' PWM_QT_MIN='$QT_MIN' makepkg --noconfirm"

mkdir -p "$OUT"
FILE="$OUT/paint-with-me-arch-x86_64.pkg.tar.zst"
cp "$WORK/paint-with-me-$VER-1-x86_64.pkg.tar.zst" "$FILE"
echo "Depends:"; tar -xOf "$FILE" .PKGINFO | grep '^depend' | sed 's/^/  /'

echo "built on Arch Linux of $(date -u +%Y-%m-%d) with Qt $QT" > "$OUT/arch.txt"
cat "$OUT/arch.txt"
