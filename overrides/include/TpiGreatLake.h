// TpiGreatLake.h: the TPI Worlds and Rides mod's Great Lake (TPW_MOD_HOOK tpi_worlds): in the Land of Invention the
// Great Lake (Theme Park Inc's vict_hs, a bumper ride of kind -11 with boats) is placed in front of the lake, and its
// boats leave the ride's plot and sail on the lake, where they stay.
// Theme Park Inc's bumper.sam for the ride gives the circles its boats may sail in (ColObjects) and the buoys they sail
// to (Buoys), both in map units of 10 a cell, laid out on the lake in front of the ride's fixed spot. Theme Park World's
// kind -11 has one circle on the ride's plot and a ring of 8 buoys in it. The converter (assets/tpi_convert.cpp,
// TpiLakeSam) writes the lake's water cells, its water level and those circles and buoys into the world as TpiLake.sam;
// CWorld::SetupNewLevel reads it (TpiParseLake) into gTpiLake.
// Placement (LLCR_PlaceRideBlock): the ride may be placed only where every cell behind its footprint (the row past the
// edge away from its entrance and exit) is lake water (TpiLakeBackOnWater).
// The ride (BMP_CreateRide): its circle on the plot is joined to Theme Park Inc's circles on the lake by a chain of
// circles (TpiLakeBuild): one on the plot's back row, then one a cell along paths of water cells to each of Theme Park
// Inc's circles. Every circle stays within the lake's water and the plot, so a boat's centre never goes over land
// (BMP_InsideArea keeps a boat inside the circles). The ring's buoys move to Theme Park Inc's buoys.
// Steering (SetNavRoute, TpiLakeAim): a boat whose target is in another circle steers to the middle of the overlap of
// its circle and the next circle on the shortest chain to the target's circle, so it never runs into the shore.
// Unloading (BMP_AI, the Mac's own, no hook): when the ride's time is up a boat unloads where it is, its riders
// beamed off, and waits on the lake; the next riders are beamed onto a waiting boat (BMP_AddJoiningListToBump), as in
// Theme Park Inc. Before 0.6.3 a hook sailed each boat back to the plot's circle to unload under the station.
// Drawing (BMP_DrawCarsForRide): a boat over water floats at the water level, a little above it so its wake shows.
// Header only and pure, so the host test sees what the game does.
#pragma once


#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "TpiGateFlow.h"

