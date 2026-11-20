#!/usr/bin/env bash
#
# Pack the build 00-Make.sh left in ./build into one runnable file:
# OUT/<project>-<version>-x86_64.AppImage, beside the bare OUT/<project>.
#
# It installs the build into an AppDir and hands it to linuxdeploy, whose Qt
# plugin brings the Qt libraries and plugins along. The image runs on systems
# as new as the one it was built on (Ubuntu 24.04 base) and newer.
#
# Nothing is built or numbered here: run ./00-Make.sh first.
#
set -e
cd "$(dirname "$0")" || exit 1
. ./98-common

BUILD_DIR="$PWD/build"
BINARY="$BUILD_DIR/src/app/$PROJECT"
APPDIR="$BUILD_DIR/AppDir"
# Three images are enough to fall back on.
KEEP=3

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    sed -n '2,11p' "$0"
    exit 0
fi

[ -x "$BINARY" ] || die "No $BINARY: build with ./00-Make.sh first."
version_load

# ---------- tools ----------
# linuxdeploy and its Qt plugin are single downloaded files, not packages. They
# live in tools/ beside this script, kept out of git; the plugin is found
# beside linuxdeploy.
TOOLS_DIR="$PWD/tools"
LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
PLUGIN_QT="$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"
if [ ! -f "$LINUXDEPLOY" ] || [ ! -f "$PLUGIN_QT" ]; then
    echo "linuxdeploy or its Qt plugin not found in $TOOLS_DIR. Download them once:"
    echo "  mkdir -p tools && cd tools"
    echo "  wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
    echo "  wget https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
    echo "  chmod +x linuxdeploy-x86_64.AppImage linuxdeploy-plugin-qt-x86_64.AppImage"
    exit 1
fi
# A freshly downloaded file comes without the execute bit; one installed by a
# package manager already has it and is not ours to chmod.
for tool in "$LINUXDEPLOY" "$PLUGIN_QT"; do
    [ -x "$tool" ] || chmod +x "$tool" 2>/dev/null || true
    [ -x "$tool" ] || die "Cannot run $tool: no execute bit. chmod +x $tool"
done

# The Qt plugin finds Qt through qmake.
QMAKE=$(command -v qmake6 || command -v qmake || true)
[ -n "$QMAKE" ] || die "No qmake6: sudo apt install qt6-base-dev"
export QMAKE

# ---------- AppDir ----------
# Configured again first: cmake --install follows the rules of the last
# configure, and a build made before an install rule changed would leave it
# out. Configuring builds nothing and leaves the number alone.
cmake -S . -B "$BUILD_DIR" >/dev/null
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --prefix /usr
[ -x "$APPDIR/usr/bin/$PROJECT" ] || die "The install left no $APPDIR/usr/bin/$PROJECT"

# ---------- pack ----------
mkdir -p "$OUT_DIR"
IMAGE="$PWD/$OUT_DIR/$PROJECT-$version-x86_64.AppImage"
rm -f "$IMAGE"
# Extract-and-run: the tools are AppImages themselves, and FUSE is not always
# there to mount them.
APPIMAGE_EXTRACT_AND_RUN=1 LDAI_OUTPUT="$IMAGE" ARCH=x86_64 "$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/$PROJECT" \
    --desktop-file "$APPDIR/usr/share/applications/$PROJECT.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/$PROJECT.svg" \
    --plugin qt \
    --output appimage

ls -t "$OUT_DIR/$PROJECT"-*-x86_64.AppImage 2>/dev/null | tail -n +$((KEEP + 1)) | while read -r old; do
    echo "Removing older image: $(basename "$old")"
    rm -f "$old"
done

echo
echo "Version: $version"
echo "Image:   $(du -sh "$IMAGE" | cut -f1)  $IMAGE"
echo "Run:     $IMAGE"
pause
