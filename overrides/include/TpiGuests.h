// TpiGuests.h: the TPI Worlds and Rides mod's extra visitors (TPW_MOD_HOOK tpi_worlds): Theme Park Inc's kids, parents
// and grandparents walking among Theme Park World's kids in the mod's worlds, a look and nothing else; there, Theme Park
// World's TA and CH are not worn, since Theme Park Inc's BD and GB are the same two kids.
// The game's visitors are the sprites of the kids group (gESPGroups[0], "kids") and, on a ride, of the kids' heads
// (gESPGroups[1]); a visitor's sprite number (CPerson +0x20) is a random pick below the group's count, saved with the
// park. The converter writes Theme Park Inc's generic visitors into each of the mod's worlds as that theme's own kids
// and kidsheads (assets/tpi_convert.cpp, ConvertGuests). In the mod's worlds those are loaded after the group, outside
// its count (ESP_FindTpiExtraGuests), so every pick, the player's number of kid sprites and the saved sprite numbers stay
// what they were. Each sprite of the kids set, the group's and the extras, is a look with a kind (kid, parent,
// grandparent), a sex and whether it is worn, by its name (kTpiLookTags); a visitor wears the look its thing index gives
// (TpiGuestLook: 70 in 100 a kid look, 20 a parent's, 10 a grandparent's, as Theme Park Inc's levels/Standard.sam
// spreads its generations, each worn look of a kind as often as the others), only where its sprite script is made
// (TpiGuestSpriteOf).
// A script's sprite number past the group's count is saved, but every build without this hook brings it back below the
// count when the park is loaded (ESP_FixSpriteIDsFromDetail), so the visitor is a kid there.
// Header only and pure, so the host test sees what the game does.
#pragma once


#include <cstdint>

#include "TpiGateFlow.h"

namespace tpw {

// The kinds of look, Theme Park Inc's generations (SpriteTest.Generation 0, 1, 2).
enum : int8_t { kTpiKid = 0, kTpiParent = 1, kTpiGrandparent = 2 };
// The share of new visitors of each kind, in hundredths (Theme Park Inc's levels/Standard.sam: PercentageOfKids 70,
// PercentageOfParents 20, PercentageOfGrandparents 10).
inline constexpr int32_t kTpiKindShare[3] = {70, 20, 10};

struct TpiLookTag {
    const char* name;   // the sprite's name (its .ESP file's stem)
    int8_t kind;        // kTpiKid, kTpiParent or kTpiGrandparent
    int8_t female;      // 1 a girl or woman, 0 a boy or man
    int8_t worn;        // 1 a look the mod's worlds dress visitors in, 0 not
};
// Every visitor look, told from its pictures (the walk, from the front and the back), not from its name: Theme Park
// World's eight kids, then Theme Park Inc's four kids, two parents and two grandparents. Theme Park World's TA and CH
// are not worn: Theme Park Inc's BD and GB are the same two kids (the maintainer, 2026-11-14), so the mod's worlds' kid looks are
// Theme Park World's other six and Theme Park Inc's four, five boys and five girls.
inline constexpr TpiLookTag kTpiLookTags[] = {
    {"SPR_BE", kTpiKid, 0, 1},    // brown hair, green top
    {"SPR_BI", kTpiKid, 0, 1},    // blond, blue top
    {"SPR_CH", kTpiKid, 1, 0},    // black bunches with red bows, red dress (Theme Park Inc's GB)
    {"SPR_FR", kTpiKid, 0, 1},    // black hair, red top
    {"SPR_KI", kTpiKid, 1, 1},    // red bunches, green dungarees
    {"SPR_SA", kTpiKid, 1, 1},    // brown plaits, blue top
    {"SPR_SU", kTpiKid, 1, 1},    // blonde bunches, blue dungarees
    {"SPR_TA", kTpiKid, 0, 0},    // short dark hair, red top (Theme Park Inc's BD)
    {"SPR_BD", kTpiKid, 0, 1},    // Theme Park Inc's: short dark hair, red top
    {"SPR_GB", kTpiKid, 1, 1},    // black bunches with red bows, red dress
    {"SPR_GK", kTpiKid, 1, 1},    // pink cap with a flower, green trousers
    {"SPR_KB", kTpiKid, 0, 1},    // yellow cap turned back, blue dungarees
    {"SPR_FA", kTpiParent, 0, 1},     // the father: hat, yellow shirt, camera
    {"SPR_MO", kTpiParent, 1, 1},     // the mother: blonde, pink top
    {"SPR_GM", kTpiGrandparent, 1, 1},  // the grandmother: white hair, pink cardigan, blue dress
    {"SPR_GR", kTpiGrandparent, 0, 1},  // the grandfather: grey hair, brown coat
};

// The tag of the sprite named `name` (a path or a stem; the part after the last ':', '/' or '\\', any case), or null.
inline const TpiLookTag* TpiLookTagOf(const char* name) {
    if (name == nullptr) return nullptr;
    const char* stem = name;
    for (const char* p = name; *p != 0; ++p)
        if (*p == ':' || *p == '/' || *p == '\\') stem = p + 1;
    for (const TpiLookTag& tag : kTpiLookTags) {
        int32_t i = 0;
        for (; tag.name[i] != 0 && stem[i] != 0; ++i) {
            const char a = stem[i] >= 'a' && stem[i] <= 'z' ? static_cast<char>(stem[i] - 32) : stem[i];
            if (a != tag.name[i]) break;
        }
        if (tag.name[i] == 0 && stem[i] == 0) return &tag;
    }
    return nullptr;
}

inline constexpr int32_t kTpiMaxLooks = 32;

// TPW_PORT_FIX mods gTpiExtraGuests | Mac: none | Port: TPW_MOD_HOOK tpi_worlds (gModTpiWorlds, include/TpiWorlds.h): the sprites loaded after the kids (0) and kidsheads (1) groups in one of the mod's worlds, set by ESP_LoadSprites | Why: tpw-mods tpi-worlds extra visitors | Off: 0, nothing loaded past the groups
inline int32_t gTpiExtraGuests[2] = {0, 0};

}  // namespace tpw


