// content_overlay.cpp: a mod's content rides laid over the game folder (content_overlay.h).
#include "content_overlay.h"
#include "path_text.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
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

static bool LessNoCase(const std::string& a, const std::string& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
    });
}

// The entries of `folder` that are folders (`folders`) or files, in name order (ignoring case, then by case), so a scan
// is the same on every system.
static std::vector<fs::path> Children(const fs::path& folder, bool folders) {
    std::vector<fs::path> children;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code kind;
        const std::string name = PathText(it->path().filename());
        if (name.empty() || name[0] == '.') continue;
        if (folders ? it->is_directory(kind) : it->is_regular_file(kind)) children.push_back(it->path());
    }
    std::sort(children.begin(), children.end(),
              [](const fs::path& a, const fs::path& b) {
                  const std::string x = PathText(a.filename()), y = PathText(b.filename());
                  return LessNoCase(x, y) || (!LessNoCase(y, x) && x < y);
              });
    return children;
}

// Every `name` inside `folder`, matched ignoring case (more than one only where the system tells case apart).
static std::vector<fs::path> ChildrenNoCase(const fs::path& folder, const std::string& name, bool isFolder) {
    std::vector<fs::path> found;
    for (const fs::path& child : Children(folder, isFolder))
        if (Lower(PathText(child.filename())) == Lower(name)) found.push_back(child);
    return found;
}

// `name` inside `folder`, matched ignoring case; empty if none.
static fs::path ChildNoCase(const fs::path& folder, const std::string& name, bool isFolder) {
    const std::vector<fs::path> found = ChildrenNoCase(folder, name, isFolder);
    return found.empty() ? fs::path() : found.front();
}

// Whether `folder` has the folders `parts` (from `at`), one inside the next, each matched ignoring case.
static bool HasFoldersNoCase(const fs::path& folder, const std::vector<std::string>& parts, size_t at = 0) {
    if (at == parts.size()) return true;
    for (const fs::path& child : ChildrenNoCase(folder, parts[at], true))
        if (HasFoldersNoCase(child, parts, at + 1)) return true;
    return false;
}

// A .sam's Info.DownloadedRideNumber and Info.Name ("Aztec Bounce", without its quotes).
static void ReadSam(const fs::path& sam, int& number, std::string& title) {
    std::ifstream in(sam, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        line = Trim(line);
        size_t gap = 0;
        while (gap < line.size() && !std::isspace(static_cast<unsigned char>(line[gap]))) ++gap;
        const std::string key = Lower(line.substr(0, gap)), value = Trim(line.substr(gap));
        if (key == "info.downloadedridenumber") number = std::atoi(value.c_str());
        else if (key == "info.name" && value.size() >= 2 && value.front() == '"' && value.back() == '"')
            title = value.substr(1, value.size() - 2);
        else if (key == "info.name") title = value;
    }
}

