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
BUILD_FILE=build_number.txt

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

# The next build number: build + 1, version = <base_version>.<build>, the
# date of today. Done before configuring, so the binary carries the new number.
version_bump() {
    local base_version build
    base_version=$(sed -n 's/^base_version=//p' "$BUILD_FILE")
    build=$(sed -n 's/^build=//p' "$BUILD_FILE")
    if [[ ! "$base_version" =~ ^[0-9]+\.[0-9]+$ || ! "$build" =~ ^[0-9]+$ ]]; then
        echo ">>> $BUILD_FILE: base_version or build is missing or malformed"
        return 1
    fi
    build=$((10#$build + 1)) # 10#: a number like 08 is not octal
    printf 'base_version=%s\nbuild=%s\nversion=%s.%s\nbuild_date=%s\n' \
        "$base_version" "$build" "$base_version" "$build" "$(date +%F)" > "$BUILD_FILE"
    echo "Version: $base_version.$build"
}

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
