// TpiGuests.h: the TPI Worlds and Rides mod's extra visitors (TPW_MOD_HOOK tpi_worlds): Theme Park Inc's grandparents
// walking among Theme Park World's kids in the mod's worlds, a look and nothing else.
// The game's visitors are the sprites of the kids group (gESPGroups[0], "kids") and, on a ride, of the kids' heads
// (gESPGroups[1]); a visitor's sprite number (CPerson +0x20) is a random pick below the group's count, saved with the
// park. The converter writes Theme Park Inc's grandparents into each of the mod's worlds as that theme's own kids and
// kidsheads (assets/tpi_convert.cpp, ConvertGuests). In the mod's worlds those are loaded after the group, outside its
// count (ESP_FindTpiExtraGuests), so every pick, the player's number of kid sprites and the saved sprite numbers stay
// what they were; a visitor wears an extra one by its thing id (TpiGuestPick) only where its sprite script is made
// (TpiGuestSpriteOf). A script's sprite number past the group's count is saved, but every build without this hook
// brings it back below the count when the park is loaded (ESP_FixSpriteIDsFromDetail), so the visitor is a kid there.
// Header only and pure, so the host test sees what the game does.
#pragma once


#include <cstdint>

#include "TpiGateFlow.h"

namespace tpw {

// TPW_PORT_FIX mods gTpiExtraGuests | Mac: none | Port: TPW_MOD_HOOK tpi_worlds (gModTpiWorlds, include/TpiWorlds.h): the sprites loaded after the kids (0) and kidsheads (1) groups in one of the mod's worlds, set by ESP_LoadSprites | Why: tpw-mods tpi-worlds extra visitors | Off: 0, nothing loaded past the groups
inline int32_t gTpiExtraGuests[2] = {0, 0};

}  // namespace tpw


namespace tpw {

// TPW_PORT_FIX mods gTpiGuestCounts | Mac: none | Port: TPW_MOD_HOOK tpi_worlds: the kids (0) and kidsheads (1) groups' counts (gESPGroups[i].mCount), kept by ESP_LoadSprites beside gTpiExtraGuests so that the visitors' code reads them without the sprite tables (the test libraries hold only one unit each) | Why: tpw-mods tpi-worlds extra visitors | Off: what the groups hold, unread
inline int32_t gTpiGuestCounts[2] = {0, 0};

// The extra visitor (0 to `extras` - 1) that the visitor with thing index `thing` wears, or -1 for a kid. Of `kids` +
// `extras` equal shares, the last `extras` are the extra visitors: a fixed spread of the thing index (Knuth's
// multiplicative hash), so a visitor keeps its look whenever its script is made again, and no random number is drawn.
inline int32_t TpiGuestPick(uint16_t thing, int32_t kids, int32_t extras) {
    if (kids <= 0 || extras <= 0) return -1;
    const uint32_t spread = (static_cast<uint32_t>(thing) * 2654435761u) >> 16;
    const int32_t share = static_cast<int32_t>(spread % static_cast<uint32_t>(kids + extras));
    return share < kids ? -1 : share - kids;
}

// The sprite in set `set` that the visitor with thing index `thing` and sprite number `index` is drawn with: `index`, or
// for the kids (0) and kidsheads (1) sets the extra visitor's place past the set's `count` sprites when TpiGuestPick
// gives one and the set has it. `kidsCount` and `kidsExtras` are the kids group's count and extras (one pick for the
// body and the head, so the two match); `setExtras` the set's extras.
inline int32_t TpiGuestSprite(int32_t set, int32_t index, uint16_t thing, int32_t kidsCount, int32_t kidsExtras,
                              int32_t count, int32_t setExtras) {
    if (set != 0 && set != 1) return index;
    const int32_t extra = TpiGuestPick(thing, kidsCount, kidsExtras);
    if (extra < 0 || extra >= setExtras) return index;
    return count + extra;
}

// The thing index of `thing`, a CThing (or a class that starts with one): the half-word at +0 that CThing::GetThingId
// returns (include/analyser.h). Read here rather than called, so a visitor's code calls nothing more than the Mac's.
inline uint16_t TpiThingIndex(const void* thing) { return *static_cast<const uint16_t*>(thing); }

// The sprite a visitor's script is made with (CPerson::CPerson and the other places include/TpiGuests.h's top names):
// `index` of set `set`, or in one of the mod's worlds the extra visitor TpiGuestSprite gives `person` (a CPerson).
// Anywhere else it is `index`, and `person` is not read.
inline int32_t TpiGuestSpriteOf(int32_t set, int32_t index, const void* person) {
    if (!TpiWorldInPlay() || (set != 0 && set != 1)) return index;
    return TpiGuestSprite(set, index, TpiThingIndex(person), gTpiGuestCounts[0], gTpiExtraGuests[0], gTpiGuestCounts[set],
                          gTpiExtraGuests[set]);
}

}  // namespace tpw

