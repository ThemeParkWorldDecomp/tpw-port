// TpiGenerations.h: the TPI Worlds and Rides mod's three generations of visitors (TPW_MOD_HOOK tpi_worlds): in the
// mod's worlds a visitor acts as Theme Park Inc's visitor of its look's generation (kid, parent or grandparent,
// include/TpiGuests.h), not only looks like one.
// Theme Park Inc's levels/Standard.sam has 24 visitor types, 8 a generation (PeepTypes[i].Generation 0, 1, 2), each with
// a preferred excitement, a starting cash, a boredom threshold and a speed (SpeedMultiplier), and per generation the
// effects of the four kinds of weather on happiness, thirst and hunger (Peep*WeatherEffects[generation].Heatwave, Rain,
// Storm, Snow), applied every PeepInfo.WeatherModifierFrequency days. Theme Park World's has 8 types, no generations, no
// speeds and no weather effects. The converter copies those lines of the player's own copy into each of the mod's
// worlds as TpiPeeps.sam (assets/tpi_convert.cpp, TpiPeepsSam), with the world's own heatwave quality
// (WeatherEffects.QualityForHeatWaveLow); CWorld::SetupNewLevel reads it (TpiParsePeeps) into gTpiPeeps.
// A visitor's type is a fixed spread of its thing index among its generation's types (TpiPeepTypeOf), so no random
// number is drawn, the Mac's own type (CPerson +0x1f0) stays as it is, and the visitor keeps its type after a park is
// loaded (the thing index is saved). Where the game reads a type's value, the mod's worlds read the Theme Park Inc
// type's: the starting cash with PeepInfo.StartingCashVarPc's spread from the Mac's own draw (CPerson::CPerson), the
// preferred excitement (CPerson::GetPreferredExcitement), the speed (CPeep::UpdateTurn) and the weather (CParkTime::Update,
// TpiWeatherDue). Theme Park World reads no BoredomThreshold anywhere, so the type's is read here but used nowhere.
// Header only and pure, so the host test sees what the game does.
#pragma once


#include <cstddef>
#include <cstdint>
#include <cstring>

#include "TpiGateFlow.h"
#include "TpiGuests.h"

