// CellIdLookup.h: a map or track cell found by its 1 based id with plain arithmetic, as the Mac does it (MapCellOf and
// TrackCellOf in src/game/ui/ui_main.cpp, PointerCell in src/game/Mouse.cpp, the ticket queue in
// src/game/people/PersonQueue.cpp). The Mac computes (id - 1) * size in 32 bits and adds it to a 32 bit address, so the
// id 0 (no cell: a cell that belongs to nothing, the pointer off the map) is the cell before the first one, still inside
// CWorld: the map cells start at CWorld + 0x2d8, so map cell 0 is CWorld + 0x294, and the track cells follow the map
// cells, so track cell 0 is the last 0x28 bytes of the last map cell. On a 64 bit host the same 32 bit offset taken as
// unsigned is 4 GB past the cells instead.
// Map cell 0's thing list head (+0x24) is CWorld + 0x2b8, the high half of CParkTime's raw year (+0x18: 0 when loaded,
// then 2000 and on). On the big-endian Mac that half is 0, the end of a thing list, so CMapCell::GetObjectThingId and every
// walk of the list answer no thing. A little-endian host reads the low half there, the year itself, as a thing id, so the
// port answers no thing for map cell 0 without reading it. Header only and pure, so the host tests see what the game does.
#pragma once
// TPW_PORT_FIX cellzero CellIdOffset/CellIdWho/IsMapCellZero | Mac: (id - 1) * size wraps in 32 bits, so the id 0 is the cell before the first one, inside CWorld, and map cell 0's thing list is empty (the high half of the year) | Port: the same place by a signed offset, and an empty thing list for map cell 0 without reading it | Why: in a TPI Worlds park a hoarding post over a tunnel or bridge was reset by RepairTpiWalkways, its three other cells still named it, and pointing at one made ContextMouse read 4 GB past the map cells; the game crashed (0.5.5)
#include <cstddef>
#include <cstdint>
namespace tpw {

// The byte offset of the cell with the 1 based id `id` from the first cell, `size` bytes each, as the Mac's 32 bit sum
// gives it: the id 0 is one cell before the first.
inline constexpr std::ptrdiff_t CellIdOffset(uint32_t id, uint32_t size) {
    return static_cast<std::ptrdiff_t>(static_cast<int32_t>((id - 1u) * size));
}

// The cell with the 1 based id `id`, the cells starting at `first`.
template <class Cell>
inline Cell* CellIdAt(void* first, uint32_t id, uint32_t size) {
    return reinterpret_cast<Cell*>(static_cast<char*>(first) + CellIdOffset(id, size));
}

// The thing list head of the map cell with the id `id`, whose saved head is `who`: none (0) for the id 0, as the Mac reads it.
inline constexpr uint16_t CellIdWho(uint32_t id, uint16_t who) {
    return id == 0 ? static_cast<uint16_t>(0) : who;
}

// Whether `cell` is map cell 0, the one before `firstMapCell` (CWorld + 0x2d8), whose thing list the Mac reads as empty.
inline bool IsMapCellZero(const void* cell, const void* firstMapCell, uint32_t mapCellSize = 0x44u) {
    return reinterpret_cast<uintptr_t>(cell) + mapCellSize == reinterpret_cast<uintptr_t>(firstMapCell);
}

}  // namespace tpw
