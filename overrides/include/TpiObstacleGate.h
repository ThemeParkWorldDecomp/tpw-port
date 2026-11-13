// TpiObstacleGate.h: the TPI Worlds and Rides mod's opening gate (TPW_MOD_HOOK tpi_worlds): Land of Invention's gate,
// a Theme Park Inc obstacle that opened by itself once the land up to it was bought (its .sam's
// Obstacle.OpenedAutomaticly 1). Theme Park World has no obstacles, so the converter (assets/tpi_terrain.h) makes it part
// of the terrain, drawn closed, with its posts' cells closed and the passage between them ground, and names each of its
// leaves "tpigate X Y W H cx cy cz cw ox oy oz ow": the gate's footprint (its shape's first map cell and its size in
// cells) and the leaf's closed and open orientations (quaternions, x y z w, as its first animation's first and last
// rotation keys hold them). In the mod's worlds the game swings each leaf (src/game/TPEngine.cpp EngineTick1) open
// while the park owns any cell of the footprint or any cell next to it (sides and corners) that a new park does not
// start with, and closed while it owns none, so the gate's state is the park's land and a saved park needs nothing
// more. (Land of Invention's starting hoarding runs through the middle of its gate, so a new park owns half of it.)
// Header only and pure, so the host test sees what the game does.
#pragma once


