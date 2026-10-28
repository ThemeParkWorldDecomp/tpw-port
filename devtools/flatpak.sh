#!/bin/sh
# Builds the Linux launcher as a Flatpak bundle: one file, STP-TPW-Launcher-<version>-linux-<arch>.flatpak, that
# `flatpak install` puts on any distribution with Flatpak (the install script does that, and makes the themepark
# command). CI's linux job makes the one on the Releases page with this script, and it makes the same file from a
# checkout of your own.
#
#   sh devtools/flatpak.sh [OUT]     default: build/STP-TPW-Launcher-<version>-linux-<arch>.flatpak
#
# What it does: flatpak-builder (the flatpak-builder package, or else the org.flatpak.Builder flatpak, installed from
# Flathub when neither is there) builds flatpak/io.github.ThemeParkWorldDecomp.Launcher.yml, the manifest, which
# compiles this checkout (submodules included: git submodule update --init first) against the freedesktop SDK 25.08,
# fetched from Flathub into the user's Flatpak installation when missing; the result goes into a local repository in
# build/flatpak/, and flatpak build-bundle makes the one file from it. The bundle names Flathub as where to get the
# runtime from, so a machine without it fetches it when the bundle is installed.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
id=io.github.ThemeParkWorldDecomp.Launcher
manifest="$here/flatpak/$id.yml"
command -v flatpak >/dev/null 2>&1 || { echo "flatpak: Flatpak is not installed (the flatpak package)"; exit 1; }
[ -f "$here/tpw-decomp/CMakeLists.txt" ] || { echo "flatpak: tpw-decomp is missing: git submodule update --init --recursive"; exit 1; }
version=$(head -n 1 "$here/launcher/VERSION.txt" | tr -d '\r')
arch=$(flatpak --default-arch)
out=${1:-$here/build/STP-TPW-Launcher-$version-linux-$arch.flatpak}
work="$here/build/flatpak"
flathub=https://dl.flathub.org/repo/flathub.flatpakrepo

flatpak remote-add --user --if-not-exists flathub "$flathub"
if command -v flatpak-builder >/dev/null 2>&1; then
    builder=flatpak-builder
else
    flatpak info --user org.flatpak.Builder >/dev/null 2>&1 || flatpak install --user -y --noninteractive flathub org.flatpak.Builder
    builder="flatpak run org.flatpak.Builder"
fi
mkdir -p "$work" "$(dirname "$out")"
# shellcheck disable=SC2086  # $builder is one or three words
$builder --user --install-deps-from=flathub --force-clean --disable-rofiles-fuse \
    --state-dir="$work/state" --repo="$work/repo" "$work/app" "$manifest"
flatpak build-bundle --runtime-repo="$flathub" "$work/repo" "$out" "$id"
echo "flatpak: $out ($(du -h "$out" | cut -f1), STP/TPW Launcher v$version)"
