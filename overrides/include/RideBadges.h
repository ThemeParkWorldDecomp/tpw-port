// RideBadges.h: the port's ride badges, a small corner tag on the Buy Ride panel's picture of a ride (UpdateViewWindow,
// src/game/ui/ui_ridepanel.cpp): "GOLD" on the Gold Edition's 18 rides the base game does not have, and "PS1" on the
// PlayStation rides mod's. Display only: no ride, park or saved game changes. The launcher fills the lists before the
// game starts (platform/ride_badges.h), from the Gold rides and the content rides it lays over the game folder, and sets
// the switch from its Settings box's "Show Ride Badges" (launcher.ini's ride_badges, on unless "0"). Never filled, as in
// the differential tests, every ride has none. Only with the port's fixes.
#pragma once


#include <cstdint>
#include <string>
#include <vector>

enum class RideBadge : uint8_t {
    kNone,
    kPs1,    // a PlayStation rides mod ride (tpw-mods ps1-rides, its ps1_badge hook)
    kGold,   // a Gold Edition ride the base game has no copy of
};

// TPW_PORT_FIX goldbadge gPortRideBadges | Mac: no such switch | Port: set by the launcher (the Settings box's "Show Ride Badges", on unless launcher.ini's ride_badges is 0, on by default on the maintainer's approval); set, the Buy Ride panel's picture of a Gold Edition extra ride has a "GOLD" corner tag, and of a PlayStation rides mod ride the mod's "PS1" one (UpdateViewWindow) | Why: so the player can tell which rides come from the Gold CD or the PlayStation disc; display only, the maintainer's approved exception (2026-11-08); false unless the launcher sets it, so the differential tests run the original
inline bool gPortRideBadges = false;

// TPW_PORT_FIX mods gModPs1Badge | Mac: no such flag | Port: TPW_MOD_HOOK ps1_badge, set by the launcher while a tpw-mods mod that switches it on is on (platform/mods.h); set (and gPortRideBadges with it), the PlayStation rides have the "PS1" tag | Why: tpw-mods ps1-rides: the tag is the mod's own
inline bool gModPs1Badge = false;

// The rides of each badge, by RideBadgeKey of their folder: data/levels/space/rides/_gforce_35.
inline std::vector<std::string> gRideBadgeGold;
inline std::vector<std::string> gRideBadgePs1;

// A ride's folder as one key, whichever way it is written: RideInfo::mFolder ("data:levels:space:rides:_gforce_35:", the
// .wad's name without .wad), a Gold ride's place and name (data/levels/space/rides + _gforce_35.wad) or a content ride's
// folder (data/levels/jungle/rides/aztec). Lower case, '/' separated, no separator at the end, no ".wad".
inline std::string RideBadgeKey(const std::string& folder) {
    std::string key;
    key.reserve(folder.size());
    for (char c : folder) {
        if (c == ':' || c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c == '/' && (key.empty() || key.back() == '/')) continue;
        key += c;
    }
    while (!key.empty() && key.back() == '/') key.pop_back();
    if (key.size() > 4 && key.compare(key.size() - 4, 4, ".wad") == 0) key.resize(key.size() - 4);
    return key;
}

// The badge of the ride whose folder is `folder` (RideInfo::mFolder): none while the switch is off.
inline RideBadge RideBadgeFor(const char* folder) {
    if (!gPortRideBadges || folder == nullptr || folder[0] == 0) return RideBadge::kNone;
    const std::string key = RideBadgeKey(folder);
    if (gModPs1Badge) {
        for (const std::string& ride : gRideBadgePs1)
            if (ride == key) return RideBadge::kPs1;
    }
    for (const std::string& ride : gRideBadgeGold)
        if (ride == key) return RideBadge::kGold;
    return RideBadge::kNone;
}

// The tag's text (the same in every language: the CD's and the console's names); NULL for none.
inline const char16_t* RideBadgeText(RideBadge badge) {
    switch (badge) {
    case RideBadge::kPs1: return u"PS1";
    case RideBadge::kGold: return u"GOLD";
    default: return nullptr;
    }
}

// Where the tag goes in a picture `width` x `height` pixels, for a text `textWidth` x `textHeight`: its box (right and
// bottom outside) in the top left corner, a margin and the padding around the text growing with the picture's size, so
// that it keeps its look at 640x480 and at a larger window. Empty (all 0) when it does not fit. The top left: the picture
// is the footprint over the bottom left of the ride's preview, so its right half lies over the ride and its left edge is
// the preview's; the footprint grows from the bottom left corner, so the top left is the last place it reaches.
struct RideBadgeBox {
    int32_t left = 0, top = 0, right = 0, bottom = 0;   // the tag, its 1 pixel border included
    int32_t textLeft = 0, textTop = 0;                  // where the text's top left corner goes
};
inline RideBadgeBox PlaceRideBadge(int32_t width, int32_t height, int32_t textWidth, int32_t textHeight) {
    RideBadgeBox box;
    if (width <= 0 || height <= 0 || textWidth <= 0 || textHeight <= 0) return box;
    const int32_t margin = 2 + height / 60;
    const int32_t padX = 2 + textHeight / 4;
    const int32_t padY = 1 + textHeight / 10;
    const int32_t w = textWidth + 2 * padX + 2;    // + the border
    const int32_t h = textHeight + 2 * padY + 2;
    if (w + 2 * margin > width || h + 2 * margin > height) return box;
    box.left = margin;
    box.right = box.left + w;
    box.top = margin;
    box.bottom = box.top + h;
    box.textLeft = box.left + 1 + padX;
    box.textTop = box.top + 1 + padY;
    return box;
}

// Draws `badge`'s tag on the current render area, a picture `width` x `height` pixels, in its top left corner
// (PlaceRideBadge); nothing for kNone (src/game/ui/ui_ridebadge.cpp).
void DrawRideBadge(RideBadge badge, int32_t width, int32_t height);

