// TPI Worlds: the cleared Arabian tunnel's ground above its fixed footbridge route.
#pragma once

// TPW_PORT_FIX mods TpiTunnelTop | Mac: no such surface | Port: TPW_MOD_HOOK tpi_worlds, converter metadata identifies a roof rectangle and the tunnel footprint; once its land is bought the whole top is raised and becomes ordinary ground (the route's cells under it too), and the route under it is a fixed table rebuilt from the level (gTpiTunnelCovered), walked at the level copy's floor heights on a layer of its own that never meets the top's (TpiTunnelTopLevel) | Why: The maintainer, 0.6.2: just normal ground and path building above the tunnel, the tunnel underneath the same, nobody ever builds inside it
#include <cmath>
#include <cstdio>
#include <cstring>
#include "TpiGateFlow.h"

namespace tpw {
struct TpiTunnelTop {
    int x = 0, y = 0, w = 0, h = 0; // obstacle footprint
    int tx = 0, ty = 0, tw = 0, th = 0; // roof quads
    float height = 0;
    unsigned long long hidden = 0; // the converted level's original hidden bits, roof quads row by row
    unsigned long long raised = ~0ull; // the actual roof quads within its rectangle
};

inline bool TpiParseTunnelTop(const char* name, TpiTunnelTop& out) {
    if (name == nullptr || std::strncmp(name, "tpitop ", 7) != 0) return false;
    TpiTunnelTop top;
    char extra;
    if (std::sscanf(name + 7, "%d %d %d %d %d %d %d %d %f %llx %llx %c", &top.x, &top.y, &top.w, &top.h,
                    &top.tx, &top.ty, &top.tw, &top.th, &top.height, &top.hidden, &top.raised, &extra) != 11 ||
        top.x < 0 || top.y < 0 || top.w <= 0 || top.h <= 0 || top.x > 127 || top.y > 127 ||
        top.w > 128 - top.x || top.h > 128 - top.y || top.tx < top.x || top.ty < top.y ||
        top.tw <= 0 || top.th <= 0 || top.tw > top.x + top.w - top.tx ||
        top.th > top.y + top.h - top.ty || top.tw > 64 / top.th || !std::isfinite(top.height)) return false;
    out = top;
    return true;
}

inline bool TpiOnTunnelTop(const TpiTunnelTop& top, int x, int y) {
    return x >= top.tx && y >= top.ty && x < top.tx + top.tw && y < top.ty + top.th &&
           ((top.raised >> ((y - top.ty) * top.tw + x - top.tx)) & 1u) != 0;
}

inline bool TpiTunnelTopWasHidden(const TpiTunnelTop& top, int x, int y) {
    return x >= top.tx && y >= top.ty && x < top.tx + top.tw && y < top.ty + top.th &&
           ((top.hidden >> ((y - top.ty) * top.tw + x - top.tx)) & 1u) != 0;
}

// A grid point (x, y) that is a corner of one of the top's quads.
inline bool TpiTunnelTopPoint(const TpiTunnelTop& top, int x, int y) {
    return TpiOnTunnelTop(top, x, y) || TpiOnTunnelTop(top, x - 1, y) || TpiOnTunnelTop(top, x, y - 1) ||
           TpiOnTunnelTop(top, x - 1, y - 1);
}

inline bool TpiTunnelTopHeights(const TpiTunnelTop& top, bool cleared, unsigned width, unsigned height,
                               float* live, const float* floor) {
    if (live == nullptr || floor == nullptr || unsigned(top.tx + top.tw) > width ||
        unsigned(top.ty + top.th) > height) return false;
    bool changed = false;
    for (int y = top.ty; y <= top.ty + top.th; ++y)
        for (int x = top.tx; x <= top.tx + top.tw; ++x) {
            const unsigned v = unsigned(y) * (width + 1) + unsigned(x);
            // Only the top's own corners: the rectangle's other corners may belong to land around it, which keeps
            // whatever the park made of it.
            if (!TpiTunnelTopPoint(top, x, y)) continue;
            const float wanted = cleared ? top.height : floor[v];
            if (live[v] != wanted) { live[v] = wanted; changed = true; }
        }
    return changed;
}

// The slopes the height field's lighting gives the cleared top's grid point (x, y) (CHField::CalcFullLighting, a grid
// `points` wide and `rows` high, `point` its height): a neighbour that is not one of the top's points counts at the
// point's own height. The top's edge stands over the canyon's hidden cliff quads, whose points stay at the canyon's
// height, so the edge quads were lit as a steep slope: darker wedges on the cleared top. Every other point keeps its slopes.
inline void TpiTunnelTopSlopes(const TpiTunnelTop& top, int x, int y, int points, int rows, const float* point,
                               float& slopeX, float& slopeZ) {
    if (x <= 0 || y <= 0 || x >= points - 1 || y >= rows - 1 || !TpiTunnelTopPoint(top, x, y)) return;
    const auto at = [&](int dx, int dy) { return TpiTunnelTopPoint(top, x + dx, y + dy) ? point[dy * points + dx] : point[0]; };
    slopeX = at(-1, 0) - at(1, 0);
    slopeZ = at(0, -1) - at(0, 1);
}

// Runtime only. The original level copy remains the floor source; nothing new enters saves.
inline const void* gTpiTunnelTopField = nullptr;
inline const void* gTpiTunnelFloorSource = nullptr;
inline int (*gTpiTunnelFloorMode)(int x, int y) = nullptr;
inline TpiTunnelTop gTpiTunnelTop;
inline bool gTpiTunnelTopOpen = false;
inline bool gTpiTunnelTopPrepared = false;

inline bool TpiTunnelFloorHeight(const void* field, int x, int y, int mode) {
    const auto& t = gTpiTunnelTop;
    return TpiWorldInPlay() && field == gTpiTunnelTopField && mode == 0x1e &&
           x >= t.x && y >= t.y && x < t.x + t.w && y < t.y + t.h;
}

// The tunnel's fixed route under the cleared top: the cells of the level's walkway (attribute 0x80) on the top, bit
// (y - ty) * tw + x - tx as in `raised`. Once the top is cleared these cells hold the top's own ground, where paths are
// drawn, and the route through them lives only here. Rebuilt from the level by TpiRepairTunnelTop on every load and
// tick, never saved. The route's cells beside the top stay mode 0x1e, as the level made them.
template<class Route>
unsigned long long TpiTunnelCoveredCells(const TpiTunnelTop& top, Route route) {
    unsigned long long cells = 0;
    for (int y = top.ty; y < top.ty + top.th; ++y)
        for (int x = top.tx; x < top.tx + top.tw; ++x)
            if (TpiOnTunnelTop(top, x, y) && route(x, y)) cells |= 1ull << ((y - top.ty) * top.tw + x - top.tx);
    return cells;
}
inline unsigned long long gTpiTunnelCovered = 0;

inline bool TpiTunnelOpen() { return TpiWorldInPlay() && gTpiTunnelTopOpen && gTpiTunnelTopField != nullptr; }

// A cell of the route under the cleared top (the cell `id`, 1 + x + (y << 7)).
inline bool TpiTunnelCovered(uint16_t id) {
    if (!TpiTunnelOpen() || id == 0) return false;
    const auto& t = gTpiTunnelTop;
    const int x = (id - 1) & 127, y = (id - 1) >> 7;
    return x >= t.tx && y >= t.ty && x < t.tx + t.tw && y < t.ty + t.th &&
           ((gTpiTunnelCovered >> ((y - t.ty) * t.tw + x - t.tx)) & 1u) != 0;
}

// The surface a walker on the cell `id` is on: 1 for the top, 0 for the ground and the tunnel. A covered cell is on the
// top for a walker on the top (`roof` 1) and in the tunnel for one walking the route underneath (`roof` 0).
inline int TpiTunnelTopLevel(uint16_t id, int roof = 1) {
    if (!TpiTunnelOpen() || id == 0) return 0;
    if (!TpiOnTunnelTop(gTpiTunnelTop, (id - 1) & 127, (id - 1) >> 7)) return 0;
    return !TpiTunnelCovered(id) || roof != 0 ? 1 : 0;
}

// A walker's layer on the cell `id`: on a covered cell the one it keeps, elsewhere the cell's own surface.
inline int TpiWalkerRoof(int kept, uint16_t id) {
    return TpiTunnelCovered(id) ? (kept != 0 ? 1 : 0) : TpiTunnelTopLevel(id);
}

// The layer after a step from `from` to `to`: a walker keeps its layer along the covered cells and takes the surface of
// the cell it came from when it steps onto them, so only a walker coming off the top walks on the top over the route,
// and one coming in at a mouth walks the route underneath.
inline int TpiStepRoof(int kept, uint16_t from, uint16_t to) {
    if (!TpiTunnelCovered(to)) return TpiTunnelTopLevel(to);
    return TpiTunnelCovered(from) ? (kept != 0 ? 1 : 0) : TpiTunnelTopLevel(from);
}

// How far a peep drawn at world (x, z) (ten units a cell) is moved from `liveHeight(x, z)`, the height the sprite code
// stands it on: down to `floorHeight(x, z)`, the tunnel floor, while it walks the route under a covered cell (`kept` 0).
// Zero everywhere else, the top's walkers included.
template<class Live, class Floor>
float TpiTunnelLift(int kept, float x, float z, Live liveHeight, Floor floorHeight) {
    if (kept != 0 || !TpiTunnelOpen() || !(x >= 0.0f) || !(z >= 0.0f)) return 0.0f;
    const int cx = int(x / 10.0f), cy = int(z / 10.0f);
    if (cx > 127 || cy > 127 || !TpiTunnelCovered(uint16_t(1 + cx + (cy << 7)))) return 0.0f;
    const float lift = floorHeight(x, z) - liveHeight(x, z);
    return lift < 0.0f ? lift : 0.0f;
}

// The heights a sprite's shadow lies on (SSpriteGameData::CreateShadow reads them under its corners). The cleared top gives
// the route's cells under and beside it the top's height in the live field, so a sprite standing below the top over one of
// them (a guest walking the tunnel; one on the top stands at the top's height) lays its shadow on the level's own heights,
// the floor's.
template<class Field>
const float* TpiShadowHeights(const Field* live, float x, float y, float z) {
    const float* heights = live->mHeights;
    if (gTpiTunnelFloorSource == nullptr || gTpiTunnelFloorMode == nullptr || live != gTpiTunnelTopField) return heights;
    if (!(y < gTpiTunnelTop.height - 1.0f) || !(x >= live->mF08[0]) || !(z >= live->mF08[1]) ||
        !(live->mF08[2] > 0.0f) || !(live->mF08[3] > 0.0f)) return heights;
    const int cx = int((x - live->mF08[0]) / live->mF08[2]), cy = int((z - live->mF08[1]) / live->mF08[3]);
    if (cx > 127 || cy > 127) return heights;
    if (!TpiTunnelFloorHeight(live, cx, cy, gTpiTunnelFloorMode(cx, cy)) && !TpiTunnelCovered(uint16_t(1 + cx + (cy << 7))))
        return heights;
    return static_cast<const Field*>(gTpiTunnelFloorSource)->mHeights;
}

// The bit a walker's saved nav mode carries while it is on the top over a covered cell (the original's modes are 0 to 2).
constexpr int32_t kTpiRoofNavBit = 0x100;
// The bit a walker's callers (Vehicle's plan, steps and wall avoidance) add to the mode MAP_wall_in_way gets while the
// walker is not on the top (on the ground, or on the route under it), never saved: without it, as for every other caller,
// a covered cell is the top's.
constexpr int32_t kTpiTunnelNavBit = 0x200;

// First person's walk (HandleCamera, GetNextGridsquare) in the same two layers: the layer it keeps on the covered cells,
// set to the top's whenever first person starts, so one who walks in at a mouth goes through the tunnel and one who
// walks onto the top stays on it. Runtime only.
inline int gTpiFirstPersonRoof = 1;
inline uint16_t TpiCellAt(float x, float z) {
    if (!(x >= 0.0f && z >= 0.0f && x < 1280.0f && z < 1280.0f)) return 0;
    return static_cast<uint16_t>(1 + static_cast<int>(x / 10.0f) + (static_cast<int>(z / 10.0f) << 7));
}
// The mode MAP_wall_in_way checks a first-person step from the cell `id` in (the Mac's 2).
inline int32_t TpiFirstPersonNavMode(uint16_t id) {
    return TpiTunnelOpen() && TpiWalkerRoof(gTpiFirstPersonRoof, id) == 0 ? 2 | kTpiTunnelNavBit : 2;
}
inline void TpiFirstPersonStep(float fromX, float fromZ, float toX, float toZ) {
    const uint16_t from = TpiCellAt(fromX, fromZ), to = TpiCellAt(toX, toZ);
    if (TpiTunnelOpen() && from != to) gTpiFirstPersonRoof = TpiStepRoof(gTpiFirstPersonRoof, from, to);
}
// First person stands on the tunnel floor at (x, z), under the top.
inline bool TpiFirstPersonUnderTop(float x, float z) {
    return TpiTunnelCovered(TpiCellAt(x, z)) && gTpiFirstPersonRoof == 0;
}
} // namespace tpw
// Reapply a loaded park's cleared roof state; called before rendering and exercised by the host engine test.
void TpiRepairTunnelTop();
void TpiPrepareTunnelTop();
// DetermineHFieldHeightQuick on the level copy, the tunnel floor's own heights (src/game/park/TPHField.cpp).
float TpiTunnelFloorHeightQuick(float x, float z);