namespace tpw {

inline constexpr const char* kTpiLakeFile = "TpiLake.sam";
inline constexpr int32_t kTpiLakeCell = 3072;    // a bump's units a map cell (BumperRide: 10 * 307.2)
inline constexpr int32_t kTpiLakeMap = 128;      // map cells a side
inline constexpr float kTpiLakeLift = 0.3f;      // a boat over water floats this far above the water level (world units)
inline constexpr size_t kTpiLakeMaxNodes = 250;  // circles in one ride's chain (the route table's bytes)

struct TpiLakeCircle {
    int32_t x = 0, y = 0, r = 0;   // a bump's units
};
struct TpiLakePoint {
    int32_t x = 0, y = 0;
};

// One world's lake, from its TpiLake.sam.
struct TpiLake {
    bool loaded = false;
    int32_t rideId = -1;      // the Great Lake's object id
    float waterLevel = 0;     // world units
    std::vector<uint8_t> water = std::vector<uint8_t>(kTpiLakeMap * kTpiLakeMap, 0);
    std::vector<TpiLakeCircle> circles;   // Theme Park Inc's, in a bump's units
    std::vector<TpiLakePoint> buoys;
    bool Water(int32_t x, int32_t y) const {
        return x >= 0 && y >= 0 && x < kTpiLakeMap && y < kTpiLakeMap && water[static_cast<size_t>(y * kTpiLakeMap + x)] != 0;
    }
    // Back to not loaded in place: the water map keeps its storage, so a new level allocates nothing the Mac does not.
    void Clear() {
        loaded = false;
        rideId = -1;
        waterLevel = 0;
        water.assign(water.size(), 0);
        circles.clear();
        buoys.clear();
    }
};

// TPW_PORT_FIX mods gTpiLake | Mac: none | Port: TPW_MOD_HOOK tpi_worlds (gModTpiWorlds, include/TpiWorlds.h): the lake of the world being played, read by CWorld::SetupNewLevel from the world's TpiLake.sam in one of the mod's worlds (TpiParseLake) and cleared in any other | Why: tpw-mods tpi-worlds, the Great Lake's boats sail on the lake | Off: not loaded, unread
inline TpiLake gTpiLake;

namespace detail {

inline bool LakeNumber(const char*& p, const char* end, double& value) {
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    const char* q = p;
    bool negative = false;
    if (q < end && (*q == '-' || *q == '+')) negative = *q++ == '-';
    double v = 0.0;
    int32_t digits = 0;
    while (q < end && *q >= '0' && *q <= '9') {
        v = v * 10.0 + (*q++ - '0');
        ++digits;
    }
    if (q < end && *q == '.') {
        ++q;
        double scale = 0.1;
        while (q < end && *q >= '0' && *q <= '9') {
            v += (*q++ - '0') * scale;
            scale *= 0.1;
            ++digits;
        }
    }
    if (digits == 0) return false;
    value = negative ? -v : v;
    p = q;
    return true;
}

// Theme Park Inc's map units (10 a cell) in a bump's.
inline int32_t LakeUnits(double v) { return static_cast<int32_t>(std::lround(v * kTpiLakeCell / 10.0)); }

}  // namespace detail

// Reads TpiLake.sam's text (whatever the locale's decimal point). Loaded when it names the ride and some water.
inline bool TpiParseLake(const char* text, size_t size, TpiLake& lake) {
    lake = TpiLake{};
    const char* p = text;
    const char* end = text + size;
    bool anyWater = false;
    while (p < end) {
        const char* line = p;
        while (p < end && *p != '\n') ++p;
        const char* lineEnd = p;
        if (p < end) ++p;
        while (line < lineEnd && (*line == ' ' || *line == '\t')) ++line;
        const char* key = line;
        while (line < lineEnd && *line != ' ' && *line != '\t' && *line != '\r') ++line;
        const size_t keyLength = static_cast<size_t>(line - key);
        auto is = [&](const char* name) { return std::strlen(name) == keyLength && std::memcmp(key, name, keyLength) == 0; };
        double v[3];
        auto numbers = [&](int count) {
            for (int i = 0; i < count; ++i)
                if (!detail::LakeNumber(line, lineEnd, v[i])) return false;
            return true;
        };
        if (is("Lake.RideId") && numbers(1)) {
            lake.rideId = static_cast<int32_t>(v[0]);
        } else if (is("Lake.WaterLevel") && numbers(1)) {
            lake.waterLevel = static_cast<float>(v[0]);
        } else if (is("Lake.Water") && numbers(3)) {
            const int32_t y = static_cast<int32_t>(v[0]);
            for (int32_t x = static_cast<int32_t>(v[1]); x <= static_cast<int32_t>(v[2]); ++x) {
                if (x < 0 || y < 0 || x >= kTpiLakeMap || y >= kTpiLakeMap) continue;
                lake.water[static_cast<size_t>(y * kTpiLakeMap + x)] = 1;
                anyWater = true;
            }
        } else if (is("Lake.Circle") && numbers(3)) {
            lake.circles.push_back({detail::LakeUnits(v[0]), detail::LakeUnits(v[1]), detail::LakeUnits(v[2])});
        } else if (is("Lake.Buoy") && numbers(2)) {
            lake.buoys.push_back({detail::LakeUnits(v[0]), detail::LakeUnits(v[1])});
        }
    }
    lake.loaded = lake.rideId >= 0 && anyWater;
    return lake.loaded;
}

// The ride's placement is allowed: every cell behind its footprint, place(col, height) for each column, is water.
// `place(col, row, cx, cy)` maps a footprint cell to the map's for the rotation (LLCR_PlaceRideBlock's).
template <class Place>
inline bool TpiLakeBackOnWater(const TpiLake& lake, int32_t width, int32_t height, Place place) {
    if (width <= 0 || height <= 0) return false;
    for (int32_t col = 0; col < width; ++col) {
        int32_t cx = -1, cy = -1;
        place(col, height, cx, cy);
        if (!lake.Water(cx, cy)) return false;
    }
    return true;
}

// The direction behind the ride, in cells, for its bumper rotation kind (LLCR_AddRideToDB: 0 north, 5 east, 6 south,
// 1 west). False for any other.
inline bool TpiLakeBack(int32_t rotKind, int32_t& dx, int32_t& dy) {
    dx = 0;
    dy = 0;
    switch (rotKind) {
    case 0: dy = 1; return true;
    case 6: dy = -1; return true;
    case 5: dx = 1; return true;
    case 1: dx = -1; return true;
    default: return false;
    }
}

// A ride's chain of circles: node 0 the plot's own, the route table and the buoys.
struct TpiLakeNet {
    std::vector<TpiLakeCircle> nodes;
    std::vector<uint8_t> next;    // next[i * n + j]: the node after i on a shortest chain from i to j
    std::vector<TpiLakePoint> buoys;
    bool Empty() const { return nodes.size() < 2; }
    int32_t Next(int32_t i, int32_t j) const { return next[static_cast<size_t>(i) * nodes.size() + static_cast<size_t>(j)]; }
};

namespace detail {

inline double LakeDistance(double dx, double dy) { return std::sqrt(dx * dx + dy * dy); }

// Inside a circle as PointInColObject answers it (the radius at least the distance).
inline bool LakeInside(const TpiLakeCircle& c, double x, double y) { return LakeDistance(x - c.x, y - c.y) <= c.r; }

// Two circles overlap (DoAreasIntersect: the sum of the radii above the distance).
inline bool LakeOverlap(const TpiLakeCircle& a, const TpiLakeCircle& b) {
    return static_cast<double>(a.r) + b.r > LakeDistance(a.x - b.x, a.y - b.y) + 1.0;
}

// The cells a boat's centre may be over: the lake's water, and the 3 by 3 cells of the plot around its circle's cell.
struct LakeArea {
    const TpiLake& lake;
    int32_t hx, hy;
    bool Ok(int32_t x, int32_t y) const {
        if (x < 0 || y < 0 || x >= kTpiLakeMap || y >= kTpiLakeMap) return false;
        if (x >= hx - 1 && x <= hx + 1 && y >= hy - 1 && y <= hy + 1) return true;
        return lake.Water(x, y);
    }
    // The distance from (px, py) to the nearest cell a centre may not be over, at most 4 cells.
    double Clearance(double px, double py) const {
        const double c = kTpiLakeCell;
        double best = 4 * c;
        const int32_t cx = static_cast<int32_t>(std::floor(px / c));
        const int32_t cy = static_cast<int32_t>(std::floor(py / c));
        for (int32_t y = cy - 4; y <= cy + 4; ++y) {
            for (int32_t x = cx - 4; x <= cx + 4; ++x) {
                if (Ok(x, y)) continue;
                const double nx = px < x * c ? x * c : px > (x + 1) * c ? (x + 1) * c : px;
                const double ny = py < y * c ? y * c : py > (y + 1) * c ? (y + 1) * c : py;
                const double d = LakeDistance(px - nx, py - ny);
                if (d < best) best = d;
            }
        }
        return best;
    }
};

}  // namespace detail

// The cells a boat's centre may be over, for a ride whose plot circle's centre is (headX, headY).
inline bool TpiLakeCellOk(const TpiLake& lake, int32_t headX, int32_t headY, int32_t cx, int32_t cy) {
    return detail::LakeArea{lake, headX / kTpiLakeCell, headY / kTpiLakeCell}.Ok(cx, cy);
}

// Builds a ride's chain from its plot circle `head` and rotation kind. Empty (the Mac's ride, its boats on the plot)
// when there is no water behind the plot, no path of water from it to Theme Park Inc's circles, or no buoy to sail to.
inline TpiLakeNet TpiLakeBuild(const TpiLake& lake, const TpiLakeCircle& head, int32_t rotKind) {
    TpiLakeNet net;
    int32_t dx, dy;
    if (!lake.loaded || head.r <= 0 || !TpiLakeBack(rotKind, dx, dy)) return net;
    const int32_t c = kTpiLakeCell;
    const detail::LakeArea area{lake, head.x / c, head.y / c};
    std::vector<TpiLakeCircle> nodes{head};

    // The plot's back row, then the water cell behind it.
    TpiLakeCircle bridge{head.x + dx * c, head.y + dy * c, 0};
    bridge.r = static_cast<int32_t>(std::fmin(c, area.Clearance(bridge.x, bridge.y)));
    if (bridge.r < c / 2) return net;
    nodes.push_back(bridge);
    const int32_t lx = (head.x + 2 * dx * c) / c;
    const int32_t ly = (head.y + 2 * dy * c) / c;
    if (!lake.Water(lx, ly)) return net;

    // Theme Park Inc's circles, each cut down to the water around its centre.
    std::vector<TpiLakeCircle> tpi;
    for (const TpiLakeCircle& t : lake.circles) {
        if (!lake.Water(t.x / c, t.y / c)) continue;
        TpiLakeCircle k = t;
        k.r = static_cast<int32_t>(std::fmin(t.r, area.Clearance(t.x, t.y)));
        if (k.r >= c * 3 / 10) tpi.push_back(k);
    }
    if (tpi.empty()) return net;

    // Paths of water cells, each with room for a circle of 0.7 cells, from the landing cell to each of those circles
    // that the chain does not reach yet (one search, so the paths share their cells).
    auto centreOf = [&](int32_t x, int32_t y) { return TpiLakePoint{x * c + c / 2, y * c + c / 2}; };
    auto room = [&](int32_t x, int32_t y) {
        const TpiLakePoint p = centreOf(x, y);
        return area.Clearance(p.x, p.y);
    };
    std::vector<int32_t> from(static_cast<size_t>(kTpiLakeMap * kTpiLakeMap), -2);
    std::vector<int32_t> queue{ly * kTpiLakeMap + lx};
    from[static_cast<size_t>(queue[0])] = -1;
    for (size_t q = 0; q < queue.size(); ++q) {
        const int32_t cell = queue[q];
        const int32_t x = cell % kTpiLakeMap, y = cell / kTpiLakeMap;
        const int32_t steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& s : steps) {
            const int32_t nx = x + s[0], ny = y + s[1];
            if (!lake.Water(nx, ny)) continue;
            const size_t n = static_cast<size_t>(ny * kTpiLakeMap + nx);
            if (from[n] != -2 || room(nx, ny) < c * 0.7) continue;
            from[n] = cell;
            queue.push_back(static_cast<int32_t>(n));
        }
    }
    std::vector<uint8_t> added(static_cast<size_t>(kTpiLakeMap * kTpiLakeMap), 0);
    auto reaches = [&](const std::vector<TpiLakeCircle>& all, const TpiLakeCircle& t) {
        std::vector<uint8_t> seen(all.size(), 0);
        std::vector<size_t> order{0};
        seen[0] = 1;
        for (size_t q = 0; q < order.size(); ++q) {
            if (detail::LakeOverlap(all[order[q]], t)) return true;
            for (size_t k = 0; k < all.size(); ++k)
                if (!seen[k] && detail::LakeOverlap(all[order[q]], all[k])) {
                    seen[k] = 1;
                    order.push_back(k);
                }
        }
        return false;
    };
    for (const TpiLakeCircle& t : tpi) {
        if (reaches(nodes, t)) continue;
        int32_t found = -1;
        for (const int32_t cell : queue) {
            const TpiLakePoint p = centreOf(cell % kTpiLakeMap, cell / kTpiLakeMap);
            if (detail::LakeDistance(p.x - t.x, p.y - t.y) + 1.0 < t.r) {
                found = cell;
                break;
            }
        }
        if (found < 0) continue;
        std::vector<int32_t> path;
        for (int32_t cell = found; cell >= 0 && !added[static_cast<size_t>(cell)]; cell = from[static_cast<size_t>(cell)]) path.push_back(cell);
        for (size_t i = path.size(); i-- > 0;) {
            const int32_t x = path[i] % kTpiLakeMap, y = path[i] / kTpiLakeMap;
            const TpiLakePoint p = centreOf(x, y);
            added[static_cast<size_t>(path[i])] = 1;
            nodes.push_back({p.x, p.y, static_cast<int32_t>(std::fmin(c, room(x, y)))});
        }
        if (nodes.size() > kTpiLakeMaxNodes) return net;
    }
    for (const TpiLakeCircle& t : tpi) nodes.push_back(t);
    if (nodes.size() > kTpiLakeMaxNodes) return net;

