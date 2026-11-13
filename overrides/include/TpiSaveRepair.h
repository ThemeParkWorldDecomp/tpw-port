// TpiSaveRepair.h: the TPI Worlds and Rides mod's repairs of parks saved before a converter showed more of a world's ground
// (TPW_MOD_HOOK tpi_worlds). A park file keeps every map cell's mode (CCellBase::Serialise), and a cell whose height field
// quad was hidden when the park started was given the hidden quad's mode by CMapCell::Reset (2, 0x1e or 7, from the
// attribute byte): no land to buy and no path. Converter 14 shows the quads of the bridges' walkways and ramps as land
// (the walkable bridges) and those of Land of Invention's gate, so a new park gets them as ordinary land, but a park saved
// with converter 12 still holds the hidden quad's mode on them. Loading such a park draws each cell
// again from its saved mode, and a mode 7 cell gets the plain tile, which hides its quad once more (SetHeightfieldTexture),
// so the bridges stay hidden ground that no path can cross. On loading a park in one of the mod's worlds, each map cell
// that still has the hidden quad's mode of its attribute, belongs to nothing and is not a path edge, while the level's own
// quad (gHeightfieldCopy, kept as the level was read) is shown, gets that quad back and is reset as a new park resets it
// (CMapCell::Reset), keeping whether it was inside the park (flag 0x40); its track cell is reset too when it has the hidden
// quad's mode 7. A cell the park has built on never matches, and a second load changes nothing.
// The same mod's repair of the shops of parks saved before converter 15: a park keeps each object type's control
// (CControlManager, made once from the record when the type is first seen), so its shops keep the RipOffOK converter 14
// gave them (0 where the shops folder named none) and visitors still walk away from their prices; on loading, a shop
// type's control whose RipOffOK is below its record's gets the record's. And a photo booth placed before converter 15
// keeps the object flags its record gave it then (CObject::mFlags, saved): without flag 4 (Info.IsChoosable) no visitor
// chooses it, and the state it was built in (kStateThree, which ModelState skips); a placed shop whose record now says
// Info.IsChoosable gets flag 4, and state 3 becomes the normal state a new booth gets.
// Header only and pure, so the host test sees the rule the game uses (src/game/save/SaveLoad.cpp).
#pragma once


// TPW_PORT_FIX mods TpiSaveRepair | Mac: no such header; Theme Park World has no converted worlds | Port: which map and track cells of a park saved before converter 14 RepairTpiHiddenCells resets as land on loading in the mod's worlds (TPW_MOD_HOOK tpi_worlds), and which shop controls and placed shops of a park saved before converter 15 RepairTpiShops repairs | Why: tpw-mods tpi-worlds; parks saved with 0.5.1 kept Land of Invention's walkable bridges hidden, so no path could cross them, and parks saved before 0.5.3 kept shop prices visitors walked away from and an unchoosable photo booth

#include <cstdint>

namespace tpw {

// The mode CMapCell::Reset gives a map cell whose height field quad is hidden (HFieldCellStatus 0), from its attribute
// byte: 2 for bit 2, else 0x1e for bit 0x80, else 7.
inline int32_t TpiHiddenQuadMode(uint8_t attribute) {
    if ((attribute & 2) != 0) return 2;
    if ((attribute & 0x80) != 0) return 0x1e;
    return 7;
}

// A map cell (its mode, attribute byte, parent and flags) that a park saved while its quad was hidden keeps, and whose
// quad the level now shows (`levelShows`: the level's own quad, in gHeightfieldCopy, has bit 1 clear): the hidden quad's
// mode of its attribute, no parent and not a path edge (flag 0x20).
inline bool TpiStaleHiddenMapCell(int32_t mode, uint8_t attribute, uint16_t parent, uint16_t flags, bool levelShows) {
    return levelShows && parent == 0 && (flags & 0x20) == 0 && mode == TpiHiddenQuadMode(attribute);
}

// The flags a reset cell keeps: CMapCell::Reset's (`reset`), with the saved cell's flag 0x40 (outside the park), so land
// the park owned stays its own; a start path edge (Reset's 0x20, which gets no 0x40) keeps Reset's flags.
inline uint16_t TpiRepairedFlags(uint16_t saved, uint16_t reset) {
    if ((reset & 0x20u) != 0) return reset;
    return static_cast<uint16_t>((reset & ~0x40u) | (saved & 0x40u));
}

// The same for a track cell (`shown`: its quad is shown once the map cell's is back): CTrackCell::Reset gives 7 to a
// hidden quad and 0 to a shown one.
inline bool TpiStaleHiddenTrackCell(int32_t mode, uint16_t parent, bool shown) { return shown && parent == 0 && mode == 7; }

// A control's RipOffOK after loading (`saved`, from the park file), for an object type with the record's RipOffOK
// `record` and Info.WhichUIType `uiType`: a shop's (1) rises to its record's; nothing else changes, and nothing falls.
inline int32_t TpiRepairedRipOffOK(int32_t saved, int32_t record, int32_t uiType) {
    return uiType == 1 && record > saved ? record : saved;
}

// Whether a placed object (its saved flags) is a shop (`uiType` 1) whose record now says Info.IsChoosable
// (`recordChoosable` not 0) but that was built without flag 4.
inline bool TpiShopToMakeChoosable(uint16_t flags, int32_t recordChoosable, int32_t uiType) {
    return uiType == 1 && recordChoosable != 0 && (flags & 4u) == 0;
}

// The state such a shop gets: kStateThree (3, given at building to an object no visitor chooses) becomes kStateNormal
// (0), what CObject's constructor gives a choosable one; any other state (broken down, condemned) is kept.
inline int32_t TpiRepairedShopState(int32_t state) { return state == 3 ? 0 : state; }

}  // namespace tpw

