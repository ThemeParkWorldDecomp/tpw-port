// gold_rides.h: the Gold Edition rides, laid over the game folder. The PC Gold bonus CD's installer copied each ride's
// .wad into the base game's data/levels/<theme>/<rides|features|sideshow> folder, as its RideConfig.txt lists: 35 rides,
// 17 of them newer copies of rides the base game ships and 18 new. The launcher imports the CD to assets/gold, apart
// from the US and European game folders, and this overlay shows each .wad to the game at its RideConfig.txt place, as
// if the installer had copied it there: a lookup (files.cpp's FindHostPath) finds it after the user folder and the
// asset packs and before the game folder's copy, and a listing (ListGameFolder) shows it in the game folder's place,
// a Gold copy in a base ride's place and a new ride sorted in among the base ones. The game's ride database scans those
// folders for .wad files (CRideDatabase::ScanDirectory), so no game code reads the list. With the overlay empty (the
// launcher's Gold switch off, or no Gold data) the game sees the game folder alone, as before.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "platform.h"

namespace tpw {

struct GoldRide {
    std::string folder;            // the game folder it belongs in, '/' separated, lower case: data/levels/space/rides
    std::string name;              // the .wad's name as RideConfig.txt gives it: _wormhol_33.wad
    std::filesystem::path host;    // the file in the Gold folder
};

// Read `folder`'s RideConfig.txt (the Gold installer's list: lines in threes, a flag, the .wad's name, its destination
// folder) and lay its rides over the game folder; an empty `folder` takes the overlay away. Returns how many rides are
// now overlaid: those the list names whose file the folder holds (a listed file that is missing is skipped, and
// `missing`, when given, counts them).
size_t UseGoldRides(const std::filesystem::path& folder, size_t* missing = nullptr);

// The rides overlaid, in RideConfig.txt order.
const std::vector<GoldRide>& GoldRides();

// Parse the text of a RideConfig.txt: each ride's name and destination folder (as GoldRide, `host` empty). Tolerant of
// CR LF, blank lines and either separator in the folder.
std::vector<GoldRide> ParseRideConfig(const std::string& text);

// The overlaid file at the game path split into `parts` (SplitGamePath), matched ignoring case; empty if none.
std::filesystem::path GoldRideHostPath(const std::vector<std::string>& parts);

// A file's last write time as the game's FILETIME (files.cpp; 0 when it cannot be read).
uint64_t FileTimeOf(const std::filesystem::path& path);

// The overlay's entries in the game folder `inside` ('/' separated from the game folder, any case): a ride's .wad in
// that folder, or a folder on the way to one that is not in the game folder itself. In name order.
std::vector<DirEntry> GoldRideEntries(const std::string& inside);

}  // namespace tpw
