// packs.h: asset packs, folders of replacement game files laid over the game folder. The game finds a file in the user
// folder first, then in each pack folder in turn, then in the game folder (files.cpp's FindHostPath and ListGameFolder),
// so a pack replaces a game file without touching the game folder, which the launcher's startup check keeps as imported.
// The launcher unpacks the packs' zips and fills the list before the game starts (launcher/asset_packs.cpp).
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace tpw {

// The unpacked packs, searched first to last.
std::vector<std::filesystem::path>& PackDirs();

// Whether a pack may hold the game file at `path` ('/' separated from the game folder, any case; a resource fork's side
// file goes by the file it belongs to): a file that changes only how the game looks or sounds, under folders whose names
// hold no dot. That is a picture (.tga, the .wct textures, the .ani and .cur cursors), a sound (.sf2, and a sound bank's
// .sdt and .map index, both only in the folders the game keeps the banks in), a font (.ttf, the .bf4 bitmap fonts) or a
// film (.mov). Any other file, the .sam settings files above all, can change how a park plays, which a pack never does,
// and a folder named like a game file (bfbin.wad/) would hide that file from the game. The unpacking and the game's
// lookups both go by it, so nothing else in a pack's folder is ever used.
bool PackMayHold(const std::string& path);

// Whether `path` ('/' separated from the game folder, any case) is a file, not a folder, in the game folder.
bool IsGameFile(const std::string& path);

}  // namespace tpw
