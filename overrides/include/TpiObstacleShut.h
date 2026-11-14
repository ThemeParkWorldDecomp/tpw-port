// TpiObstacleShut.h: the TPI Worlds and Rides mod's obstacles that stay until their land is bought (TPW_MOD_HOOK
// tpi_worlds). Theme Park World has no obstacles, so the converter (assets/tpi_terrain.h) makes Theme Park Inc's part of
// the terrain. A cleared tunnel (Arabian Nights', Polar Zone's) is drawn open, its floor footbridge cells, with what
// closed it before it was cleared (its rubble, its wreckage) drawn too, each such node named "tpishut X Y W H": the
// tunnel's footprint (its shape's first map cell and its size in cells). An obstacle that clears once its land is bought
// (Arabian Nights' rock and cliff, Polar Zone's swamp) is drawn as it was before clearing, its cells closed, each of its
// nodes named "tpishut X Y W H M" ("tpishut X Y W H" when it closed none, the swamp):
// its footprint and the cells it closed (M, hex digits, four cells to a digit with the first the lowest bit, row by row
// from (X, Y)). In the mod's worlds the game (src/game/TPEngine.cpp EngineTick2) shows those nodes while the park owns
// none of the land the footprint and the cells next to it hold that a new park does not start with, the Invention gate's
// rule (include/TpiObstacleGate.h TpiGateOpen), and folds them away once it owns some; the closed cells then
// become land, their quads shown again, as the level's own (gHeightfieldCopy). So the obstacle's state is the park's
// land, and a saved park needs nothing more. (A tunnel's ends are walked whatever it shows: no path is laid on land the
// park does not own.)
// Header only and pure, so the host test sees what the game does.
#pragma once


// TPW_PORT_FIX mods TpiObstacleShut | Mac: no such header; Theme Park World has no obstacles | Port: the footprint and closed cells of the obstacles that stay until their land is bought, which EngineTick2 shows, folds away and opens in the mod's worlds (TPW_MOD_HOOK tpi_worlds) | Why: tpw-mods tpi-worlds; Theme Park Inc's tunnels, rock, cliff and swamp were cleared by missions Theme Park World has no part of (golden tickets, skilled staff), and the maintainer wants them closed until their land is bought (0.5.4)

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "TpiObstacleGate.h"

namespace tpw {

// The start of such a node's name.
inline constexpr char kTpiShutPrefix[] = "tpishut ";

// The most cells a name's mask holds: a name has at most 63 characters.
inline constexpr int32_t kTpiShutMaxCells = 4 * 63;

// An obstacle as its node's name gives it.
struct TpiShut {
    int32_t x = 0, y = 0, w = 0, h = 0;   // the footprint: its first map cell and its size in cells
    bool masked = false;                  // the name holds the cells it closed (an obstacle that clears)
    bool hid[kTpiShutMaxCells] = {};      // those cells, row by row from (x, y)
};

// Reads a node's name. False for any other name, a footprint outside the 128 by 128 map, a number missing, a mask of
// the wrong length or anything after it.
inline bool TpiParseShut(const char* name, TpiShut& shut) {
    const size_t prefix = sizeof(kTpiShutPrefix) - 1;
    if (name == nullptr || std::strncmp(name, kTpiShutPrefix, prefix) != 0)
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
    TpiShut out;
    out.x = box[0];
    out.y = box[1];
    out.w = box[2];
    out.h = box[3];
    while (*at == ' ') ++at;
    if (*at != '\0') {
        const int32_t cells = out.w * out.h, digits = (cells + 3) / 4;
        if (cells > kTpiShutMaxCells)
            return false;
        for (int32_t d = 0; d < digits; ++d) {
            const char c = at[d];
            const int32_t v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (v < 0)
                return false;
            for (int32_t k = 0; k < 4 && 4 * d + k < cells; ++k) out.hid[4 * d + k] = (v >> k & 1) != 0;
        }
        at += digits;
        while (*at == ' ') ++at;
        if (*at != '\0')
            return false;
        out.masked = true;
    }
    shut = out;
    return true;
}

// Whether the obstacle closed map cell (x, y) (only an obstacle that clears names any).
inline bool TpiShutHides(const TpiShut& shut, int32_t x, int32_t y) {
    if (!shut.masked || x < shut.x || y < shut.y || x >= shut.x + shut.w || y >= shut.y + shut.h)
        return false;
    return shut.hid[(y - shut.y) * shut.w + (x - shut.x)];
}

// Whether the obstacle is gone: the Invention gate's rule over its footprint (TpiGateOpen: the park owns a cell of it or
// next to it that a new park does not start with).
template <class Owned, class Start>
inline bool TpiShutCleared(const TpiShut& shut, Owned owned, Start start) {
    TpiGateLeaf leaf;
    leaf.x = shut.x;
    leaf.y = shut.y;
    leaf.w = shut.w;
    leaf.h = shut.h;
    return TpiGateOpen(leaf, owned, start);
}

// How small a node is folded once the obstacle is gone: its 3 by 3 part times this, its place kept, so nothing of it
// shows and nothing about it moves.
inline constexpr float kTpiShutFold = 1.0e-4f;

// A node's 3 by 3 part (the rows of its matrix): its own (`own`) while the obstacle stands, folded once it is gone.
inline void TpiShutRows(const float own[3][3], bool cleared, float rows[3][3]) {
    for (int32_t r = 0; r < 3; ++r)
        for (int32_t c = 0; c < 3; ++c) rows[r][c] = cleared ? own[r][c] * kTpiShutFold : own[r][c];
}

// What a closed cell's quad in the level's copy (gHeightfieldCopy, which SetHeightfieldTexture's tile 0x37 and a loaded
// park's repair, RepairTpiHiddenCells, read) needs: shown (1) once the obstacle is gone and still hidden, hidden (-1)
// while it stands and shown (another park of the level, loaded, had cleared it), else nothing (0).
inline int32_t TpiShutQuadChange(bool cleared, bool copyHidden) {
    if (cleared && copyHidden) return 1;
    if (!cleared && !copyHidden) return -1;
    return 0;
}

// A closed cell that goes back to its hidden quad's mode (CMapCell::Reset, 7) when the copy's quad is hidden again: plain
// land (mode 0) that belongs to nothing and is no path edge (flag 0x20). Anything built on it is kept.
inline bool TpiShutCellToClose(int32_t mode, uint16_t parent, uint16_t flags) {
    return mode == 0 && parent == 0 && (flags & 0x20u) == 0;
}

}  // namespace tpw