    // Only the circles the plot's reaches.
    const size_t all = nodes.size();
    std::vector<int32_t> seen(all, 0);
    std::vector<size_t> order{0};
    seen[0] = 1;
    for (size_t q = 0; q < order.size(); ++q)
        for (size_t k = 0; k < all; ++k)
            if (!seen[k] && detail::LakeOverlap(nodes[order[q]], nodes[k])) {
                seen[k] = 1;
                order.push_back(k);
            }
    for (size_t k = 0; k < all; ++k)
        if (seen[k]) net.nodes.push_back(nodes[k]);

    // The route table: a breadth-first search back from every node.
    const size_t n = net.nodes.size();
    net.next.assign(n * n, 0);
    for (size_t j = 0; j < n; ++j) {
        std::vector<int32_t> hops(n, -1);
        std::vector<size_t> wave{j};
        hops[j] = 0;
        net.next[j * n + j] = static_cast<uint8_t>(j);
        for (size_t q = 0; q < wave.size(); ++q) {
            const size_t at = wave[q];
            for (size_t k = 0; k < n; ++k) {
                if (hops[k] >= 0 || !detail::LakeOverlap(net.nodes[at], net.nodes[k])) continue;
                hops[k] = hops[at] + 1;
                net.next[k * n + j] = static_cast<uint8_t>(at);
                wave.push_back(k);
            }
        }
    }

