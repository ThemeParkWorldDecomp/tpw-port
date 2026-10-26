// gold_rides.cpp: the Gold Edition rides laid over the game folder (gold_rides.h).
#include "gold_rides.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>


namespace fs = std::filesystem;

namespace tpw {

static std::string Lower(const std::string& text) {
    std::string lower = text;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower;
}

static std::string Trim(const std::string& text) {
    size_t begin = 0, end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

// A destination folder as RideConfig.txt writes it (data\levels\space\rides, from the PC's game folder) as a '/'
// separated lower case path, without a leading, trailing or doubled separator.
static std::string FolderKey(const std::string& folder) {
    std::string key;
    for (char c : Lower(folder)) {
        if (c == '\\' || c == '/' || c == ':') {
            if (!key.empty() && key.back() != '/') key.push_back('/');
        }
        else key.push_back(c);
    }
    if (!key.empty() && key.back() == '/') key.pop_back();
    return key;
}

std::vector<GoldRide> ParseRideConfig(const std::string& text) {
    // Lines in threes: "x" (the installer's flag for a ride to install), the .wad's name, its destination folder. A
    // ride's name and folder are taken from the two non-flag lines in order, so a missing or doubled flag line does
    // not shift the rest.
    std::vector<GoldRide> rides;
    std::istringstream in(text);
    std::string line, name;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty()) continue;
        if (Lower(line) == "x") {
            name.clear();
            continue;
        }
        if (name.empty()) {
            name = line;
            continue;
        }
        rides.push_back({FolderKey(line), name, {}});
        name.clear();
    }
    return rides;
}

static std::vector<GoldRide>& Overlay() {
    static std::vector<GoldRide> rides;
    return rides;
}

const std::vector<GoldRide>& GoldRides() { return Overlay(); }

// `name` inside `folder`, matched ignoring case (files.cpp's FindChild, for the Gold folder).
static fs::path ChildNoCase(const fs::path& folder, const std::string& name) {
    std::error_code ec;
    const fs::path exact = folder / name;
    if (fs::is_regular_file(exact, ec)) return exact;
    const std::string lower = Lower(name);
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code kind;
        if (it->is_regular_file(kind) && Lower(it->path().filename().string()) == lower) return it->path();
    }
    return {};
}

size_t UseGoldRides(const fs::path& folder, size_t* missing) {
    std::vector<GoldRide>& overlay = Overlay();
    overlay.clear();
    if (missing != nullptr) *missing = 0;
    if (folder.empty()) return 0;
    const fs::path list = ChildNoCase(folder, "RideConfig.txt");
    if (list.empty()) return 0;
    std::ifstream in(list, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    for (GoldRide ride : ParseRideConfig(text.str())) {
        if (ride.folder.empty() || ride.name.empty() || ride.name[0] == '.') continue;
        ride.host = ChildNoCase(folder, ride.name);
        if (ride.host.empty()) {
            if (missing != nullptr) ++*missing;
            continue;
        }
        // The installer copied each file once; a name listed twice is one file.
        const bool seen = std::any_of(overlay.begin(), overlay.end(), [&](const GoldRide& r) {
            return r.folder == ride.folder && Lower(r.name) == Lower(ride.name);
        });
        if (!seen) overlay.push_back(ride);
    }
    return overlay.size();
}

static std::string JoinLower(const std::vector<std::string>& parts) {
    std::string path;
    for (const std::string& part : parts) path += (path.empty() ? "" : "/") + Lower(part);
    return path;
}

fs::path GoldRideHostPath(const std::vector<std::string>& parts) {
    if (Overlay().empty() || parts.empty()) return {};
    const std::string path = JoinLower(parts);
    for (const GoldRide& ride : Overlay())
        if (path == ride.folder + "/" + Lower(ride.name)) return ride.host;
    return {};
}

std::vector<DirEntry> GoldRideEntries(const std::string& inside) {
    std::vector<DirEntry> entries;
    if (Overlay().empty()) return entries;
    const std::string folder = FolderKey(inside);
    const std::string prefix = folder.empty() ? std::string() : folder + "/";
    for (const GoldRide& ride : Overlay()) {
        if (ride.folder == folder) {
            std::error_code ec;
            const uint64_t size = fs::file_size(ride.host, ec);
            entries.push_back({ride.name, false, ec ? 0 : size, FileTimeOf(ride.host)});
        }
        else if (ride.folder.compare(0, prefix.size(), prefix) == 0) {
            // A folder on the way to the ride (the base game has every one of them; listed for a folder it lacks).
            const std::string rest = ride.folder.substr(prefix.size());
            const std::string child = rest.substr(0, rest.find('/'));
            const bool seen = std::any_of(entries.begin(), entries.end(),
                                          [&](const DirEntry& e) { return Lower(e.name) == child; });
            if (!seen) entries.push_back({child, true, 0, 0});
        }
    }
    std::sort(entries.begin(), entries.end(), [](const DirEntry& a, const DirEntry& b) {
        return std::lexicographical_compare(a.name.begin(), a.name.end(), b.name.begin(), b.name.end(),
                                            [](char x, char y) {
                                                return std::tolower(static_cast<unsigned char>(x)) <
                                                       std::tolower(static_cast<unsigned char>(y));
                                            });
    });
    return entries;
}

}  // namespace tpw
