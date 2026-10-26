#!/bin/sh
# Packs the built Linux launcher as an AppImage: one file that runs on any distribution with a glibc at least as new as
# the one it was built on (the launcher links nothing else; SDL opens the window system, the sound system and the
# graphics driver at run time). CI builds it on an Ubuntu 22.04 base (glibc 2.35: Ubuntu 22.04, Debian 12, Fedora 36,
# Arch and anything newer), and this script makes the same file from a build of your own.
#
#   sh devtools/appimage.sh [BUILD_DIR] [OUT]     default: build, then build/STP-TPW_Launcher-<version>-<arch>.AppImage
#
# What it does: an AppDir with the launcher, a .desktop entry and the launcher's icon (the launcher writes its own,
# --write-icon), squashed with mksquashfs (the squashfs-tools package) and put after the AppImage runtime, the small
# static program that mounts the image and starts AppRun. The runtime is fetched from the AppImage project's
# type2-runtime release, checked against the SHA-256 below, and kept in BUILD_DIR/tools/ for the next time; TPW_APPIMAGE_RUNTIME
# names one you already have instead. The AppImage needs no FUSE to run when FUSE is missing: APPIMAGE_EXTRACT_AND_RUN=1
# or --appimage-extract-and-run unpacks it to a temporary folder first.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
build=${1:-$here/build}
launcher="$build/tpw_launcher"
[ -x "$launcher" ] || { echo "appimage: no launcher at $launcher (build the tpw_launcher target first)"; exit 1; }
command -v mksquashfs >/dev/null 2>&1 || { echo "appimage: mksquashfs is not installed (the squashfs-tools package)"; exit 1; }

arch=$(uname -m)
case "$arch" in
    x86_64) sum=617aa77314d57c8b8e4fae9c3f0b35af7b30d5529cb69e98f64f92510ed98a46 ;;
    aarch64) sum=9a72a744d221576c9347ec074243acc5c3c7568896b12714e0e82e9f0bf47900 ;;
    *) echo "appimage: no AppImage runtime for $arch"; exit 1 ;;
esac
# The runtime: AppImage/type2-runtime's continuous release of 2026-10 (static, FUSE 3, no library needed on the host).
url="https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-$arch"
runtime=${TPW_APPIMAGE_RUNTIME:-$build/tools/runtime-$arch}
checksum() { sha256sum "$1" | cut -c1-64; }
if [ ! -f "$runtime" ] || [ "$(checksum "$runtime")" != "$sum" ]; then
    [ -z "${TPW_APPIMAGE_RUNTIME:-}" ] || { echo "appimage: $runtime is not the pinned runtime (SHA-256 $sum)"; exit 1; }
    mkdir -p "$(dirname "$runtime")"
    echo "appimage: fetching the AppImage runtime from $url"
    curl -fsSL -o "$runtime.part" "$url"
    got=$(checksum "$runtime.part")
    if [ "$got" != "$sum" ]; then
        rm -f "$runtime.part"
        echo "appimage: the runtime's SHA-256 is $got, not the pinned $sum (a new runtime was released: check it, then update the pin here)"
        exit 1
    fi
    mv "$runtime.part" "$runtime"
fi

version=$("$launcher" --version 2>/dev/null | sed -n '1s/.* v//p')
out=${2:-$build/STP-TPW_Launcher-${version:-unknown}-$arch.AppImage}
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
app="$dir/AppDir"
mkdir -p "$app/usr/bin" "$app/usr/share/applications" "$app/usr/share/icons/hicolor/256x256/apps" "$app/usr/share/metainfo"
cp "$launcher" "$app/usr/bin/tpw_launcher"
ln -s usr/bin/tpw_launcher "$app/AppRun"
# the icon is the launcher's own, drawn without a window (the stand-in until a game is imported, as on Windows and Macs)
"$launcher" --data "$dir/data" --write-icon "$app/tpw-launcher.png" >/dev/null
cp "$app/tpw-launcher.png" "$app/usr/share/icons/hicolor/256x256/apps/tpw-launcher.png"
ln -s tpw-launcher.png "$app/.DirIcon"
cat > "$app/tpw-launcher.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=STP/TPW Launcher
Comment=Play Sim Theme Park and Theme Park World from your own discs
Exec=tpw_launcher
Icon=tpw-launcher
Categories=Game;
Terminal=false
EOF
cp "$app/tpw-launcher.desktop" "$app/usr/share/applications/tpw-launcher.desktop"
cat > "$app/usr/share/metainfo/org.tpw-port.launcher.appdata.xml" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<component type="desktop-application">
  <id>org.tpw-port.launcher</id>
  <name>STP/TPW Launcher</name>
  <summary>Play Sim Theme Park and Theme Park World from your own discs</summary>
  <metadata_license>CC0-1.0</metadata_license>
  <description><p>An unofficial port of Sim Theme Park (Theme Park World, 1999). You need your own copy of the game.</p></description>
  <launchable type="desktop-id">tpw-launcher.desktop</launchable>
  <url type="homepage">https://github.com/ThemeParkWorldDecomp/tpw-port</url>
  <provides><binary>tpw_launcher</binary></provides>
</component>
EOF

# A type 2 AppImage: the runtime, then the squashfs image (gzip, which every runtime reads). The runtime finds the
# image after its own ELF, so the two are simply put one after the other. Everything in the image is root's, readable
# by all and dated zero, so the same launcher gives the same file.
chmod -R a+rX "$app"
mksquashfs "$app" "$dir/image.squashfs" -comp gzip -root-owned -all-time 0 -mkfs-time 0 -noappend -no-exports -no-recovery -quiet -no-progress
cat "$runtime" "$dir/image.squashfs" > "$out"
chmod +x "$out"
echo "appimage: $out ($(du -h "$out" | cut -f1), $("$launcher" --version | sed -n 1p))"
