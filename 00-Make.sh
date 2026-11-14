#!/usr/bin/env bash
#
# Configure, build and test PocketPCB (Release build in ./build).
# Every run takes the next build number in build_number.txt.
# A build that passed its tests is copied to ./OUT/pocketpcb.
#
#   Build and test:   ./00-Make.sh
#   Fresh build:      ./00-Make.sh --clean
#
set -uo pipefail
cd "$(dirname "$0")" || exit 1

BUILD_DIR=build
OUT_DIR=OUT
BINARY=src/app/pocketpcb

if [[ "${1:-}" == "--clean" ]]; then
    rm -rf "$BUILD_DIR"
elif [[ $# -gt 0 ]]; then
    sed -n '2,9p' "$0"
    exit 1
fi

# The generator is fixed when the build dir is first configured; passing -G
# again to an existing dir with another generator makes CMake fail.
GENERATOR=()
if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    GENERATOR=(-G Ninja)
fi

status=0
step() { # step <name> <command...>
    local name="$1"; shift
    echo ">>> $name"
    "$@" || { status=$?; echo ">>> $name FAILED (exit $status)"; }
}

# The next build number, and the next line when a feature waits in the
# changelog (version_bump in 98-common). Done before configuring, so the binary
# carries the new number.
. ./98-common

step "Version" version_bump
[[ $status -eq 0 ]] && step "Configure" cmake -S . -B "$BUILD_DIR" "${GENERATOR[@]}" -DCMAKE_BUILD_TYPE=Release
[[ $status -eq 0 ]] && step "Build" cmake --build "$BUILD_DIR" --parallel
[[ $status -eq 0 ]] && step "Test" ctest --test-dir "$BUILD_DIR" --output-on-failure
# Only a tested build gets out; after a failure OUT keeps the last good one.
[[ $status -eq 0 ]] && step "Copy to $OUT_DIR" install -D "$BUILD_DIR/$BINARY" "$OUT_DIR/pocketpcb"

echo
if [[ $status -eq 0 ]]; then
    echo ">>> OK"
else
    echo ">>> FAILED"
fi
# Keep the summary on screen when started from a file manager.
sleep 3
exit "$status"
