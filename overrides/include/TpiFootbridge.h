// TpiFootbridge.h: the TPI Worlds and Rides mod's path joins and wandering across its footbridges and tunnel floors
// (TPW_MOD_HOOK tpi_worlds). These links are calculated when used, never stored in a park. The floor stays mode 0x1e,
// where nothing is built; the original's targeted route search already walks onto it and back onto a path.
#pragma once

// TPW_PORT_FIX mods TpiFootbridge | Mac: paths draw and random walks follow only their stored links, which never join a footbridge | Port: TPW_MOD_HOOK tpi_worlds, path tiles join a neighbouring footbridge, and guests as well as staff wander onto, along and off it in the mod's worlds | Why: the tunnel floor stays a footbridge, with paths joining both mouths and visitors walking through; no saved cell or converter change | Off: the Mac's path tiles and walks
#include <cstdint>
#include "TpiGateFlow.h"

namespace tpw {

struct TpiFootbridgeCell {
    int32_t mode;
    uint16_t flags;
};

// Flag 0x80 marks the pavement outside the gate, also mode 0x1e, rather than a footbridge.
inline bool TpiIsFootbridge(TpiFootbridgeCell cell) { return cell.mode == 0x1e && (cell.flags & 0x80u) == 0; }

inline bool TpiFootbridgeStep(TpiFootbridgeCell here, TpiFootbridgeCell next) {
    if (!TpiWorldInPlay()) return false;
    const bool bridge = TpiIsFootbridge(here);
    return (bridge || here.mode == 1) && (TpiIsFootbridge(next) || (bridge && next.mode == 1));
}

// Only the four straight sides join. A missing neighbour has mode -1. The getter takes a compass bit.
template<class GetCell>
uint8_t TpiFootbridgeLinks(TpiFootbridgeCell here, GetCell getCell) {
    if (!TpiWorldInPlay() || (here.mode != 1 && !TpiIsFootbridge(here))) return 0;
    uint8_t links = 0;
    constexpr uint8_t directions[] = {1, 4, 0x10, 0x40};
    for (uint8_t direction : directions)
        if (TpiFootbridgeStep(here, getCell(direction))) links |= direction;
    return links;
}

inline bool TpiFootbridgeWander(bool staff, bool staffBridgeMod) {
    return TpiWorldInPlay() || (staff && staffBridgeMod);
}

}  // namespace tpw
