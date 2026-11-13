#!/usr/bin/env bash
#
# Configure, build and test PocketPCB (Release build in ./build).
#
#   Build and test:   ./00-Make.sh
#   Fresh build:      ./00-Make.sh --clean
#
set -uo pipefail
cd "$(dirname "$0")" || exit 1

BUILD_DIR=build

if [[ "${1:-}" == "--clean" ]]; then
    rm -rf "$BUILD_DIR"
elif [[ $# -gt 0 ]]; then
    sed -n '2,7p' "$0"
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

step "Configure" cmake -S . -B "$BUILD_DIR" "${GENERATOR[@]}" -DCMAKE_BUILD_TYPE=Release
[[ $status -eq 0 ]] && step "Build" cmake --build "$BUILD_DIR" --parallel
[[ $status -eq 0 ]] && step "Test" ctest --test-dir "$BUILD_DIR" --output-on-failure

echo
if [[ $status -eq 0 ]]; then
    echo ">>> OK"
else
    echo ">>> FAILED"
fi
# Keep the summary on screen when started from a file manager.
sleep 5
exit "$status"
