// ride_badges.cpp: which rides have a badge (ride_badges.h).
#include "ride_badges.h"

#ifdef TPW_PORT_FIX

#include <algorithm>
#include <cctype>
#include <system_error>

#include "RideBadges.h"
#include "path_text.h"

namespace fs = std::filesystem;

namespace tpw {

namespace {

bool SameIgnoringCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// Whether `root`/`relative` ('/' separated, from `start`) is there, each name matched ignoring case (any of the names
// that match, where the system tells case apart).
bool HasIgnoringCase(const fs::path& root, const std::string& relative, size_t start = 0) {
    while (start < relative.size() && relative[start] == '/') ++start;
    if (start >= relative.size()) return true;
    const size_t end = std::min(relative.find('/', start), relative.size());
    const std::string name = relative.substr(start, end - start);
    std::error_code err;
    if (fs::exists(root / name, err) && HasIgnoringCase(root / name, relative, end)) return true;
    for (fs::directory_iterator it(root, err), last; !err && it != last; it.increment(err)) {
        const std::string other = PathText(it->path().filename());
        if (other != name && SameIgnoringCase(other, name) && HasIgnoringCase(it->path(), relative, end)) return true;
    }
    return false;
}

// Whether `path` is `root` or inside it, by their names (both lexically normal; a trailing separator on `root` allowed).
bool Within(const fs::path& path, const fs::path& root) {
    auto at = path.begin();
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (it->empty()) continue;   // the empty last name of a root ending in a separator
        if (at == path.end() || *at != *it) return false;
        ++at;
    }
    return !root.empty();
}

}  // namespace

std::vector<std::string> GoldExtraRideKeys(const std::vector<GoldRide>& rides, const fs::path& gameDir) {
    std::vector<std::string> keys;
    if (gameDir.empty()) return keys;   // no game folder to tell them apart by
    for (const GoldRide& ride : rides) {
        if (ride.name.empty()) continue;
        const std::string place = ride.folder + "/" + ride.name;
        if (HasIgnoringCase(gameDir, place)) continue;
        keys.push_back(RideBadgeKey(place));
    }
    return keys;
}

std::vector<std::string> ContentRideKeys(const std::vector<ContentRide>& rides, const std::vector<fs::path>& roots) {
    std::vector<std::string> keys;
    for (const ContentRide& ride : rides) {
        if (ride.folder.empty()) continue;
        const fs::path host = ride.host.lexically_normal();
        for (const fs::path& root : roots) {
            if (Within(host, root.lexically_normal())) {
                keys.push_back(RideBadgeKey(ride.folder));
                break;
            }
        }
    }
    return keys;
}

void UseRideBadges(bool show, const fs::path& gameDir, const std::vector<fs::path>& ps1Roots) {
    gPortRideBadges = show;
    gRideBadgeGold.clear();
    gRideBadgePs1.clear();
    if (!show) return;
    gRideBadgeGold = GoldExtraRideKeys(GoldRides(), gameDir);
    gRideBadgePs1 = ContentRideKeys(ContentRides(), ps1Roots);
}

}  // namespace tpw

#endif  // TPW_PORT_FIX
