// asset_packs.h: the asset packs the launcher lays over the game's files (platform/packs.h). A pack is a zip of
// replacement game files, each at its place in the game folder ("data/init/640/Splash_American.tga"), kept in the packs
// folder: "packs" in the data folder, or --packs. Before the game starts, each pack is unpacked into the packs folder's
// ".unpacked", once and again only when its zip changes, and the game finds a pack's files before the game folder's. The
// game folder is never written. Packs apply in name order: where two hold the same file, the later name's is used. A pack
// changes only how the game looks or sounds: it takes pictures, sounds, fonts and films (tpw::PackMayHold), and leaves out
// any other file and any file under a folder named like a game file, logging each.
#pragma once

#include <filesystem>

namespace tpw::launcher {

// Unpacks the packs of `folder` where needed and hands them to the platform layer, logging each one. A pack that cannot
// be read is left out, with a log line saying why. The folder is made when it is missing, so the player can find it.
void UsePacks(const std::filesystem::path& folder);

}  // namespace tpw::launcher