// The rides of the content folder `root`, after `rides` (another content folder's, whose numbers and folders they
// cannot have again).
static void ScanContent(const fs::path& root, const fs::path& gameDir, std::vector<std::string>* refused,
                        std::vector<ContentRide>& rides) {
    if (root.empty()) return;
    std::vector<fs::path> themes;
    for (const fs::path& data : ChildrenNoCase(root, "data", true))
        for (const fs::path& levels : ChildrenNoCase(data, "levels", true))
            for (const fs::path& theme : Children(levels, true)) themes.push_back(theme);
    for (const fs::path& theme : themes) {
        for (const char* kind : {"rides", "features", "sideshow"}) {
            for (const fs::path& place : ChildrenNoCase(theme, kind, true)) {
                for (const fs::path& host : Children(place, true)) {
                    const std::string name = PathText(host.filename());
                    ContentRide ride;
                    ride.folder = "data/levels/" + Lower(PathText(theme.filename())) + "/" + kind + "/" + Lower(name);
                    ride.host = host;
                    // <name>.sam, or for a folder named as the game's are, "_<stem>_<number>", _<stem>.sam (the game's item
                    // name drops the "_<number>" of a name with two underscores, CTPItemResource::InitItem)
                    fs::path sam = ChildNoCase(host, name + ".sam", false);
                    const size_t last = name.rfind('_');
                    if (sam.empty() && name.size() > 1 && name[0] == '_' && std::count(name.begin(), name.end(), '_') == 2 &&
                        last + 1 < name.size() &&
                        std::all_of(name.begin() + static_cast<std::ptrdiff_t>(last) + 1, name.end(),
                                    [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }))
                        sam = ChildNoCase(host, name.substr(0, last) + ".sam", false);
                    auto refuse = [&](const std::string& why) {
                        if (refused != nullptr) refused->push_back(ride.folder + ": " + why);
                    };
                    if (sam.empty()) {
                        refuse("no " + name + ".sam");
                        continue;
                    }
                    ReadSam(sam, ride.number, ride.title);
                    if (ride.number < kContentFirstNumber || ride.number > kContentLastNumber) {
                        refuse("Info.DownloadedRideNumber " + std::to_string(ride.number) + " is not " +
                               std::to_string(kContentFirstNumber) + " to " + std::to_string(kContentLastNumber));
                        continue;
                    }
                    const auto same = std::find_if(rides.begin(), rides.end(),
                                                   [&](const ContentRide& r) { return r.number == ride.number; });
                    if (same != rides.end()) {
                        refuse("Info.DownloadedRideNumber " + std::to_string(ride.number) + " is " + same->folder + "'s too");
                        continue;
                    }
                    if (std::any_of(rides.begin(), rides.end(), [&](const ContentRide& r) { return r.folder == ride.folder; })) {
                        refuse("another content folder has it");
                        continue;
                    }
                    // The game folder's own ride of that name stays the game's.
                    if (!gameDir.empty() &&
                        HasFoldersNoCase(gameDir, {"data", "levels", PathText(theme.filename()), std::string(kind), name})) {
                        refuse("the game folder has a folder in its place");
                        continue;
                    }
                    rides.push_back(ride);
                }
            }
        }
    }
}

std::vector<ContentRide> FindContentRides(const fs::path& root, const fs::path& gameDir, std::vector<std::string>* refused) {
    std::vector<ContentRide> rides;
    ScanContent(root, gameDir, refused, rides);
    return rides;
}

static std::vector<ContentRide>& Overlay() {
    static std::vector<ContentRide> rides;
    return rides;
}

const std::vector<ContentRide>& ContentRides() { return Overlay(); }

size_t UseContentRides(const std::vector<fs::path>& roots, const fs::path& gameDir, std::vector<std::string>* refused) {
    Overlay().clear();
    for (const fs::path& root : roots) ScanContent(root, gameDir, refused, Overlay());
    return Overlay().size();
}

fs::path ContentRideFolder(const std::vector<std::string>& parts, std::vector<std::string>& rest) {
    rest.clear();
    if (Overlay().empty() || parts.empty()) return {};
    std::string path;
    for (size_t i = 0; i < parts.size(); ++i) {
        path += (i == 0 ? "" : "/") + Lower(parts[i]);
        for (const ContentRide& ride : Overlay()) {
            if (path != ride.folder) continue;
            rest.assign(parts.begin() + static_cast<std::ptrdiff_t>(i) + 1, parts.end());
            return ride.host;
        }
    }
    return {};
}