// TPW_PORT_FIX mods TpiObstacleGate | Mac: no such header; Theme Park World has no obstacles | Port: the opening gate's footprint, leaves and swing that EngineTick1 uses in the mod's worlds (TPW_MOD_HOOK tpi_worlds) | Why: tpw-mods tpi-worlds; Theme Park Inc's Land of Invention gate opened by itself once the land up to it was bought

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace tpw {

// The start of a leaf's node name.
inline constexpr char kTpiGateLeafPrefix[] = "tpigate ";

// A leaf as its name gives it.
struct TpiGateLeaf {
    int32_t x = 0, y = 0, w = 0, h = 0;   // the footprint: its first map cell and its size in cells
    float closed[4] = {0, 0, 0, 1};       // the closed orientation (x y z w)
    float open[4] = {0, 0, 0, 1};         // the open orientation
};

// Reads a leaf's node name. False for any other name, a footprint outside the 128 by 128 map, or a number missing.
inline bool TpiParseGateLeaf(const char* name, TpiGateLeaf& leaf) {
    const size_t prefix = sizeof(kTpiGateLeafPrefix) - 1;
    if (name == nullptr || std::strncmp(name, kTpiGateLeafPrefix, prefix) != 0)
        return false;
    const char* at = name + prefix;
    int32_t box[4];
    for (int32_t& v : box) {
        char* end = nullptr;
        const long n = std::strtol(at, &end, 10);
        if (end == at || n < 0 || n > 128)
            return false;
        v = static_cast<int32_t>(n);
        at = end;
    }
    if (box[2] == 0 || box[3] == 0 || box[0] + box[2] > 128 || box[1] + box[3] > 128)
        return false;
    float q[8];
    for (float& v : q) {
        char* end = nullptr;
        v = std::strtof(at, &end);
        if (end == at)
            return false;
        at = end;
    }
    while (*at == ' ') ++at;
    if (*at != '\0')
        return false;
    leaf.x = box[0];
    leaf.y = box[1];
    leaf.w = box[2];
    leaf.h = box[3];
    for (int k = 0; k < 4; ++k) {
        leaf.closed[k] = q[k];
        leaf.open[k] = q[4 + k];
    }
    return true;
}

// The land a new park starts with, as LLCC_DrawInitialHoarding (src/game/park/LLCellSystem.cpp) leaves it: the
// hoarding clicked through the world's Hoardings.sam offsets from (right, bottom) and the land inside it. A click at
// cell (cx, cy) puts the hoarding along that cell's far edges, so the land inside is the polygon through the points
// (cx + 1, cy + 1), closed from the last click back to the first; the fill (LLCC_FillInsideOfPark, two by two) also
// takes each cell outside it that has a cell inside beside it on both axes (the steps of a staircase of clicks), and
// the two rows from (left + 2, bottom) to (right - 1, bottom + 1) at the entrance. Checked against the game's own land
// on a new park, cell for cell (notes/tpi-worlds.md). The entrance road, which the level's map gives the park and not
// the hoarding, is not in it.
struct TpiStartFence {
    int32_t left = 0, right = 0, bottom = 0;   // gHoardingLeft, gHoardingRight, gHoardingBottom as the game sets them
    int32_t count = 0;                         // the clicks held
    int32_t x[64] = {}, y[64] = {};            // the clicks, map cells
};

// Adds a click at map cell (cx, cy), as LLCC_DrawHoardingsFromBalanceFile clicks one; past 64 (the game's limit) none.
inline void TpiFenceClick(TpiStartFence& fence, int32_t cx, int32_t cy) {
    if (fence.count >= 64)
        return;
    fence.x[fence.count] = cx;
    fence.y[fence.count] = cy;
    ++fence.count;
}

// Whether map cell (x, y)'s middle is inside the polygon of the clicks (even-odd, in half cells, so no middle ever
// lies on an edge).
inline bool TpiInsideFence(const TpiStartFence& fence, int32_t x, int32_t y) {
    if (x < 0 || y < 0 || x > 127 || y > 127)
        return false;
    const int64_t px = 2 * x + 1, py = 2 * y + 1;
    bool inside = false;
    for (int32_t i = 0, j = fence.count - 1; i < fence.count; j = i++) {
        const int64_t xi = 2 * fence.x[i] + 2, yi = 2 * fence.y[i] + 2, xj = 2 * fence.x[j] + 2, yj = 2 * fence.y[j] + 2;
        if ((yi > py) != (yj > py) && (px - xi) * (yj - yi) * (yj > yi ? 1 : -1) < (py - yi) * (xj - xi) * (yj > yi ? 1 : -1))
            inside = !inside;
    }
    return inside;
}

// Whether a new park owns map cell (x, y) (above).
inline bool TpiStartLand(const TpiStartFence& fence, int32_t x, int32_t y) {
    if (TpiInsideFence(fence, x, y))
        return true;
    if ((y == fence.bottom || y == fence.bottom + 1) && x >= fence.left + 2 && x < fence.right)
        return true;
    return (TpiInsideFence(fence, x - 1, y) || TpiInsideFence(fence, x + 1, y)) &&
           (TpiInsideFence(fence, x, y - 1) || TpiInsideFence(fence, x, y + 1));
}

// Whether the gate is open: the park owns a cell of the footprint or a cell next to it, a side or a corner away (the
// footprint grown by one cell each way, within the map), that a new park does not start with. Should a new park start
// with all of those cells, any of them counts. `owned(x, y)` answers for one map cell (in the game: its flag 0x40,
// outside the park, is clear) and `start(x, y)` whether a new park starts with it (TpiStartLand).
template <class Owned, class Start>
inline bool TpiGateOpen(const TpiGateLeaf& leaf, Owned owned, Start start) {
    const int32_t x0 = leaf.x > 0 ? leaf.x - 1 : 0, y0 = leaf.y > 0 ? leaf.y - 1 : 0;
    const int32_t x1 = leaf.x + leaf.w < 128 ? leaf.x + leaf.w : 127, y1 = leaf.y + leaf.h < 128 ? leaf.y + leaf.h : 127;
    bool outside = false, startOwned = false;
    for (int32_t y = y0; y <= y1; ++y)
        for (int32_t x = x0; x <= x1; ++x) {
            if (!start(x, y)) {
                outside = true;
                if (owned(x, y))
                    return true;
            } else if (owned(x, y)) {
                startOwned = true;
            }
        }
    return !outside && startOwned;
}

// The swing: the way open (0 closed, 1 open) moves this far each tick of the engine (EngineTick1, one frame of the
// height field's animation), so a gate swings in 40 ticks as Theme Park Inc's took about 40 frames to swing and settle.
inline constexpr float kTpiGateSwingStep = 1.0f / 40.0f;

// The way open after one more tick towards open (or closed).
inline float TpiGateStep(float way, bool open) {
    const float next = open ? way + kTpiGateSwingStep : way - kTpiGateSwingStep;
    return next < 0.0f ? 0.0f : next > 1.0f ? 1.0f : next;
}

// The share of the turn at that way open: slow at both ends.
inline float TpiGateEase(float way) { return way * way * (3.0f - 2.0f * way); }

// The leaf's orientation at that way open (x y z w): its closed and open orientations blended along the shorter arc
// at the eased share of the turn, as a unit quaternion.
inline void TpiGateTurn(const TpiGateLeaf& leaf, float way, float out[4]) {
    const float t = TpiGateEase(way < 0.0f ? 0.0f : way > 1.0f ? 1.0f : way);
    float dot = 0.0f;
    for (int k = 0; k < 4; ++k) dot += leaf.closed[k] * leaf.open[k];
    const float sign = dot < 0.0f ? -1.0f : 1.0f;
    dot *= sign;
    float a = 1.0f - t, b = t * sign;
    if (dot < 0.9995f) {
        const double angle = std::acos(static_cast<double>(dot));
        const double s = std::sin(angle);
        a = static_cast<float>(std::sin((1.0 - t) * angle) / s);
        b = static_cast<float>(std::sin(t * angle) / s) * sign;
    }
    float length = 0.0f;
    for (int k = 0; k < 4; ++k) {
        out[k] = a * leaf.closed[k] + b * leaf.open[k];
        length += out[k] * out[k];
    }
    length = length > 0.0f ? 1.0f / std::sqrt(length) : 0.0f;
    for (int k = 0; k < 4; ++k) out[k] *= length;
}

// The three axes (the rows of a node matrix's 3 by 3 part) of a unit quaternion (x y z w), as the engine's own node
// animation makes them (src/game/rides/RideAnim.cpp RAQuaternionToLAxis, then SetLMatrixOrientation).
inline void TpiGateRows(const float q[4], float rows[3][3]) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    rows[0][0] = 1.0f - 2.0f * (y * y + z * z);
    rows[0][1] = 2.0f * (x * y + w * z);
    rows[0][2] = 2.0f * (x * z - w * y);
    rows[1][0] = 2.0f * (x * y - w * z);
    rows[1][1] = 1.0f - 2.0f * (x * x + z * z);
    rows[1][2] = 2.0f * (y * z + w * x);
    rows[2][0] = 2.0f * (x * z + w * y);
    rows[2][1] = 2.0f * (y * z - w * x);
    rows[2][2] = 1.0f - 2.0f * (x * x + y * y);
}

}  // namespace tpw

