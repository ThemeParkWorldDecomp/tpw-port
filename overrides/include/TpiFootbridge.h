// TpiFootbridge.h: the TPI Worlds and Rides mod's path joins and wandering across its footbridges and tunnel floors
// (TPW_MOD_HOOK tpi_worlds). These links are calculated when used, never stored in a park. The floor stays mode 0x1e,
// where nothing is built; the original's targeted route search already walks onto it and back onto a path.
#pragma once

// TPW_PORT_FIX mods TpiFootbridge | Mac: paths draw and random walks follow only their stored links, which never join a footbridge | Port: TPW_MOD_HOOK tpi_worlds, path tiles join a neighbouring footbridge, guests as well as staff wander onto, along and off it, and a footbridge's sides are walls except toward a path or another mode 0x1e cell (TpiFootbridgeWall) in the mod's worlds | Why: the tunnel floor stays a footbridge, with paths joining both mouths and visitors walking through; no saved cell or converter change | Off: the Mac's path tiles and walks
#include <cstdint>
#include "TpiGateFlow.h"
#include "TpiTunnelTop.h"

namespace tpw {

struct TpiFootbridgeCell {
    int32_t mode;
    uint16_t flags;
    int level = 0;
};

// Flag 0x80 marks the pavement outside the gate, also mode 0x1e, rather than a footbridge.
inline bool TpiIsFootbridge(TpiFootbridgeCell cell) { return cell.mode == 0x1e && (cell.flags & 0x80u) == 0; }

// The cell `id` (mode, flags) as a walker on the layer `roof` sees it (include/TpiTunnelTop.h): a covered cell of the
// Arabian tunnel is the route's footbridge, at the floor's level, to a walker in the tunnel (`roof` 0), whatever the top
// holds there; every other cell is itself, on its own surface's level.
inline TpiFootbridgeCell TpiWalkerCell(int32_t mode, uint16_t flags, uint16_t id, int roof) {
    if (roof == 0 && TpiTunnelCovered(id)) return {0x1e, static_cast<uint16_t>(flags & ~0x80u), 0};
    return {mode, flags, TpiTunnelTopLevel(id, roof)};
}

inline bool TpiFootbridgeStep(TpiFootbridgeCell here, TpiFootbridgeCell next) {
    if (!TpiWorldInPlay()) return false;
    if (here.level != next.level) return false;
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

// Whether MAP_wall_in_way puts a wall between `here` and `next` for a walker in nav mode `navMode`: in the mod's
// worlds a footbridge's sides are walls, so a walker on one steps only onto a path or another mode 0x1e cell. The Mac
// lets any walker off a non-path cell onto any open neighbour; under a TPI tunnel that is the open land beside the
// floor, so guests left the two-cell strip at the mouths and along the hill's foot. First person (nav mode 2) keeps the
// Mac's rule.
inline bool TpiFootbridgeWall(TpiFootbridgeCell here, TpiFootbridgeCell next, int32_t navMode) {
    if (!TpiWorldInPlay()) return false;
    // A path beside the Arabian tunnel floor is on its roof, twenty units higher. Never join the two surfaces.
    if (here.level != next.level && (TpiIsFootbridge(here) || TpiIsFootbridge(next))) return true;
    if (navMode == 2 || !TpiIsFootbridge(here)) return false;
    return next.mode != 1 && next.mode != 0x1e;
}

inline bool TpiFootbridgeWander(bool staff, bool staffBridgeMod) {
    return TpiWorldInPlay() || (staff && staffBridgeMod);
}

}  // namespace tpw
