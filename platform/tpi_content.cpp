// tpi_content.cpp: the TPI Worlds and Rides mod's content folder (tpi_content.h).
#include "tpi_content.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <system_error>

#include "platform.h"

namespace tpw {

namespace {

std::filesystem::path gTpiRoot;
bool gTpiRootAsked = false;   // UseTpiContent was called, or the TPW_TPI_ROOT hook read
bool gTpiForced = false;      // the TPW_TPI_ROOT hook laid a folder over

// Names in the content folder are matched ignoring case (FindHostPathUnder), as every lookup in it is (files.cpp), so a
// folder the Mac takes is taken on a disk that tells the cases apart (Linux) too.
bool HasStamp(const std::filesystem::path& root) {
    const std::filesystem::path stamp = FindHostPathUnder(root, {"converter.txt"});
    if (stamp.empty()) return false;
    std::ifstream in(stamp);
    std::string line;
    return in && std::getline(in, line) && !line.empty() && line.find_first_not_of("0123456789 \t\r") == std::string::npos &&
           line.find_first_of("123456789") != std::string::npos;
}

bool EqualNoCase(const std::string& a, const char* b) {
    if (a.size() != std::strlen(b)) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// The world's three-letter name as the game builds it: "_" and the first three letters of the folder (TPLevel.cpp).
std::string ShortName(const char* folder) { return std::string("_") + std::string(folder).substr(0, 3); }

// `name` with every occurrence of `from` (any case) replaced by `to`, the letters' case kept where both are letters.
std::string MapShortName(const std::string& name, const std::string& from, const std::string& to) {
    std::string out;
    for (size_t i = 0; i < name.size();) {
        bool match = i + from.size() <= name.size();
        for (size_t j = 0; match && j < from.size(); ++j)
            match = std::tolower(static_cast<unsigned char>(name[i + j])) == std::tolower(static_cast<unsigned char>(from[j]));
        if (!match) {
            out += name[i++];
            continue;
        }
        for (size_t j = 0; j < to.size(); ++j) {
            const bool upper = j < from.size() && std::isupper(static_cast<unsigned char>(name[i + j]));
            out += static_cast<char>(upper ? std::toupper(static_cast<unsigned char>(to[j])) : to[j]);
        }
        i += from.size();
    }
    return out;
}

bool IsOwnFolder(const std::string& name) {
    for (const char* own : kTpiOwnFolders)
        if (EqualNoCase(name, own)) return true;
    return false;
}

// The easy mode's park, Easymode.TPWI (FS_UserInfo.cpp copies it into the player's folder): "easymode" with any
// extension or none.
bool IsEasyModePark(const std::string& name) {
    const size_t dot = name.find('.');
    return EqualNoCase(name.substr(0, dot), "easymode");
}

}  // namespace

bool TpiDonorParts(const std::vector<std::string>& parts, std::vector<std::string>& donor) {
    if (parts.size() < 3 || !EqualNoCase(parts[0], "data") || !EqualNoCase(parts[1], "levels")) return false;
    const TpiWorld* world = TpiWorldOf(parts[2]);
    if (world == nullptr) return false;
    if (parts.size() > 3 && (IsOwnFolder(parts[3]) || (parts.size() == 4 && IsEasyModePark(parts[3])))) return false;
    donor = parts;
    donor[2] = world->donor;
    const std::string from = ShortName(world->folder), to = ShortName(world->donor);
    for (size_t i = 3; i < donor.size(); ++i) donor[i] = MapShortName(donor[i], from, to);
    return true;
}

std::string TpiWorldName(const TpiWorld& world, const std::string& donorName) {
    return MapShortName(donorName, ShortName(world.donor), ShortName(world.folder));
}

std::vector<std::string> FindTpiWorlds(const std::filesystem::path& root) {
    std::vector<std::string> found;
    if (root.empty() || !HasStamp(root)) return found;
    for (const TpiWorld& world : kTpiWorlds) {
        const std::filesystem::path folder = FindHostPathUnder(root, {"data", "levels", world.folder});
        std::error_code ec;
        if (!folder.empty() && std::filesystem::is_directory(folder, ec)) found.push_back(world.title);
    }
    return found;
}

bool TpiContentReady(const std::filesystem::path& root) {
    return FindTpiWorlds(root).size() == sizeof(kTpiWorlds) / sizeof(kTpiWorlds[0]);
}

namespace {

// The TPW_TPI_ROOT test hook: lay the folder it names over, when it names a whole content folder (tpi_content.h).
void LayHookOver() {
#ifdef TPW_PORT_FIX
    if (const char* hook = std::getenv("TPW_TPI_ROOT"); hook != nullptr && hook[0] != '\0') {
        const std::filesystem::path root(hook);
        if (TpiContentReady(root)) {
            gTpiRoot = root;
            gTpiForced = true;
        }
    }
#endif
}

}  // namespace

size_t UseTpiContent(const std::filesystem::path& root) {
    gTpiRootAsked = true;
    gTpiRoot.clear();
    gTpiForced = false;
    if (root.empty()) {
        LayHookOver();   // the launcher's "mod off" keeps the test hook's folder (tpi_content.h)
    } else if (TpiContentReady(root)) {
        gTpiRoot = root;   // files.cpp's lookups and listings read it (FindHostPathOf, ListGameFolder)
    }
    return gTpiRoot.empty() ? 0 : sizeof(kTpiWorlds) / sizeof(kTpiWorlds[0]);
}

const std::filesystem::path& TpiContentRoot() {
#ifdef TPW_PORT_FIX
    if (!gTpiRootAsked) {
        gTpiRootAsked = true;
        LayHookOver();
    }
    if (gTpiForced) gModTpiWorlds = true;   // again after tpw::mods::Apply set it from the Mods box
#endif
    return gTpiRoot;
}

bool TpiContentForced() {
    TpiContentRoot();   // reads the hook if nothing has yet
    return gTpiForced;
}

}  // namespace tpw