namespace tpw {

// TPW_PORT_FIX mods gTpiLooks | Mac: none | Port: TPW_MOD_HOOK tpi_worlds: the looks of the kids set in one of the mod's worlds (the group's sprites and the extras, in sprite order): how many, each one's kind and sex (-1 a sprite kTpiLookTags does not name, taken as a kid), the kind TpiGuestLook picks it as (-1 never: a look kTpiLookTags marks not worn) and its head's place in the kidsheads set (-1 none), set by ESP_LoadSprites (TpiSetLooks) so that the visitors' code reads them without the sprite tables (the test libraries hold only one unit each) | Why: tpw-mods tpi-worlds extra visitors | Off: 0 looks, unread
inline int32_t gTpiLookCount = 0;
inline int8_t gTpiLookKind[kTpiMaxLooks] = {};
inline int8_t gTpiLookFemale[kTpiMaxLooks] = {};
inline int8_t gTpiLookPick[kTpiMaxLooks] = {};
inline int16_t gTpiHeadOf[kTpiMaxLooks] = {};

// Sets the looks from the names of the kids set's `bodyCount` sprites and the kidsheads set's `headCount` (each a path
// or a stem): a head is the kidsheads sprite of the body's name.
inline void TpiSetLooks(const char* const* bodies, int32_t bodyCount, const char* const* heads, int32_t headCount) {
    gTpiLookCount = bodyCount < kTpiMaxLooks ? (bodyCount > 0 ? bodyCount : 0) : kTpiMaxLooks;
    for (int32_t b = 0; b < gTpiLookCount; ++b) {
        const TpiLookTag* tag = TpiLookTagOf(bodies[b]);
        gTpiLookKind[b] = tag != nullptr ? tag->kind : kTpiKid;
        gTpiLookFemale[b] = tag != nullptr ? tag->female : -1;
        gTpiLookPick[b] = tag == nullptr ? kTpiKid : tag->worn ? tag->kind : -1;
        gTpiHeadOf[b] = -1;
        for (int32_t h = 0; h < headCount && tag != nullptr; ++h)
            if (TpiLookTagOf(heads[h]) == tag) {
                gTpiHeadOf[b] = static_cast<int16_t>(h);
                break;
            }
    }
}

// An even spread of 32 bits (the finaliser of a well-known integer hash): the thing indexes of visitors arriving one
// after another give unrelated numbers.
inline uint32_t TpiMix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// The look (a place in the kids set) of the visitor with thing index `thing`, among `count` looks picked as kinds `pick`
// (gTpiLookPick; -1 a look never picked), or -1 when there are none. A fixed spread of the thing index, so a visitor
// keeps its look whenever its script is made again (and after a park is loaded: the thing index is saved), and no random
// number is drawn: its hundredth picks the kind (kTpiKindShare; a kind with no look gives a kid's), a second spread one
// of that kind's looks.
inline int32_t TpiGuestLook(uint16_t thing, int32_t count, const int8_t* pick) {
    if (count <= 0) return -1;
    const uint32_t spread = TpiMix(0x9e3779b9u ^ thing);
    const int32_t share = static_cast<int32_t>(spread % 100u);
    int32_t want = share < kTpiKindShare[0] ? kTpiKid : share < kTpiKindShare[0] + kTpiKindShare[1] ? kTpiParent : kTpiGrandparent;
    int32_t looks = 0;
    for (int32_t i = 0; i < count; ++i) looks += pick[i] == want;
    if (looks == 0) {
        want = kTpiKid;
        for (int32_t i = 0; i < count; ++i) looks += pick[i] == want;
        if (looks == 0) return -1;
    }
    int32_t nth = static_cast<int32_t>(TpiMix(spread ^ 0x68e31da4u) % static_cast<uint32_t>(looks));
    for (int32_t i = 0; i < count; ++i)
        if (pick[i] == want && nth-- == 0) return i;
    return -1;
}

// The thing index of `thing`, a CThing (or a class that starts with one): the half-word at +0 that CThing::GetThingId
// returns (include/analyser.h). Read here rather than called, so a visitor's code calls nothing more than the Mac's.
inline uint16_t TpiThingIndex(const void* thing) { return *static_cast<const uint16_t*>(thing); }

// The sprite a visitor's script is made with (CPerson::CPerson and the other places that call it): `index` of set
// `set`, or in one of the mod's worlds with extra visitors loaded the visitor's look (set 0) or that look's head (set 1,
// `index` when the look has none). Anywhere else it is `index`, and `person` is not read.
inline int32_t TpiGuestSpriteOf(int32_t set, int32_t index, const void* person) {
    if (!TpiWorldInPlay() || (set != 0 && set != 1) || gTpiExtraGuests[0] <= 0) return index;
    const int32_t look = TpiGuestLook(TpiThingIndex(person), gTpiLookCount, gTpiLookPick);
    if (look < 0) return index;
    if (set == 0) return look;
    return gTpiHeadOf[look] >= 0 ? gTpiHeadOf[look] : index;
}

}  // namespace tpw

