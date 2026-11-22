// TpiUmbrellas.h: the TPI Worlds and Rides mod's umbrellas (TPW_MOD_HOOK tpi_worlds): in the mod's worlds a visitor who
// buys from an Umbrella Shop walks about holding Theme Park Inc's umbrella, as a visitor holds a balloon.
// Theme Park Inc's umbrella shops say UsageInfo.AppearanceEffect 3 ("Give the person an umbrella"); Theme Park World's
// ride database takes only 0 (nothing), 1 (a balloon) and 2 (a costume), so the converted shop's 3 is dropped and the
// shop gives nothing. The converter writes the ids of the shops that said 3, with Theme Park Inc's umbrella lines of its
// levels/Standard.sam (PeepInfo.UmbrellaModifier and DiscardBalloonPctModifier, GenericUmbrellaModifiers.*,
// PersonSpriteUmbrellaModifiers[i].Y), into each of the mod's worlds as TpiUmbrellas.sam (assets/tpi_convert.cpp,
// TpiUmbrellasSam), and Theme Park Inc's Generic\Umbrellas\Spr_um sprite as the world's own umbrellas group;
// CWorld::SetupNewLevel reads the file (TpiParseUmbrellas) into gTpiUmbrellas and ESP_LoadSprites loads the sprite after
// the balloons, outside their count (gTpiUmbrellaSprites), so a random balloon is never an umbrella.
// The visitor's balloon fields carry the umbrella: its script (CPerson +0x210) is the umbrella's, made from the balloon's
// script block, and its balloon life (+0x214) is kTpiUmbrellaLife, one more than any balloon's life (25 to 255), which
// never runs down. Both are saved as the balloon's are, so a park keeps its umbrellas with nothing added to the save.
// A build without this hook (the mod off, an older build) reads the umbrella as a balloon (ESP_FixSpriteIDsFromDetail
// brings its sprite number back below the balloons' count) that pops after 256 turns, as a balloon of that life does.
// Header only and pure, so the host test sees what the game does.
#pragma once


#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "TpiGateFlow.h"
#include "TpiGenerations.h"
#include "TpiGuests.h"