namespace tpw {

// The file the converter writes into each of the mod's worlds (data/levels/<world>/TpiPeeps.sam).
inline constexpr char kTpiPeepsFile[] = "TpiPeeps.sam";

inline constexpr int32_t kTpiMaxPeepTypes = 32;
// The kinds of weather, in the order of Theme Park Inc's tables' fields; kTpiNoWeather is none of them.
enum : int32_t { kTpiHeatwave = 0, kTpiRain = 1, kTpiStorm = 2, kTpiSnow = 3, kTpiNoWeather = -1 };

struct TpiPeepType {
    int32_t excitement = 0;   // PreferredExcitement, 0 to 100
    int32_t cash = 0;         // StartingCash
    int32_t boredom = 0;      // BoredomThreshold (read, used nowhere: Theme Park World has no reader)
    int32_t generation = -1;  // Generation: kTpiKid, kTpiParent, kTpiGrandparent
    float speed = 0.0f;       // SpeedMultiplier
    uint8_t set = 0;          // bit 0 the four integers given, bit 1 the speed
};

struct TpiPeepTable {
    bool loaded = false;      // every type complete and each generation with one at least (TpiParsePeeps)
    int32_t typeCount = 0;    // the highest type given + 1
    TpiPeepType types[kTpiMaxPeepTypes];
    int32_t cashVarPc = -1;   // PeepInfo.StartingCashVarPc, 0 to 99 (-1 not given: the Mac's)
    int32_t weatherEvery = 0; // PeepInfo.WeatherModifierFrequency, days (0 not given: no weather effects)
    float happiness[3][4] = {};   // Peep*WeatherEffects[generation].<kind>
    float thirst[3][4] = {};
    float hunger[3][4] = {};
    int32_t heatwaveLow = 101;    // WeatherEffects.QualityForHeatWaveLow (101 not given: no heatwave)
    // Each generation's types in type order (TpiParsePeeps fills it).
    int32_t genCount[3] = {0, 0, 0};
    int32_t byGen[3][kTpiMaxPeepTypes] = {};
};

// TPW_PORT_FIX mods gTpiPeeps | Mac: none | Port: TPW_MOD_HOOK tpi_worlds (gModTpiWorlds, include/TpiWorlds.h): Theme Park Inc's visitor types and weather effects of the world being played, read by CWorld::SetupNewLevel from the world's TpiPeeps.sam in one of the mod's worlds (TpiParsePeeps) and cleared in any other | Why: tpw-mods tpi-worlds generations | Off: not loaded, unread
inline TpiPeepTable gTpiPeeps;

namespace detail {
inline bool PeepSpace(char c) { return c == ' ' || c == '\t'; }
inline bool PeepEqual(const char* a, size_t n, const char* b) { return std::strlen(b) == n && std::memcmp(a, b, n) == 0; }
// A decimal number ("-1.5", "95", "0.47") at `p`, below `end`: true with the value and `p` past it. Read by hand, so the
// player's locale's decimal point does not matter.
inline bool PeepNumber(const char*& p, const char* end, double& value) {
    const char* q = p;
    bool negative = false;
    if (q < end && (*q == '-' || *q == '+')) negative = *q++ == '-';
    double v = 0.0;
    int32_t digits = 0;
    while (q < end && *q >= '0' && *q <= '9') {
        v = v * 10.0 + (*q++ - '0');
        ++digits;
    }
    if (q < end && *q == '.') {
        ++q;
        double scale = 0.1;
        while (q < end && *q >= '0' && *q <= '9') {
            v += (*q++ - '0') * scale;
            scale *= 0.1;
            ++digits;
        }
    }
    if (digits == 0) return false;
    value = negative ? -v : v;
    p = q;
    return true;
}
inline int32_t PeepWeatherField(const char* f, size_t n) {
    if (PeepEqual(f, n, "Heatwave")) return kTpiHeatwave;
    if (PeepEqual(f, n, "Rain")) return kTpiRain;
    if (PeepEqual(f, n, "Storm")) return kTpiStorm;
    if (PeepEqual(f, n, "Snow")) return kTpiSnow;
    return -1;
}
// One field's value into the table; a field or group it does not know is passed over.
inline void PeepAssign(TpiPeepTable& t, const char* g, size_t gn, int32_t index, const char* f, size_t fn, double v) {
    if (PeepEqual(g, gn, "PeepInfo") && index < 0) {
        if (PeepEqual(f, fn, "StartingCashVarPc") && v >= 0.0 && v < 100.0) t.cashVarPc = static_cast<int32_t>(v);
        if (PeepEqual(f, fn, "WeatherModifierFrequency") && v >= 1.0) t.weatherEvery = static_cast<int32_t>(v);
    } else if (PeepEqual(g, gn, "WeatherEffects") && index < 0) {
        if (PeepEqual(f, fn, "QualityForHeatWaveLow")) t.heatwaveLow = static_cast<int32_t>(v);
    } else if (PeepEqual(g, gn, "PeepTypes") && index >= 0 && index < kTpiMaxPeepTypes) {
        TpiPeepType& type = t.types[index];
        if (index + 1 > t.typeCount) t.typeCount = index + 1;
        const int32_t i = static_cast<int32_t>(v);
        if (PeepEqual(f, fn, "PreferredExcitement")) type.excitement = i;
        else if (PeepEqual(f, fn, "StartingCash")) type.cash = i;
        else if (PeepEqual(f, fn, "BoredomThreshold")) type.boredom = i;
        else if (PeepEqual(f, fn, "Generation")) type.generation = i;
        else if (PeepEqual(f, fn, "SpeedMultiplier")) {
            type.speed = static_cast<float>(v);
            type.set |= 2;
        }
        if (PeepEqual(f, fn, "Generation")) type.set |= 1;
    } else if (index >= 0 && index < 3) {
        float(*table)[4] = PeepEqual(g, gn, "PeepHappinessWeatherEffects") ? t.happiness
                           : PeepEqual(g, gn, "PeepThirstWeatherEffects")  ? t.thirst
                           : PeepEqual(g, gn, "PeepHungerWeatherEffects")  ? t.hunger
                                                                           : nullptr;
        const int32_t kind = PeepWeatherField(f, fn);
        if (table != nullptr && kind >= 0) table[index][kind] = static_cast<float>(v);
    }
}
}  // namespace detail

// Reads `length` bytes of a .sam text (the game's balance file form: "Group.Field value", "Group[i].A.B.C a b c", '#'
// to the end of a line a remark, other lines passed over) into `t`, from nothing. True when it is complete: types 0 to
// typeCount - 1 each with Generation's four integers (one line) and a SpeedMultiplier, a generation of 0 to 2, and each
// generation with one type at least. Otherwise `t` is left not loaded.
inline bool TpiParsePeeps(const char* text, size_t length, TpiPeepTable& t) {
    t = TpiPeepTable{};
    const char* p = text;
    const char* const end = text + length;
    while (p < end) {
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
        // The group, its index and the fields.
        const char* g = name;
        const char* c = name;
        while (c < nameEnd && *c != '.' && *c != '[') ++c;
        const size_t gn = static_cast<size_t>(c - g);
        int32_t index = -1;
        if (c < nameEnd && *c == '[') {
            ++c;
            double v = 0.0;
            if (!detail::PeepNumber(c, nameEnd, v) || c >= nameEnd || *c != ']') continue;
            index = static_cast<int32_t>(v);
            ++c;
        }
        if (gn == 0 || c >= nameEnd || *c != '.') continue;
        const char* fields[8];
        size_t fieldLength[8];
        int32_t count = 0;
        while (c < nameEnd && *c == '.' && count < 8) {
            const char* f = ++c;
            while (c < nameEnd && *c != '.') ++c;
            fields[count] = f;
            fieldLength[count] = static_cast<size_t>(c - f);
            ++count;
        }
        if (c != nameEnd) continue;
        double values[8];
        int32_t got = 0;
        while (got < count) {
            while (q < lineEnd && (detail::PeepSpace(*q) || *q == '\r')) ++q;
            if (!detail::PeepNumber(q, lineEnd, values[got])) break;
            ++got;
        }
        if (got != count) continue;
        for (int32_t k = 0; k < count; ++k) detail::PeepAssign(t, g, gn, index, fields[k], fieldLength[k], values[k]);
    }
    if (t.typeCount == 0) return false;
    for (int32_t i = 0; i < t.typeCount; ++i) {
        const TpiPeepType& type = t.types[i];
        if (type.set != 3 || type.generation < 0 || type.generation > 2) return false;
        t.byGen[type.generation][t.genCount[type.generation]++] = i;
    }
    for (int32_t g = 0; g < 3; ++g)
        if (t.genCount[g] == 0) return false;
    t.loaded = true;
    return true;
}

// The generations' behaviour is in play: one of the mod's worlds with its table read.
inline bool TpiGenerationsInPlay() { return TpiWorldInPlay() && gTpiPeeps.loaded; }

// The new park day's weather step (src/game/World.cpp): in play and on a due day, every visitor's happiness, thirst and
// hunger move by its generation's effect of the weather there is. CParkTime::Update calls it through gTpiNewDay, which
// CWorld::SetupNewLevel sets, so ParkTime.cpp links nothing more than the Mac's (tpw-mods' enable-strikes test builds it
// with its own fakes); nullptr until a level is set up.
void TpiGenerationsNewDay();
inline void (*gTpiNewDay)() = nullptr;

// The type (a row of `t`) of the visitor with thing index `thing` of generation `generation`, or -1 when the table is not
// loaded or the generation is not one: one of the generation's types, each as often as the others, by a spread of the
// thing index unrelated to the one that picks its look (TpiGuestLook).
inline int32_t TpiPeepTypeOf(uint16_t thing, int32_t generation, const TpiPeepTable& t) {
    if (!t.loaded || generation < 0 || generation > 2) return -1;
    const uint32_t spread = TpiMix(TpiMix(0x9e3779b9u ^ thing) ^ 0x2545f491u);
    return t.byGen[generation][spread % static_cast<uint32_t>(t.genCount[generation])];
}

// The generation of a visitor's look (a CPerson) in one of the mod's worlds: the look TpiGuestSpriteOf dresses it in
// (with extra visitors loaded the thing index's, otherwise its sprite number while its set is the kids'), its kind; a
// kid when the look is not known.
inline int32_t TpiGuestGeneration(const void* person) {
    int32_t look = -1;
    if (gTpiExtraGuests[0] > 0) {
        look = TpiGuestLook(TpiThingIndex(person), gTpiLookCount, gTpiLookPick);
    } else {
        const uint8_t* const p = static_cast<const uint8_t*>(person);
        int32_t sprite = 0, set = 0;
        std::memcpy(&sprite, p + 0x20, sizeof sprite);
        std::memcpy(&set, p + 0x24, sizeof set);
        if (set == 0) look = sprite;
    }
    return look >= 0 && look < gTpiLookCount ? gTpiLookKind[look] : kTpiKid;
}

// The Theme Park Inc type of a visitor (a CPerson) in play, or -1 anywhere else.
inline int32_t TpiVisitorType(const void* person) {
    if (!TpiGenerationsInPlay() || person == nullptr) return -1;
    return TpiPeepTypeOf(TpiThingIndex(person), TpiGuestGeneration(person), gTpiPeeps);
}

// The starting cash of a type of cash `base` from the Mac's draw `draw` (CPerson::CPerson's third random number) with a
// spread of `varPc` percent either way: the Mac's own arithmetic (base * (draw mod (2 * varPc + 1) - varPc + 100) / 100,
// toward zero, at least 0).
inline int32_t TpiStartingCash(int32_t base, int32_t draw, int32_t varPc) {
    const int32_t d = varPc * 2 + 1;
    const int32_t rem = draw - (draw / d) * d;
    const int32_t scaled = static_cast<int32_t>(static_cast<uint32_t>(base) * static_cast<uint32_t>(rem - varPc + 100));
    const int32_t hi = static_cast<int32_t>((static_cast<int64_t>(0x51eb851f) * static_cast<int64_t>(scaled)) >> 32);
    const int32_t shifted = hi >> 5;
    const int32_t result = shifted + static_cast<int32_t>(static_cast<uint32_t>(shifted) >> 31);
    return result < 0 ? 0 : result;
}

// The kind of weather the park has: a storm while there is lightning, snow or rain while drops fall (precipitation
// type 1 snow), a heatwave when the quality is at Theme Park Inc's heatwave quality or above, else none.
inline int32_t TpiWeatherKind(int32_t quality, int32_t drops, int32_t precipitation, int32_t lightning, int32_t heatwaveLow) {
    if (lightning != 0) return kTpiStorm;
    if (drops > 0) return precipitation == 1 ? kTpiSnow : kTpiRain;
    if (quality >= heatwaveLow) return kTpiHeatwave;
    return kTpiNoWeather;
}

// The park's day as one count (the days from 1 January of year 0 of the park's calendar: gParkDate's year since 1900,
// month 0 to 11 and day 1 to 31), so that "every N days" needs nothing saved.
inline int32_t TpiParkDayNumber(int32_t year, int32_t month, int32_t day) {
    static const int32_t kBefore[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    if (month < 0 || month > 11) month = 0;
    const int32_t y = year + 1900;
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    const int32_t past = y - 1;
    return past * 365 + past / 4 - past / 100 + past / 400 + kBefore[month] + (leap && month > 1 ? 1 : 0) + day - 1;
}

// Whether the weather's effects are applied on park day `dayNumber`: every `every` days (none for 0).
inline bool TpiWeatherDue(int32_t dayNumber, int32_t every) { return every > 0 && dayNumber % every == 0; }

// The weather's effect on a visitor of generation `generation`: happiness, thirst and hunger each moved by the
// generation's value for `kind`, kept within 0 to 100 (as the game's own changes are, AddClamped).
inline void TpiApplyWeather(float& happiness, float& thirst, float& hunger, int32_t generation, int32_t kind,
                            const TpiPeepTable& t) {
    if (generation < 0 || generation > 2 || kind < 0 || kind > 3) return;
    const auto add = [](float& field, float amount) {
        const float sum = field + amount;
        field = sum > 100.0f ? 100.0f : sum < 0.0f ? 0.0f : sum;
    };
    add(happiness, t.happiness[generation][kind]);
    add(thirst, t.thirst[generation][kind]);
    add(hunger, t.hunger[generation][kind]);
}

}  // namespace tpw