    // Theme Park Inc's buoys that lie over water in one of the circles.
    for (const TpiLakePoint& b : lake.buoys) {
        if (!lake.Water(b.x / c, b.y / c)) continue;
        for (size_t k = 1; k < n; ++k)
            if (detail::LakeInside(net.nodes[k], b.x, b.y)) {
                net.buoys.push_back(b);
                break;
            }
    }
    if (net.buoys.empty()) net = TpiLakeNet{};
    return net;
}

// The first node holding (x, y), or -1.
inline int32_t TpiLakeNodeAt(const TpiLakeNet& net, int32_t x, int32_t y) {
    for (size_t k = 0; k < net.nodes.size(); ++k)
        if (detail::LakeInside(net.nodes[k], x, y)) return static_cast<int32_t>(k);
    return -1;
}

// Where a boat in node `at` at (px, py) steers for a target in node `to`: false and the target itself when the boat is
// already in the target's node (after following the chain through the nodes it is in), else true and the middle of the
// overlap of the boat's node and the next one on the chain.
inline bool TpiLakeAim(const TpiLakeNet& net, int32_t at, int32_t px, int32_t py, int32_t to, int32_t& ax, int32_t& ay) {
    const int32_t n = static_cast<int32_t>(net.nodes.size());
    if (at < 0 || to < 0 || at >= n || to >= n) return false;
    for (int32_t guard = 0; guard < n && at != to; ++guard) {
        const int32_t k = net.Next(at, to);
        const TpiLakeCircle& a = net.nodes[static_cast<size_t>(at)];
        const TpiLakeCircle& b = net.nodes[static_cast<size_t>(k)];
        if (detail::LakeInside(b, px, py)) {
            at = k;
            continue;
        }
        const double d = detail::LakeDistance(b.x - a.x, b.y - a.y);
        if (d < 1.0) {
            at = k;
            continue;
        }
        double lo = d - b.r, hi = a.r;
        if (lo < 0) lo = 0;
        if (hi > d) hi = d;
        const double m = (lo + hi) / 2;
        ax = static_cast<int32_t>(std::lround(a.x + (b.x - a.x) * m / d));
        ay = static_cast<int32_t>(std::lround(a.y + (b.y - a.y) * m / d));
        return true;
    }
    return false;
}

