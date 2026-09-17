#!/usr/bin/env sh
set -eu

# Portable QIF Viewer build wrapper for macOS, Linux, Solaris, and BSD.
# Environment variables:
#   BUILD_DIR        Build directory (default: build)
#   BUILD_TYPE       CMake build type (default: Release)
#   CMAKE_GENERATOR  Optional CMake generator, e.g. Ninja
#   JOBS             Parallel build jobs; auto-detected when possible
#
# Any command-line arguments are forwarded to the CMake configure step.

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT_DIR/build"}
BUILD_TYPE=${BUILD_TYPE:-Release}

if ! command -v cmake >/dev/null 2>&1; then
    echo "error: cmake was not found in PATH" >&2
    exit 1
fi

# Pick a reasonable parallelism value without depending on GNU utilities.
if [ -z "${JOBS:-}" ]; then
    if command -v nproc >/dev/null 2>&1; then
        JOBS=$(nproc)
    elif command -v getconf >/dev/null 2>&1; then
        JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
    elif command -v sysctl >/dev/null 2>&1; then
        JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 1)
    else
        JOBS=1
    fi
fi

case "$JOBS" in
    ''|*[!0-9]*) JOBS=1 ;;
esac
[ "$JOBS" -gt 0 ] 2>/dev/null || JOBS=1

GEN_ARGS=""
if [ -n "${CMAKE_GENERATOR:-}" ]; then
    GEN_ARGS="-G${CMAKE_GENERATOR}"
fi

printf '%s\n' "Configuring QIF Viewer:" \
    "  source:     $ROOT_DIR" \
    "  build:      $BUILD_DIR" \
    "  build type: $BUILD_TYPE" \
    "  jobs:       $JOBS"

# Deliberately allow word splitting only for the optional generator argument.
# shellcheck disable=SC2086
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" $GEN_ARGS \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" "$@"

cmake --build "$BUILD_DIR" --parallel "$JOBS"

printf '\nBuild complete.\n'
printf 'Viewer:   %s\n' "$BUILD_DIR/qifviewer"
printf 'Checker:  %s\n' "$BUILD_DIR/qifcheck"
printf '\nExamples:\n'
printf '  %s/qifviewer part.qif\n' "$BUILD_DIR"
printf '  %s/qifviewer --renderer vulkan part.qif\n' "$BUILD_DIR"
printf '  %s/qifcheck part.qif\n' "$BUILD_DIR"
