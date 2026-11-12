// TpiGateFlow.h: the TPI Worlds and Rides mod's walks outside the park gate (TPW_MOD_HOOK tpi_worlds): where a visitor who
// gets off the bus waits to cross, and where a visitor who leaves waits for the bus, in the mod's worlds.
// The game's own walks (CPerson's constructor, BusStopCells and CWorld::ArrivalVehicle_PeopleWaitingToLeave) were made for
// Theme Park World's entrance: the stops on a pavement row with a second row two rows above it, and the crossing's bus side
// on that pavement. Theme Park Inc's entrance (the converted Standard.sam's fixed positions over the zone's own Base.map)
// is laid out otherwise: a pavement two rows deep with bus stop A and bus stop B on its upper row, the map's edge above it,
// and the road between it and the gate's plaza, which the walkers go round by the pavement at the east end. There the cells
// two rows above stop A and the crossing's bus side (on the road) are not walkable, so a visitor leaving waits for ever on
// the plaza in front of the gate, and a visitor arriving gets no walk to the crossing and wanders in without paying.
// In the mod's worlds the second row of the stop's cells is the row below the stop, and the crossing's bus side is in the
// crossing's column on that row too: both on the pavement. Header only and pure, so the host test sees what the game does.
#pragma once


#include <cstdint>

#include "TpiWorlds.h"

namespace tpw {

// The rows between bus stop A and the second pair of its four cells: Theme Park World's two rows up (BusStopCells, the
// Mac's -256 on the cell id), and in the mod's worlds the row below.
inline constexpr int32_t kTpwBusStopPairRows = -2;
inline constexpr int32_t kTpiBusStopPairRows = 1;

// TPW_PORT_FIX mods gTpiThemeIsMod | Mac: none | Port: TPW_MOD_HOOK tpi_worlds (gModTpiWorlds, include/TpiWorlds.h): the current theme's level folder is one of the mod's worlds, noted by CGame::SetCurrentThemeName (TpiNoteTheme) whenever the theme changes, so that the walks below read a flag and the units they are in need no game object | Why: the walks change only in the mod's worlds | Off: false, the theme as the Mac sets it
inline bool gTpiThemeIsMod = false;

// Notes whether the theme `theme` (a level folder name, or nullptr) is one of the mod's worlds, whatever the hook's flag.
inline void TpiNoteTheme(const char* theme) { gTpiThemeIsMod = TpiWorldOf(theme) != nullptr; }

// The mod's walks are used: the hook is on and the theme being played is one of the mod's worlds.
inline bool TpiWorldInPlay() { return gModTpiWorlds && gTpiThemeIsMod; }

// Cell `which` (0 to 3) of the four that a visitor leaving waits for the bus on: the stop, the cell east of it, and the
// same two `rows` rows down (negative: up). A map cell's id is x + (y << 7) + 1; the sum is cut to 16 bits, as the
// Mac's halfword store cuts it.
inline uint16_t TpiBusStopCell(uint16_t stop, int32_t which, int32_t rows) {
    const int32_t dx = which & 1;
    const int32_t dy = (which & 2) != 0 ? rows : 0;
    return static_cast<uint16_t>(dx + static_cast<int32_t>(static_cast<uint32_t>(dy) << 7) + stop);
}

// The bus side of a crossing in the mod's worlds: the crossing's column (`crossing`, the Standard.sam's CrossingBSSide
// cell) on the row below bus stop B (`stop`, where the bus lets visitors off), the pavement's lower row.
inline uint16_t TpiCrossingBusSide(uint16_t crossing, uint16_t stop) {
    const uint32_t x = (static_cast<uint32_t>(crossing) - 1u) & 0x7fu;
    const uint32_t y = ((static_cast<uint32_t>(stop) - 1u) >> 7) + 1u;
    return static_cast<uint16_t>(x + (y << 7) + 1u);
}

}  // namespace tpw

