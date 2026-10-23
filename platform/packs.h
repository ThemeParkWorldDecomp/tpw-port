// packs.h: asset packs, folders of replacement game files laid over the game folder. The game finds a file in the user
// folder first, then in each pack folder in turn, then in the game folder (files.cpp's FindHostPath and ListGameFolder),
// so a pack replaces a game file without touching the game folder, which the launcher's startup check keeps as imported.
// The launcher unpacks the packs' zips and fills the list before the game starts (launcher/asset_packs.cpp).
#pragma once

#include <filesystem>
#include <vector>

namespace tpw {

// The unpacked packs, searched first to last.
std::vector<std::filesystem::path>& PackDirs();

}  // namespace tpw
