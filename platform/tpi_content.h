// tpi_content.h: the TPI Worlds and Rides mod's content (tpw-mods tpi-worlds, mod.json's "content": "tpi"), three worlds
// the launcher's converter makes from the player's own copy of Theme Park Inc (assets/tpi_convert.h) in the data
// folder's assets/tpi. The content folder mirrors the game folder: data/levels/<world>/... is a whole level folder in
// the form the game's own four are (terrain, rides, shops, sideshows, features, facilities and their .sam files), and
// data/lobby/... holds the island screen's files for the three (their descriptors and models). The converter writes
// converter.txt in the content folder last, so a folder without it is not complete and is not used.
// The launcher lays the folder over the game folder only with the mod on, in a build with the port's fixes, so with it
// off (or in a build without them) the game sees the game folder alone, as the original does.
//
// Laid over, the folder is part of the game folder for every file the game reaches (files.cpp FindHostPathOf and
// ListGameFolder, which every lookup, listing, wad and storage opening, stream and existence check goes through): a
// lookup the game folder cannot answer is answered from the content folder, a listing shows the content folder's
// entries after the game folder's. The content folder comes AFTER the game folder, not before it as the content rides
// do (content_overlay.h), so a file of the content folder never takes the place of one the game folder has.
//
// A converted world holds only what Theme Park Inc has; the rest of what a world needs comes from its donor, one of the
// four original worlds (kTpiWorlds): a lookup of data/levels/<world>/<rest> that nothing holds is answered with
// data/levels/<donor>/<rest> through the normal lookup, and a listing under the world shows the donor's entries after
// the world's own, with the world's three-letter name in file names mapped ("Advisor_ara.md2" is answered with the
// donor's "Advisor_jun.md2", and the donor's file is listed under the world's name). What falls back and what does not:
//
//   under data/levels/<world>/        falls back to the donor   why
//   rides, shops, sideshow, features, upgrades, facilities, obstacles
//                                     NO (lookups and listings)  CRideDatabase::LoadTheme scans them: a donor's item would
//                                                                join the world, and the world's rides are its own
//   easymode.* (Easymode.TPWI)        NO                         the donor's easy-mode park is a park of the donor world
//   Sound, Speech, Music              yes                        the banks, the advisor's voice and the music
//   Advisor                           yes, names mapped          the advisor's models and textures are the donor's
//   sky, ssky                         yes, per file              the sky textures, a safety net (the world has its own)
//   every other file and folder       yes, per file              global.sam, Standard.sam, Easy_Standard.sam, the wads...
//
// A folder of the world that is not there at all lists as the donor's (less the folders above); one that is there lists
// its own entries and then the donor's that it lacks. data/levels itself and anything outside a world never falls back.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "TpiWorlds.h"

namespace tpw {

// The table of the three worlds (kTpiWorlds: level folder, title, donor), the folders that never fall back
// (kTpiOwnFolders) and the helpers that read the table (TpiWorldOf, TpiGateNames, TpiSettingsThemeFolder) are in
// include/TpiWorlds.h, header only, shared with the game's code.

// The donor's path for a game path split into `parts` (SplitGamePath's) under a mod world, data/levels/<folder>/<rest>:
// data/levels/<donor>/<rest>, the world's three-letter name in `rest` mapped to the donor's. False, and `donor` left
// alone, when the path is not under a mod world or is one the table says never falls back.
bool TpiDonorParts(const std::vector<std::string>& parts, std::vector<std::string>& donor);

// A name the donor's listing gave, as the world lists it: the donor's three-letter name mapped to the world's.
std::string TpiWorldName(const TpiWorld& world, const std::string& donorName);

// The worlds the content folder `root` (<data folder>/assets/tpi) holds, by title, in kTpiWorlds order: those whose
// level folder is there, the stamp's and the folders' names matched ignoring case as the lookups match them. Empty when
// the folder has no converter stamp (nothing converted yet, or a conversion that did not finish).
std::vector<std::string> FindTpiWorlds(const std::filesystem::path& root);

// Whether the content is there and whole: the stamp and all of kTpiWorlds. The mod is locked until then.
bool TpiContentReady(const std::filesystem::path& root);

// Lay the content folder over the game folder (a lookup finds a file under it that the game folder does not have, a
// listing shows its folders and files after the game folder's, and a mod world's missing files are the donor's), or
// with an empty `root` take it away (the launcher's call when the mod is off). Returns how many worlds are now laid
// over: 0, or all of kTpiWorlds. Nothing is laid over unless TpiContentReady(root). One exception, with the port's
// fixes: an empty `root` lays the TPW_TPI_ROOT test hook's folder over instead when the variable names a whole content
// folder (below), so a scripted play of the mod's worlds needs no Mods box entry: TPW_TPI_ROOT=<folder> on the
// launcher's command line is enough.
size_t UseTpiContent(const std::filesystem::path& root);

// The content folder laid over (empty when none). With the port's fixes, when nothing has asked to lay one over or
// take it away, the first call reads the variable TPW_TPI_ROOT once: a test hook for the platform's headless runs and
// the scripted plays (devtools/run_launcher.sh), which names the content folder itself (what <data folder>/assets/tpi
// would be: converter.txt and data/levels/<world> under it, not the data folder). It lays that folder over as the
// launcher would, and keeps the tpi_worlds hook on whatever the Mods box says: every call sets the flag again, so the
// launcher's tpw::mods::Apply (which sets it from the Mods box, before the game's first lookup) does not take it off.
// (mods.cpp is also built alone, by the mods' tests, so it cannot ask this file.)
const std::filesystem::path& TpiContentRoot();

// Whether the TPW_TPI_ROOT test hook laid a folder over (false in a build without the port's fixes, or when the
// variable is unset, empty or names no whole content folder).
bool TpiContentForced();

}  // namespace tpw
