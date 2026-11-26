// TpiFenceBuild.h: the cells of the TPI Worlds and Rides mod's worlds that the level's own fences, walls and banks stand on
// (TPW_MOD_HOOK tpi_worlds). Theme Park Inc models some of its park's edges into the terrain (Polar Zone's wooden fence
// down the east side, Land of Invention's wooden fence along the south) and marks the two cells either side of the fence
// line, and the edges of its canyons and banks, with Base.map attribute bit 1, the bit Theme Park World gives its own
// unbuildable features (Wonderland's trench, the map's rim). Theme Park World never reads the bit for building: its
// features stand on hidden terrain or land no park owns. The converted fences stand on plain terrain, so once Buy Land,
// which takes land past Theme Park Inc's EndHoardings fence, owns both sides, a ride, shop or path could stand right
// across a fence. In the mod's worlds a cell with the bit refuses every placement that builds; demolishing, the land
// tools and the hoarding keep the Mac's answer. Header only and pure, apart from the theme flag, so the host test sees
// what the game does.
#pragma once


// TPW_PORT_FIX mods TpiFenceBuild | Mac: no such header | Port: the fence cells of the mod's worlds (TPW_MOD_HOOK tpi_worlds): which placements a cell with Base.map attribute bit 1 refuses | Why: tpw-mods tpi-worlds; a ride or path could be built right across Polar Zone's wooden fence (the maintainer, 0.6.1)

#include <cstdint>

#include "TpiGateFlow.h"

namespace tpw {

// Whether `mode` builds on the ground: a path (1, and 0x18, which CMapCell::ConnectionType counts as 1), a queue (3), a
// building's cells (4, 9, 10), track (0xb, 0x10, 0xe, 0x13, 0x1a) or a ride's pylons and moves (0x15, 0x16).
constexpr bool TpiBuildMode(int32_t mode) {
    switch (mode) {
    case 1: case 3: case 4: case 9: case 10: case 0xb: case 0xe: case 0x10: case 0x13: case 0x15: case 0x16: case 0x18:
    case 0x1a:
        return true;
    default:
        return false;
    }
}

// Whether a cell whose Base.map attribute is `attribute` refuses `mode`: in one of the mod's worlds, a building mode on a
// cell with bit 1 that is not also walkable (bit 0x80: the pavement and the crossing outside the gate, unowned land).
inline bool TpiFenceRefuses(int32_t mode, uint8_t attribute) {
    return TpiWorldInPlay() && (attribute & 1) != 0 && (attribute & 0x80) == 0 && TpiBuildMode(mode);
}

}  // namespace tpw

