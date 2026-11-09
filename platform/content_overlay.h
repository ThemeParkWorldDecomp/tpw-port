// content_overlay.h: rides of a mod's content folder, laid over the game folder (the PS1 rides mod: rides the player's
// own PlayStation disc converted to the PC's form). A content folder mirrors the game folder, and each ride in it is a
// folder of its own at data/levels/<theme>/<rides|features|sideshow>/<name>, holding <name>.sam, or for a folder named
// as the game's are (_aztec_205) the .sam of its name without the number (_aztec.sam), whose
// Info.DownloadedRideNumber and Info.Name this reads, and the files that names, as a ride the game scans for
// (CRideDatabase::ScanDirectory) would be. A lookup (files.cpp's FindHostPath) finds a file inside such a folder after
// the user folder, the asset packs and the Gold rides (gold_rides.h) and before the game folder; a listing
// (ListGameFolder) shows each ride's folder in its parent's place in the game folder, and inside it the ride's files.
// A ride whose folder the game folder has already is not laid over it (the base game's ride stays the base game's).
// Unlike the Gold rides it is a mod's: the launcher lays the folder over only with the mod on, in a build with the
// port's fixes, so with it off (or in a build without them) the game sees the game folder alone, as the original does.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "platform.h"
#include "platform/missing_rides.h"

namespace tpw {

struct ContentRide {
    std::string folder;            // the ride's folder in the game folder, '/' separated, lower case: data/levels/jungle/rides/aztec
    std::string title;             // its .sam's Info.Name: Aztec Bounce (empty when the .sam has none)
    int number = 0;                // its .sam's Info.DownloadedRideNumber: the bit a saved park needs it by (0 when none)
    std::filesystem::path host;    // the folder in the content folder
};

// The PS1 rides mod's ride numbers (Info.DownloadedRideNumber, which a park file's header keeps one bit each of, 1 to
// 256): the converter numbers its rides from here, apart from the Gold Edition's (1 to 39) and the rides the original's
// web site offered for download.
constexpr int kContentFirstNumber = 201;
constexpr int kContentLastNumber = 256;

// The rides of the content folder `root` (empty if it has none), each with its .sam read, in folder order. A ride is
// left out, and `refused` (when given) says why in one line each, when its .sam is missing or names no number in
// kContentFirstNumber..kContentLastNumber, when a ride before it has the same number, or when `gameDir` (when given)
// has a folder in its place.
std::vector<ContentRide> FindContentRides(const std::filesystem::path& root, const std::filesystem::path& gameDir = {},
                                          std::vector<std::string>* refused = nullptr);

// Lay the rides of the content folders `roots` (FindContentRides's of each, one mod's each) over the game folder, a ride
// whose number or folder one before it has refused as above; no `roots` takes the overlay away. Returns how many rides
// are now overlaid.
size_t UseContentRides(const std::vector<std::filesystem::path>& roots, const std::filesystem::path& gameDir = {},
                       std::vector<std::string>* refused = nullptr);

// The rides overlaid.
const std::vector<ContentRide>& ContentRides();

// A mod with a content folder, on or off, whose rides MissingRidesText (include/platform/missing_rides.h) names when a
// park needs one of them: the launcher tells it every such mod it has.
struct ContentMod {
    std::string name;              // the mod's name, as the launcher's Mods box shows it: PlayStation rides
    std::filesystem::path root;    // its content folder (FindContentRides's `root`; it may not exist yet)
};
void KnowContentMods(const std::vector<ContentMod>& mods);

// The overlaid ride whose folder is the game path split into `parts`, or holds it (matched ignoring case): its folder
// in the content folder, with the parts below that folder in `rest`. Empty if none.
std::filesystem::path ContentRideFolder(const std::vector<std::string>& parts, std::vector<std::string>& rest);

// The overlay's entries in the game folder `inside` ('/' separated from the game folder, any case): a ride's folder in
// its parent, or a folder on the way to one. In name order. (The files inside a ride's folder are ContentRideFolder's.)
std::vector<DirEntry> ContentEntries(const std::string& inside);

}  // namespace tpw
