#!/usr/bin/env bash
# Configure, build and test Paint.with.me with the system's own Qt. The
# installer scripts (build-deb.sh, build-rpm.sh) call it inside a container,
# so every installer is only made after all test suites passed on its system.
#
# Usage: packaging/build-and-test.sh <build dir> <version>
set -euo pipefail

BUILD="${1:?usage: build-and-test.sh <build dir> <version>}"
VER="${2:?usage: build-and-test.sh <build dir> <version>}"

cmake -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr -DPWM_VERSION="$VER"
cmake --build "$BUILD"
cmake --build "$BUILD" --target pwm_all_tests
QT_QPA_PLATFORM=offscreen ctest --test-dir "$BUILD" --output-on-failure
