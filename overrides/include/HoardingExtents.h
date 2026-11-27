// HoardingExtents.h: the park fence's extents (gLLCCHoardMinX .. gLLCCHoardMaxY, src/game/park/LLCellRide.cpp), which
// FLY_SetFlyZone turns into the fly zone every 32 ticks (GAMESYSTEM_Tick). Only LLCC_BuildHoardingDB writes them, at a level's
// start (LLCC_DrawInitialHoarding) and when land is bought (LLCC_ReviseHoarding); loading a park never does, so a park loaded
// after its land grew kept the level's own fence as its fly zone, and a tour based on the bought land flew off the map.
// HoardingWalkExtents walks the loaded fence as LLCC_BuildHoardingDB does, with the hoarding links read (gLLCC4818 set),
// and gives the extents that walk would record, changing no cell: a crossroad's links, which the original cuts as it leaves
// it, are cut only in the walk's own copy. A walk that would never end (a hole in the fence: the original stands still
// there forever), leaves the grid or takes more steps than the grid has posts answers false. HoardingExtentsFromFlyZone
// gives the extents FLY_SetFlyZone makes a fly zone from. Both are header only and pure, so the host tests see what the game
// does; RecalculateHoardingExtents (src/game/park/LLCellRide.cpp, beside the extents' writer) is the load's pass that uses them.
#pragma once
// TPW_PORT_FIX flyzoneload HoardingWalkExtents | Mac: no such walk: a loaded park keeps the fence extents of the level's start, and the fly zone made from them every 32 ticks overwrites the one the park saved | Port: CSaveLoad::PostLoadOperations walks the loaded fence read only and records its extents, or, when the walk does not close, the extents the saved fly zone was made from | Why: a flying tour (Jurassic Tours, the SimCoaster/TPI mod's Flying Pelican) based on land bought after the level's start flew diagonally off the map for its ride time after a load, then as long again back (Addable13)
#include <cstdint>
namespace tpw {

struct HoardingExtents {
    int32_t minX, minY, maxX, maxY;
};

// One post of the walk: its track cell's type and its selected links (mHoardingNeighbours for a track cell with gLLCC4818 set).
struct HoardingPost {
    int32_t type;
    uint8_t links;
};

constexpr int32_t kHoardingSide = 128;                                      // the track grid is 128 x 128 cells
constexpr int32_t kHoardingWalkCap = (kHoardingSide / 2) * (kHoardingSide / 2) + 4;   // a step per post of the walk's lattice

// OPPOSITE and ADD_DIRECTION (src/game/park/Map.cpp), as the walk calls them.
inline uint8_t HoardingOpposite(uint8_t d) {
    if (d == 0) return 0;
    if (d < 0x10) return static_cast<uint8_t>((d << 4) & 0xf0);
    return static_cast<uint8_t>((d >> 4) & 0xff);
}

inline uint8_t HoardingAddDirection(uint8_t a, uint8_t b) {
    int32_t n = 0;
    int32_t first = a;
    while (static_cast<uint8_t>(first) > 1) {
        first = first >> 1;
        ++n;
    }
    const int32_t v = static_cast<int32_t>(static_cast<uint32_t>(b) << n);
    if (v > 0x80) return static_cast<uint8_t>((v >> 8) & 0xff);
    return static_cast<uint8_t>(v & 0xff);
}

inline int32_t HoardingHVCount(uint8_t links) {
    return ((links & 1) != 0) + ((links & 4) != 0) + ((links & 0x10) != 0) + ((links & 0x40) != 0);
}

// CTrackCell::IsCorner and IsCrossRoad (src/game/park/TrackCell.cpp) of a links byte.
inline bool HoardingIsCorner(uint8_t links) {
    if (HoardingHVCount(links) != 2) return false;
    return ((links & 1) != 0 && (links & 4) != 0) || ((links & 1) != 0 && (links & 0x40) != 0) ||
           ((links & 0x10) != 0 && (links & 4) != 0) || ((links & 0x10) != 0 && (links & 0x40) != 0);
}

inline bool HoardingIsCrossRoad(uint8_t links) { return HoardingHVCount(links) > 2; }

// The walk of LLCC_BuildHoardingDB(right, bottom, left, top, 4, true) with gLLCC4818 set, over the posts `post(x, y)` gives
// (x and y from 0 to 127): true with the extents it records in *out, false when it would not end (*out unchanged).
template <class PostAt>
bool HoardingWalkExtents(int32_t right, int32_t bottom, int32_t left, int32_t top, PostAt post, HoardingExtents* out) {
    struct Cut {
        int32_t index;
        uint8_t links;
    };
    Cut cuts[64];   // the crossroads the walk has left, with the links the original leaves them
    int32_t cutCount = 0;
    int32_t x = right, y = bottom;
    uint8_t dir = 4;
    HoardingExtents e{0x400, 0x400, 0, 0};
    bool done = false;
    for (int32_t steps = 0; !done; ++steps) {
        if (steps >= kHoardingWalkCap) return false;
        if (x < 0 || x >= kHoardingSide || y < 0 || y >= kHoardingSide) return false;
        const int32_t index = x + (y << 7);
        HoardingPost cell = post(x, y);
        int32_t cut = -1;
        for (int32_t i = 0; i < cutCount; ++i)
            if (cuts[i].index == index) cut = i;
        if (cut >= 0) cell.links = cuts[cut].links;
        if (cell.type == 0x19 || cell.links != 0) {
            if (HoardingIsCorner(cell.links)) {
                uint8_t other = HoardingOpposite(dir);
                if ((other & cell.links) == 0) other = dir;
                dir = static_cast<uint8_t>(other ^ cell.links);
            }
            if (HoardingIsCrossRoad(cell.links)) {
                uint8_t found = 0;
                uint8_t probe = HoardingAddDirection(dir, 0x40);
                for (int32_t turn = 0; turn < 8 && found == 0; ++turn) {   // the original turns until it finds a link
                    if (probe & cell.links) found = probe;
                    probe = HoardingAddDirection(probe, 4);
                }
                if (found == 0) return false;
                uint8_t links = cell.links;
                if (dir == found) {
                    links = static_cast<uint8_t>(links & (dir | HoardingOpposite(dir)));
                } else {
                    links = static_cast<uint8_t>(links & ~dir);
                    links = static_cast<uint8_t>(links & ~HoardingOpposite(found));
                }
                if (cut < 0) {
                    if (cutCount == 64) return false;
                    cut = cutCount++;
                    cuts[cut].index = index;
                }
                cuts[cut].links = links;
                dir = found;
            }
        } else {
            dir = 0;
        }
        if (x == left && y == top) done = true;
        if (!done && dir == 0) return false;   // the original stands on this cell forever
        switch (dir) {
            case 1: y -= 2; break;
            case 4: x += 2; break;
            case 0x10: y += 2; break;
            case 0x40: x -= 2; break;
            default: break;
        }
        if (x < e.minX) e.minX = x;
        if (y < e.minY) e.minY = y;
        if (x > e.maxX) e.maxX = x;
        if (y > e.maxY) e.maxY = y;
    }
    *out = e;
    return true;
}

// FLY_SetFlyZone's sums (src/game/rides/FlyingRide.cpp): the low edges (v * 10 + 0x23) * 300 and the high ones (v * 10 - 0x23) * 300.
inline int32_t FlyZoneLow(int32_t v) { return static_cast<int32_t>((static_cast<uint32_t>(v) * 10u + 0x23u) * 300u); }
inline int32_t FlyZoneHigh(int32_t v) { return static_cast<int32_t>((static_cast<uint32_t>(v) * 10u - 0x23u) * 300u); }

// The extents FLY_SetFlyZone makes the fly zone (B, C, D, A) = (min x, min y, max x, max y) from, each from -0x400 to 0x400:
// true with them in *out when there are such extents, else false (*out unchanged).
inline bool HoardingExtentsFromFlyZone(int32_t b, int32_t c, int32_t d, int32_t a, HoardingExtents* out) {
    const auto back = [](int32_t zone, bool low, int32_t* v) {
        const int64_t tenths = static_cast<int64_t>(zone) / 300 + (low ? -0x23 : 0x23);
        const int64_t guess = tenths / 10;
        if (guess < -0x400 || guess > 0x400) return false;
        *v = static_cast<int32_t>(guess);
        return (low ? FlyZoneLow(*v) : FlyZoneHigh(*v)) == zone;
    };
    HoardingExtents e{};
    if (!back(b, true, &e.minX) || !back(c, true, &e.minY) || !back(d, false, &e.maxX) || !back(a, false, &e.maxY)) return false;
    *out = e;
    return true;
}

// After a park is loaded (CSaveLoad::PostLoadOperations): the extents of the loaded fence's walk, or, when it does not close,
// those the saved fly zone was made from (the level's are kept when the park saved none), then the fly zone made from them
// (FLY_SetFlyZone). The extents and where they came from go to the log.
void RecalculateHoardingExtents();

}  // namespace tpw
