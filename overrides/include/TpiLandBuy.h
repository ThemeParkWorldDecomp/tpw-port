// TpiLandBuy.h: the TPI Worlds and Rides mod's land buying around the park gate (TPW_MOD_HOOK tpi_worlds,
// LLCC_IsHoardingEditLegal and LLCC_FillInsideOfPark in src/game/park/LLCellSystem.cpp).
// The park's hoarding is an open fence from one gate post, (gHoardingRight, gHoardingBottom), round the park to the other,
// (gHoardingLeft, gHoardingTop); the gate closes it. A Buy Land rectangle that takes in the gate makes the new fence a
// small loop round the gate alone, and the fill of the park's inside then scans out of the level: the Mac reads through
// the missing cells, the port crashes on them. The original never gets there: the land outside each of Theme Park World's
// gates is freeform, and two freeform cells in a row along a side make an edit illegal. In the mod's worlds a freeform cell
// does not count (so the land past Theme Park Inc's bridges and obstacles can be bought), which let such a rectangle
// through. So in the mod's worlds an edit that meets the gate (either post, on its edge or inside it, or the opening
// between them) is refused, and the fill's scans stop before a cell whose square of four leaves the level, as a guard.
// Header only and pure, so the host test sees what the game does.
#pragma once


// The same guards apply independently while land_rough_edges or land_unbought is on: those hooks can remove the
// original worlds' freeform protection too. With all those hooks off and outside a TPI world, the Mac's checks remain.
// TPW_PORT_FIX mods TpiLandBuy | Mac: no such header (TpiEditMeetsGate, TpiFillCellLeavesLevel); an edit is refused when a side passes exactly through a gate post, and the fill's scans have no bound of their own | Port: in the mod's worlds (TPW_MOD_HOOK tpi_worlds, LLCC_IsHoardingEditLegal and the fill's scans) an edit that meets the gate is refused and a scan stops at the level's edge | Why: tpw-mods tpi-worlds; a Buy Land rectangle round the gate crashed the game

#include <cstdint>

namespace tpw {

// The level's side in cells (CMap::IsXYValid).
inline constexpr int32_t kTpiLevelSide = 0x80;

// Whether the rectangle from (lox, loy) to (hix, hiy) (lox <= hix, loy <= hiy, its edges included) meets the gate: the box
// from post (left, top) to post (right, bottom), both posts and the opening between them. No hoarding (extents of -1)
// meets nothing.
inline bool TpiEditMeetsGate(int32_t lox, int32_t loy, int32_t hix, int32_t hiy, int32_t left, int32_t top, int32_t right, int32_t bottom) {
    if (left < 0 || top < 0 || right < 0 || bottom < 0) return false;
    const int32_t gateLox = left < right ? left : right;
    const int32_t gateHix = left < right ? right : left;
    const int32_t gateLoy = top < bottom ? top : bottom;
    const int32_t gateHiy = top < bottom ? bottom : top;
    return lox <= gateHix && hix >= gateLox && loy <= gateHiy && hiy >= gateLoy;
}

// Whether a fill scan's cell (x, y) or one of the three cells of its square (+y, +x +y, +x) lies outside the level.
inline bool TpiFillCellLeavesLevel(int32_t x, int32_t y) {
    return x < 0 || y < 0 || x + 1 >= kTpiLevelSide || y + 1 >= kTpiLevelSide;
}

}  // namespace tpw

