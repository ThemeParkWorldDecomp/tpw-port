// TpiGateBuild.h: the ground beside the park gate in the TPI Worlds and Rides mod's worlds (TPW_MOD_HOOK tpi_worlds).
// Theme Park World's starting hoarding runs from the two gate posts (gHoardingLeft, gHoardingRight on row gHoardingBottom)
// up both sides of the entrance road, and its posts, two cells apart, each hold a 2 x 2 block of cells that nothing can be
// built on. In the mod's worlds that fence is hidden (LLCCM_CreateHoardingMesh) but still there, and Theme Park Inc has no
// fence at all: its land is its own hoardings.map, which in all three worlds gives a new park, beside the gate, the two
// entrance rows from left - 2 to right + 3 and, on the four rows before them, the three columns left - 2 to left and
// right + 1 to right + 3 (the road between stays outside). Theme Park World's fence owns only some of those cells and
// blocks the rest, so the strips right beside the gate and along the road could not be built on. Header only and pure,
// apart from the theme flag, so the host test sees what the game does.
#pragma once


// TPW_PORT_FIX mods TpiGateBuild | Mac: no such header | Port: the gate ground of the mod's worlds (TPW_MOD_HOOK tpi_worlds): which cells beside the gate a park owns and may build on over the hidden hoarding, and the hoarding kept under a demolished building there | Why: tpw-mods tpi-worlds; Theme Park Inc lets a park build right beside its gate and along the road up to it (Piro, Discord, 0.6.0)

#include <cstdint>

#include "TpiGateFlow.h"

namespace tpw {

// Whether map cell (x, y) is gate ground: owned by every park of the mod's worlds, as Theme Park Inc's hoardings.map has
// it, for gate posts at (left, bottom) and (right, bottom).
constexpr bool TpiGateGround(int32_t x, int32_t y, int32_t left, int32_t right, int32_t bottom) {
    if (y == bottom || y == bottom + 1)
        return x >= left - 2 && x <= right + 3;
    if (y >= bottom - 4 && y < bottom)
        return (x >= left - 2 && x <= left) || (x >= right + 1 && x <= right + 3);
    return false;
}

// Whether a ground placement may stand over the hidden hoarding at map cell (x, y): in one of the mod's worlds, a path
// (mode 1, or 0x18, which CMapCell::ConnectionType counts as 1), a queue (3) or a building's cells (4, 9, 10) over a hoarding
// post or block (`trackType`, the post's type through a block's parent, 0x19) on gate ground. A cell the level's map marks
// as the gate's own (attribute bit 1, the gate posts' cells in Theme Park World's levels too) stays blocked.
inline bool TpiBuildOverGateHoarding(int32_t trackType, int32_t mode, uint8_t attribute, int32_t x, int32_t y, int32_t left,
                                     int32_t right, int32_t bottom) {
    const bool ground = mode == 1 || mode == 3 || mode == 4 || mode == 9 || mode == 10;
    return TpiWorldInPlay() && trackType == 0x19 && ground && (attribute & 1) == 0 && TpiGateGround(x, y, left, right, bottom);
}

// Whether demolishing a ground object keeps the track cell under it: in one of the mod's worlds, a hoarding post, or one of
// its blocks (0xc or 0x11, through its parent). The fence stays, as the next land purchase walks its links.
template <class Track>
inline bool TpiKeepGroundHoarding(const Track& track, const Track* grid) {
    if (!TpiWorldInPlay())
        return false;
    const Track* post = &track;
    if (track.mType == 0xc || track.mType == 0x11) {
        if (track.mParentID == 0)
            return false;
        post = grid + track.mParentID - 1;
    }
    return post->mType == 0x19;
}

// In one of the mod's worlds, gives the park every gate ground cell it does not own yet: clears map flag 0x40 (the cell's
// tile does not depend on it, and RecalculateParkDarkening, which does, runs after each caller) and changes nothing else.
// Answers the number of cells given; 0 outside the mod's worlds, before the hoarding's extents are known (-1), and when
// repeated. Called when a new park's hoarding is drawn (LLCC_DrawInitialHoarding), after a land purchase revises it
// (LLCC_ReviseHoarding) and when a park is loaded (CSaveLoad::PostLoadOperations, for parks saved before this fix).
template <class Cell>
inline int32_t TpiOwnGateGround(Cell* cells, int32_t left, int32_t right, int32_t bottom) {
    if (!TpiWorldInPlay() || left < 0 || bottom < 0)
        return 0;
    int32_t given = 0;
    for (int32_t y = bottom - 4; y <= bottom + 1; ++y)
        for (int32_t x = left - 2; x <= right + 3; ++x) {
            if (x < 0 || x > 0x7f || y < 0 || y > 0x7f || !TpiGateGround(x, y, left, right, bottom))
                continue;
            Cell& cell = cells[x + (y << 7)];
            if ((cell.mFlags & 0x40) != 0) {   // CMapCell::IsFlag and UnSetFlag, on the field itself
                cell.mFlags = static_cast<uint16_t>(cell.mFlags & ~0x40u);
                ++given;
            }
        }
    return given;
}

}  // namespace tpw

