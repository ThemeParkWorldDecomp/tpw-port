#!/bin/sh
# Builds the Linux launcher as an AppImage: one file, STP-TPW-Launcher-<version>-<arch>.AppImage, that runs on any
# distribution with a glibc as new as the one it was built on, with nothing to install: chmod +x, then run it. CI's
# appimage job (release.yml) makes the one on the Releases page with this script, on the oldest Ubuntu GitHub offers
# (22.04, glibc 2.35), and it makes the same file from a checkout of your own.
#
#   sh devtools/appimage.sh [OUT]     default: build/STP-TPW-Launcher-<version>-<arch>.AppImage
#
# What it does: builds the launcher as the README's Linux build does (CMake and Ninja, Release, the C++ runtime linked
# in, so that the program needs only the C library), in build/appimage/build; lays out the AppDir (usr/bin/tpw_launcher,
# AppRun starting it, the Flatpak's desktop entry and metainfo, and the launcher's icon, drawn without a window as the
# Flatpak's is), and packs it with appimagetool. Nothing else goes in: SDL opens the window system, the sound system and
# the graphics driver (OpenGL, or the Vulkan loader) of the machine at run time, as on any other Linux. The launcher keeps
# its data folder where the Linux build does (~/.local/share/STP-TPW Launcher) and writes nothing beside its program, so
# the AppImage's read-only mount is all it needs. appimagetool and the AppImage runtime it puts at the file's head are
# fetched once into build/appimage/tools, each from a pinned release and checked against its SHA-256 (APPIMAGETOOL and
# APPIMAGE_RUNTIME name files of your own instead). The runtime is the static one: the AppImage needs FUSE to mount
# itself but no libfuse2, and runs without FUSE with --appimage-extract-and-run.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
id=io.github.ThemeParkWorldDecomp.Launcher
[ "$(uname -s)" = Linux ] || { echo "appimage: this is for Linux"; exit 1; }
[ -f "$here/tpw-decomp/CMakeLists.txt" ] || { echo "appimage: tpw-decomp is missing: git submodule update --init --recursive"; exit 1; }
arch=$(uname -m)
[ "$arch" = x86_64 ] || { echo "appimage: x86_64 only (this machine is $arch)"; exit 1; }
version=$(head -n 1 "$here/launcher/VERSION.txt" | tr -d '\r')
out=${1:-$here/build/STP-TPW-Launcher-$version-$arch.AppImage}
work="$here/build/appimage"
tools="$work/tools"
appdir="$work/AppDir"

# appimagetool 1.9.1 (github.com/AppImage/appimagetool) and type2-runtime 20251108 (github.com/AppImage/type2-runtime),
# given to it by file, so that it never fetches the moving "continuous" runtime itself.
tool_url=https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage
tool_sha=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
runtime_url=https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64
runtime_sha=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d

# fetch NAME URL SHA256: tools/NAME, downloaded when missing or not the pinned file. curl retries a failed transfer
# itself; a whole new download follows a wrong hash, up to three in all.
fetch() {
    f="$tools/$1"
    if [ -f "$f" ] && echo "$3  $f" | sha256sum -c --status; then return 0; fi
    mkdir -p "$tools"
    try=1
    while :; do
        if curl -fsSL --retry 5 --retry-all-errors --retry-delay 10 -o "$f.part" "$2" &&
            echo "$3  $f.part" | sha256sum -c --status; then
            mv "$f.part" "$f"
            chmod +x "$f"
            return 0
        fi
        [ "$try" -lt 3 ] || { echo "appimage: $2 failed or did not match its SHA-256, $try times"; exit 1; }
        echo "appimage: download $try of $1 failed, again"
        try=$((try + 1))
        sleep 20
    done
}
tool=${APPIMAGETOOL:-}
runtime=${APPIMAGE_RUNTIME:-}
[ -n "$tool" ] || { fetch appimagetool-x86_64.AppImage "$tool_url" "$tool_sha"; tool="$tools/appimagetool-x86_64.AppImage"; }
[ -n "$runtime" ] || { fetch runtime-x86_64 "$runtime_url" "$runtime_sha"; runtime="$tools/runtime-x86_64"; }

cmake -S "$here" -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DTPW_STATIC_RUNTIME=ON
cmake --build "$work/build" --target tpw_launcher

rm -rf "$appdir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/share/applications" "$appdir/usr/share/metainfo" \
    "$appdir/usr/share/icons/hicolor/256x256/apps" "$work/icon-data"
install -m755 "$work/build/tpw_launcher" "$appdir/usr/bin/tpw_launcher"
ln -s usr/bin/tpw_launcher "$appdir/AppRun"   # the program itself: SDL finds its folder through /proc/self/exe
install -m644 "$here/flatpak/$id.desktop" "$appdir/usr/share/applications/$id.desktop"
install -m644 "$here/flatpak/$id.metainfo.xml" "$appdir/usr/share/metainfo/$id.metainfo.xml"
ln -s "usr/share/applications/$id.desktop" "$appdir/$id.desktop"
# The icon is the launcher's own stand-in (no game is imported here), in a home and data folder of the build's own.
HOME="$work/icon-data" "$appdir/usr/bin/tpw_launcher" --data "$work/icon-data" \
    --write-icon "$appdir/usr/share/icons/hicolor/256x256/apps/$id.png"
ln -s "usr/share/icons/hicolor/256x256/apps/$id.png" "$appdir/$id.png"

mkdir -p "$(dirname "$out")"
# appimagetool is an AppImage too: extracted and run, so that the build machine needs no FUSE.
ARCH=$arch VERSION=$version APPIMAGE_EXTRACT_AND_RUN=1 "$tool" --no-appstream --runtime-file "$runtime" "$appdir" "$out"
echo "appimage: $out ($(du -h "$out" | cut -f1), STP/TPW Launcher v$version)"