std::vector<DirEntry> ContentEntries(const std::string& inside) {
    std::vector<DirEntry> entries;
    if (Overlay().empty()) return entries;
    std::string folder;
    for (char c : Lower(inside)) {
        if (c == '\\' || c == '/' || c == ':') {
            if (!folder.empty() && folder.back() != '/') folder.push_back('/');
        }
        else folder.push_back(c);
    }
    if (!folder.empty() && folder.back() == '/') folder.pop_back();
    const std::string prefix = folder.empty() ? std::string() : folder + "/";
    for (const ContentRide& ride : Overlay()) {
        if (ride.folder.compare(0, prefix.size(), prefix) != 0 || ride.folder.size() == prefix.size()) continue;
        // The ride's own folder, by its name in the content folder, or a folder on the way to it.
        const std::string rest = ride.folder.substr(prefix.size());
        const bool own = rest.find('/') == std::string::npos;
        const std::string child = own ? PathText(ride.host.filename()) : rest.substr(0, rest.find('/'));
        const bool seen = std::any_of(entries.begin(), entries.end(),
                                      [&](const DirEntry& e) { return Lower(e.name) == Lower(child); });
        if (!seen) entries.push_back({child, true, 0, 0});
    }
    std::sort(entries.begin(), entries.end(), [](const DirEntry& a, const DirEntry& b) { return LessNoCase(a.name, b.name); });
    return entries;
}

static std::vector<ContentMod>& KnownMods() {
    static std::vector<ContentMod> mods;
    return mods;
}

void KnowContentMods(const std::vector<ContentMod>& mods) { KnownMods() = mods; }

// "a", "a and b", "a, b and c".
static std::string JoinAnd(const std::vector<std::string>& items) {
    std::string text;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) text += i + 1 == items.size() ? " and " : ", ";
        text += items[i];
    }
    return text;
}

static std::string Count(size_t count, const std::string& one) {
    return std::to_string(count) + " " + one + (count == 1 ? "" : "s");
}

std::string MissingRidesText(const uint8_t* missing, MissingRidesPlace place) {
    size_t gold = 0, unknown = 0;
    // each known mod's titles of the rides the park needs, in the mods' order
    std::vector<std::vector<std::string>> named(KnownMods().size());
    std::vector<std::vector<ContentRide>> scanned(KnownMods().size());
    for (size_t i = 0; i < KnownMods().size(); ++i) scanned[i] = FindContentRides(KnownMods()[i].root);
    for (int number = 1; number <= 256; ++number) {
        if ((missing[(number - 1) >> 3] & (1 << ((number - 1) & 7))) == 0) continue;
        if (number < kContentFirstNumber) {
            ++gold;
            continue;
        }
        bool found = false;
        for (size_t i = 0; i < scanned.size() && !found; ++i) {
            for (const ContentRide& ride : scanned[i]) {
                if (ride.number != number) continue;
                named[i].push_back(ride.title.empty() ? "ride " + std::to_string(number) : ride.title);
                found = true;
                break;
            }
        }
        if (!found) ++unknown;
    }
    std::vector<std::string> parts;
    if (gold > 0) parts.push_back(Count(gold, "Gold Edition ride"));
    for (size_t i = 0; i < named.size(); ++i) {
        if (named[i].empty()) continue;
        std::vector<std::string> titles(named[i].begin(), named[i].begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(named[i].size(), 4)));
        if (named[i].size() > 4) titles.push_back(std::to_string(named[i].size() - 4) + " more");
        parts.push_back(JoinAnd(titles) + " (" + KnownMods()[i].name + ")");
    }
    if (unknown > 0) parts.push_back(Count(unknown, "ride") + " made from a PlayStation disc");
    std::string text = "This park needs rides this game hasn't got: " + JoinAnd(parts) + ". ";
    switch (place) {
    case MissingRidesPlace::kOwnPark:
        text += "It has been kept as it was. Turn those rides on in the launcher, then play it again.";
        break;
    case MissingRidesPlace::kOnlinePark:
        text += "Turn those rides on in the launcher to visit it.";
        break;
    case MissingRidesPlace::kLoadedPark:
        text += "Turn those rides on in the launcher to load it.";
        break;
    }
    // the error dialog shows 511 characters at most (UIMain_ConvertMulti); titles are a .sam's, so cut a long one short
    if (text.size() > 500) text = text.substr(0, 497) + "...";
    return text;
}

}  // namespace tpw