namespace tpw {

// The file the converter writes into each of the mod's worlds that has an umbrella shop (data/levels/<world>/TpiUmbrellas.sam).
inline constexpr char kTpiUmbrellasFile[] = "TpiUmbrellas.sam";
inline constexpr int32_t kTpiMaxUmbrellaShops = 16;
// Theme Park Inc's PersonSpriteUmbrellaModifiers rows: one for each of its visitor looks (kTpiUmbrellaRows below).
inline constexpr int32_t kTpiUmbrellaRows = 10;
// The balloon life (CPerson +0x214) of a visitor holding an umbrella: one more than the most a balloon has (gBalloonLifeMax).
inline constexpr uint32_t kTpiUmbrellaLife = 0x100;
// The umbrella's height over the ground in Theme Park World's units per unit of Theme Park Inc's
// PersonSpriteUmbrellaModifiers[i].Y (a kid's 5.7 is 2.565 units): the handle's foot (the sprite's anchor) at the top of
// the visitor's head, so the canopy is over the head, fitted from close-ups of the converted Polar Zone (0.25 and 0.35
// put the canopy behind the face), as Theme Park Inc measures the height otherwise than Theme Park World places a sprite.
inline constexpr float kTpiUmbrellaHeightUnit = 0.45f;

struct TpiUmbrellaTable {
    bool loaded = false;                  // one shop at least (TpiParseUmbrellas)
    int32_t shopCount = 0;
    int32_t shops[kTpiMaxUmbrellaShops] = {};   // the converted Info.Id of each umbrella shop
    int32_t modifier = 0;                 // PeepInfo.UmbrellaModifier: the unhappiness points bad weather does not take
    int32_t discardPct = 100;             // PeepInfo.DiscardBalloonPctModifier, 0 to 100
    float scale = 1.0f;                   // GenericUmbrellaModifiers.ScalingFactor
    float adjust[3] = {0.0f, 0.0f, 0.0f};   // GenericUmbrellaModifiers.X/Y/ZAdjustCamera
    float y[kTpiUmbrellaRows] = {};       // PersonSpriteUmbrellaModifiers[i].Y
    uint32_t ySet = 0;                    // bit i: row i given
};

// TPW_PORT_FIX mods gTpiUmbrellas | Mac: none | Port: TPW_MOD_HOOK tpi_worlds (gModTpiWorlds, include/TpiWorlds.h): the umbrella state of the world being played, gTpiUmbrellas, gTpiUmbrellaSprites, gTpiUmbrellaRow and gTpiCameraAt below, set in one of the mod's worlds and cleared in any other | Why: tpw-mods tpi-worlds umbrellas | Off: not loaded, 0, unread
// The umbrella shops and Theme Park Inc's umbrella values, read by CWorld::SetupNewLevel from the world's TpiUmbrellas.sam
// in one of the mod's worlds (TpiParseUmbrellas) and cleared in any other.
inline TpiUmbrellaTable gTpiUmbrellas;
// The umbrella sprites loaded after the balloons group (10) in one of the mod's worlds, set by ESP_LoadSprites; 0 elsewhere.
inline int32_t gTpiUmbrellaSprites = 0;
// The PersonSpriteUmbrellaModifiers row of each look of the kids set (gTpiLookCount of them), set by ESP_LoadSprites
// (TpiSetUmbrellaRows), so each look holds it at its own height.
inline int8_t gTpiUmbrellaRow[kTpiMaxLooks] = {};
// The camera's position (gCameraPosition), set by CWorld::SetupNewLevel, so the umbrella is moved toward it
// (GenericUmbrellaModifiers.*AdjustCamera) without CPeep::UpdatePositionInEngine linking more than the Mac's; nullptr before.
inline const float* gTpiCameraAt = nullptr;

// The umbrella step CPerson::SetState calls through this when a visitor holding an umbrella comes out with no script
// for it (src/game/World.cpp, TpiSpawnUmbrella: the umbrella's script, made from the balloon's script block): its handle,
// 0 for none. Set by CWorld::SetupNewLevel, so PersonQueue.cpp links nothing more than the Mac's; nullptr before.
int32_t TpiSpawnUmbrella(const void* person);
inline int32_t (*gTpiUmbrellaSpawn)(const void* person) = nullptr;

namespace detail {
// Theme Park Inc's PersonSpriteUmbrellaModifiers rows by its looks (the remarks in its levels/Standard.sam: 0 the child,
// 1 brunette pigtails, 2 the blonde girl with the cap, 3 the boy with the yellow cap, 4 the fat bloke, 5 the tall woman,
// 8 the old biddy, 9 the old bloke; 6 and 7 are not given).
struct TpiUmbrellaRowTag {
    const char* name;
    int8_t row;
};
inline constexpr TpiUmbrellaRowTag kTpiUmbrellaRowTags[] = {{"SPR_BD", 0}, {"SPR_GB", 1}, {"SPR_GK", 2}, {"SPR_KB", 3},
                                                            {"SPR_FA", 4}, {"SPR_MO", 5}, {"SPR_GM", 8}, {"SPR_GR", 9}};
}  // namespace detail

// The PersonSpriteUmbrellaModifiers row of the look named `name` (a path or a stem, any case): Theme Park Inc's look's
// own; Theme Park World's kids, and a name not known, take row 0, Theme Park Inc's first kid's (its four kids' rows are
// the same).
inline int8_t TpiUmbrellaRowOf(const char* name) {
    const TpiLookTag* tag = TpiLookTagOf(name);
    if (tag == nullptr) return 0;
    for (const detail::TpiUmbrellaRowTag& row : detail::kTpiUmbrellaRowTags)
        if (std::strcmp(row.name, tag->name) == 0) return row.row;
    return 0;
}

// Sets the rows of the kids set's `count` looks from their names (ESP_LoadSprites).
inline void TpiSetUmbrellaRows(const char* const* bodies, int32_t count) {
    for (int32_t b = 0; b < kTpiMaxLooks; ++b) gTpiUmbrellaRow[b] = b < count ? TpiUmbrellaRowOf(bodies[b]) : 0;
}

// Reads `length` bytes of a TpiUmbrellas.sam into `t`, from nothing: "UmbrellaShop <id>" lines (each id once, 1 to 65534)
// and Theme Park Inc's lines as its .sam has them ("Group.Field value", "Group[i].Field value", words after the value
// and '#' to the end of a line a remark); other lines and values out of range are passed over. True, and loaded, with one shop at least.
inline bool TpiParseUmbrellas(const char* text, size_t length, TpiUmbrellaTable& t) {
    t = TpiUmbrellaTable{};
    const char* p = text;
    const char* const end = text + length;
    while (text != nullptr && p < end) {
        const char* line = p;
        while (p < end && *p != '\n') ++p;
        const char* lineEnd = p;
        if (p < end) ++p;
        for (const char* h = line; h < lineEnd; ++h)
            if (*h == '#') {
                lineEnd = h;
                break;
            }
        const char* q = line;
        while (q < lineEnd && (detail::PeepSpace(*q) || *q == '\r')) ++q;
        const char* name = q;
        while (q < lineEnd && !detail::PeepSpace(*q) && *q != '\r') ++q;
        const char* nameEnd = q;
        while (q < lineEnd && (detail::PeepSpace(*q) || *q == '\r')) ++q;
        double v = 0.0;
        if (!detail::PeepNumber(q, lineEnd, v) || (q < lineEnd && !detail::PeepSpace(*q) && *q != '\r')) continue;
        const size_t n = static_cast<size_t>(nameEnd - name);
        if (detail::PeepEqual(name, n, "UmbrellaShop")) {
            if (v < 1.0 || v > 65534.0 || v != std::floor(v) || t.shopCount >= kTpiMaxUmbrellaShops) continue;
            bool seen = false;
            for (int32_t i = 0; i < t.shopCount; ++i) seen = seen || t.shops[i] == static_cast<int32_t>(v);
            if (!seen) t.shops[t.shopCount++] = static_cast<int32_t>(v);
        } else if (detail::PeepEqual(name, n, "PeepInfo.UmbrellaModifier")) {
            if (v >= 0.0 && v <= 100.0) t.modifier = static_cast<int32_t>(v);
        } else if (detail::PeepEqual(name, n, "PeepInfo.DiscardBalloonPctModifier")) {
            if (v >= 0.0 && v <= 100.0) t.discardPct = static_cast<int32_t>(v);
        } else if (detail::PeepEqual(name, n, "GenericUmbrellaModifiers.ScalingFactor")) {
            if (v > 0.0 && v <= 10.0) t.scale = static_cast<float>(v);
        } else if (detail::PeepEqual(name, n, "GenericUmbrellaModifiers.XAdjustCamera")) {
            if (std::fabs(v) <= 10.0) t.adjust[0] = static_cast<float>(v);
        } else if (detail::PeepEqual(name, n, "GenericUmbrellaModifiers.YAdjustCamera")) {
            if (std::fabs(v) <= 10.0) t.adjust[1] = static_cast<float>(v);
        } else if (detail::PeepEqual(name, n, "GenericUmbrellaModifiers.ZAdjustCamera")) {
            if (std::fabs(v) <= 10.0) t.adjust[2] = static_cast<float>(v);
        } else {
            static constexpr char kRow[] = "PersonSpriteUmbrellaModifiers[";
            static constexpr char kField[] = "].Y";
            const size_t rn = sizeof kRow - 1, fn = sizeof kField - 1;
            if (n <= rn + fn || std::memcmp(name, kRow, rn) != 0 || std::memcmp(nameEnd - fn, kField, fn) != 0) continue;
            const char* digits = name + rn;
            double row = 0.0;
            if (!detail::PeepNumber(digits, nameEnd - fn, row) || digits != nameEnd - fn) continue;
            if (row < 0.0 || row >= kTpiUmbrellaRows || row != std::floor(row) || v <= 0.0 || v > 100.0) continue;
            t.y[static_cast<int32_t>(row)] = static_cast<float>(v);
            t.ySet |= 1u << static_cast<int32_t>(row);
        }
    }
    t.loaded = t.shopCount > 0;
    return t.loaded;
}

// The umbrellas are in play: one of the mod's worlds with its umbrella shops read and its umbrella sprite loaded.
inline bool TpiUmbrellasInPlay() { return TpiWorldInPlay() && gTpiUmbrellas.loaded && gTpiUmbrellaSprites > 0; }

// Whether the ride type `id` is one of the world's umbrella shops (in play only).
inline bool TpiIsUmbrellaShop(int32_t id) {
    if (!TpiUmbrellasInPlay()) return false;
    for (int32_t i = 0; i < gTpiUmbrellas.shopCount; ++i)
        if (gTpiUmbrellas.shops[i] == id) return true;
    return false;
}

inline uint32_t TpiPersonWord(const void* person, int32_t offset) {
    uint32_t v = 0;
    std::memcpy(&v, static_cast<const uint8_t*>(person) + offset, sizeof v);
    return v;
}

// Whether the visitor `person` (a CPerson) holds an umbrella: in play, its balloon life is kTpiUmbrellaLife.
inline bool TpiHoldsUmbrella(const void* person) {
    return person != nullptr && TpiUmbrellasInPlay() && TpiPersonWord(person, 0x214) == kTpiUmbrellaLife;
}

// What a visitor with balloon life `life` gets from an umbrella shop (CPerson::WinningMods), by its thing index `thing`
// and Theme Park Inc's DiscardBalloonPctModifier `discardPct`: the life it has after (kTpiUmbrellaLife, an umbrella).
// A visitor with a balloon (life 1 to 255) throws it away for the umbrella `discardPct` times in 100, a fixed spread of
// its thing index (no random number is drawn), and keeps the balloon otherwise; Theme Park Inc's is 100, always.
inline uint32_t TpiUmbrellaLifeAfter(uint32_t life, uint16_t thing, int32_t discardPct) {
    if (life == 0 || life == kTpiUmbrellaLife) return kTpiUmbrellaLife;
    const uint32_t spread = TpiMix(TpiMix(0x85ebca6bu ^ thing) ^ 0x27d4eb2fu) % 100u;
    return static_cast<int32_t>(spread) < discardPct ? kTpiUmbrellaLife : life;
}

// The umbrella's colour (an action of the sprite, of `actions`) for the visitor with thing index `thing`: a fixed spread,
// so it keeps its colour whenever the umbrella is made again and no random number is drawn.
inline int32_t TpiUmbrellaColour(uint16_t thing, int32_t actions) {
    if (actions <= 0) return 0;
    return static_cast<int32_t>(TpiMix(TpiMix(0xc2b2ae35u ^ thing) ^ 0x165667b1u) % static_cast<uint32_t>(actions));
}

// The look of a visitor (a CPerson), as TpiGuestGeneration finds it, or -1.
inline int32_t TpiUmbrellaLookOf(const void* person) {
    if (gTpiExtraGuests[0] > 0) return TpiGuestLook(TpiThingIndex(person), gTpiLookCount, gTpiLookPick);
    const int32_t sprite = static_cast<int32_t>(TpiPersonWord(person, 0x20));
    const int32_t set = static_cast<int32_t>(TpiPersonWord(person, 0x24));
    return set == 0 ? sprite : -1;
}

// The umbrella's height over the ground for the look `look` (rows `rows`, gTpiUmbrellaRow): its row's
// PersonSpriteUmbrellaModifiers Y in Theme Park World's units, row 0's for a look not known or a row not given, and a
// kid's 5.7 (Theme Park Inc's) when neither is given.
inline float TpiUmbrellaHeight(int32_t look, const int8_t* rows, int32_t lookCount, const TpiUmbrellaTable& t) {
    int32_t row = look >= 0 && look < lookCount && look < kTpiMaxLooks ? rows[look] : 0;
    if (row < 0 || row >= kTpiUmbrellaRows || (t.ySet & (1u << row)) == 0) row = 0;
    const float y = (t.ySet & (1u << row)) != 0 ? t.y[row] : 5.7f;
    return y * kTpiUmbrellaHeightUnit;
}

// Where the umbrella of a visitor at `x`, `z` (its sprite's place) is drawn, height `y`: moved toward the camera at
// `camera` (null: not moved) by GenericUmbrellaModifiers' X/Y/ZAdjustCamera along each axis of the way to it, so the
// umbrella is in front of the visitor who holds it.
inline void TpiUmbrellaPlace(float x, float y, float z, const float* camera, const TpiUmbrellaTable& t, float out[3]) {
    out[0] = x;
    out[1] = y;
    out[2] = z;
    if (camera == nullptr) return;
    const float d[3] = {camera[0] - x, camera[1] - y, camera[2] - z};
    const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(length > 0.0f)) return;
    for (int32_t i = 0; i < 3; ++i) out[i] += t.adjust[i] * d[i] / length;
}

// The new park day's weather step for a visitor holding an umbrella (TpiGenerationsNewDay): Theme Park Inc's
// UmbrellaModifier ("prevent one unhappiness point for bad weather"), so in rain, a storm or snow the happiness the
// weather takes, `effect` (below 0), is less by `modifier` points, never turned into a gain; a heatwave, no weather or a
// gain is as it is.
inline float TpiUmbrellaWeatherEffect(float effect, int32_t kind, int32_t modifier) {
    if ((kind != kTpiRain && kind != kTpiStorm && kind != kTpiSnow) || !(effect < 0.0f)) return effect;
    const float less = effect + static_cast<float>(modifier);
    return less > 0.0f ? 0.0f : less;
}

}  // namespace tpw

