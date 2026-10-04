#!/usr/bin/env bash
# build-fast.sh - Linux build wrapper for HDAW (analog of dsh-build-fast.bat)
#
# Usage:
#   ./build-fast.sh              Build HDAW (default config, see CONFIG below)
#   ./build-fast.sh debug        Debug config build (reconfigures build/ to Debug)
#   ./build-fast.sh test         Build the four test exes + hdaw_plugin_host
#   ./build-fast.sh all          Build everything (HDAW, HDAW_headless,
#                                hdaw_plugin_scanner, tests, plugin host)
#   ./build-fast.sh ninja        (Re)configure build/ with Ninja
#
# Config selection: first positional arg 'debug' maps to Debug, or use the
# CONFIG env var (e.g. CONFIG=Debug ./build-fast.sh test). Default RelWithDebInfo.
set -eu

ROOT="$(cd "$(dirname "$0")" && pwd)"
CONFIG="${CONFIG:-RelWithDebInfo}"
BUILD_DIR="${HDAW_BUILD_DIR:-$ROOT/build}"

case "${1:-}" in
    debug) CONFIG=Debug ;;
    test|all|ninja|"") ;;
    *)
        echo "Unknown target: $1" >&2
        echo "Usage: ./build-fast.sh [test|all|debug|ninja]" >&2
        exit 1
        ;;
esac

# -- Concurrency guard: one build per build dir -------------------------------
LOCKFILE="$BUILD_DIR/.build.lock"
mkdir -p "$BUILD_DIR"
exec 9>"$LOCKFILE"
flock -n 9 || {
    echo "[build-fast] ERROR: another build is running in $BUILD_DIR ($LOCKFILE held)." >&2
    exit 1
}
trap 'flock -u 9' EXIT

CMAKE_EXE="${CMAKE_EXE:-cmake}"
NINJA_EXE="${NINJA_EXE:-ninja}"

# -- Configure if stale/missing ------------------------------------------------
# Reconfigure when build/ is missing or CMakeLists changed since the cache
# (CMAKE_SUPPRESS_REGENERATION-style: no automatic re-run on CMakeLists edits).
need_configure=0
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
    need_configure=1
elif [ "$ROOT/CMakeLists.txt" -nt "$BUILD_DIR/CMakeCache.txt" ]; then
    need_configure=1
fi
if [ "$need_configure" = 1 ]; then
    echo "[build-fast] configuring ($BUILD_DIR, $CONFIG)..."
    "$CMAKE_EXE" -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE="$CONFIG"
else
    echo "[build-fast] using cached configure ($BUILD_DIR, $CONFIG)"
fi

# -- Build dispatch -------------------------------------------------------------
TEST_TARGETS="hdaw_tests_engine hdaw_tests_mcp hdaw_tests_frontend hdaw_tests_platform hdaw_plugin_host"
ALL_TARGETS="$TEST_TARGETS HDAW HDAW_headless hdaw_plugin_scanner"

case "${1:-}" in
    test)
        echo "[build-fast] building test exes..."
        "$CMAKE_EXE" --build "$BUILD_DIR" --target $TEST_TARGETS
        ;;
    all)
        echo "[build-fast] building all targets..."
        "$CMAKE_EXE" --build "$BUILD_DIR" --target $ALL_TARGETS
        ;;
    *)
        echo "[build-fast] building HDAW..."
        "$CMAKE_EXE" --build "$BUILD_DIR" --target HDAW
        echo "[build-fast] HDAW up to date (config: $CONFIG)."
        ;;
esac