// One ride's chain in the game: the ride, its collision objects (index 0 the plot's), and the chain.
struct TpiLakeRide {
    const void* ride = nullptr;
    std::vector<void*> cols;
    TpiLakeNet net;
};
// TPW_PORT_FIX mods gTpiLakeRides | Mac: none | Port: TPW_MOD_HOOK tpi_worlds (gModTpiWorlds, include/TpiWorlds.h): the chains of the Great Lakes built (BMP_CreateRide), so that steering finds a boat's | Why: tpw-mods tpi-worlds, the Great Lake's boats sail on the lake | Off: empty, unread
inline TpiLakeRide gTpiLakeRides[4];

// The collision object flag that marks a lake chain's circle: a bit no Mac object carries (the Mac's use 0x01 to 0x100,
// and 0x80 is its own mark of a water track's crossroad centre, a circle with no bump to bump collision).
inline constexpr uint32_t kTpiLakeColFlag = 0x10000000u;
// The flags a lake chain's circle gets on top of BMP_StartColObject's: the mark, and the Mac's 0x80 (no bump to bump
// collision, BMP_Collision), which the lake's circles have had since the Great Lake first sailed.
inline constexpr uint32_t kTpiLakeColFlags = kTpiLakeColFlag | 0x80u;

// Whether a collision object with `flags` is a lake chain's circle (a crossroad centre is not, though it has 0x80 too).
inline bool TpiLakeCol(uint32_t flags) { return (flags & kTpiLakeColFlag) != 0; }

// Whether BMP_InsideArea moves a bump into a live collision object with `flags` in the mod's worlds: any object but a
// lake chain's circle, and a lake chain's circle only for a bump of that chain's ride (`ownChain`).
inline bool TpiLakeColEnterable(uint32_t flags, bool ownChain) { return !TpiLakeCol(flags) || ownChain; }

}  // namespace tpw

