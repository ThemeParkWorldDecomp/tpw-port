// TpiOutside.h: the TPI Worlds and Rides mod's outside features (TPW_MOD_HOOK tpi_worlds). Theme Park Inc's scenery round
// each park (the waterfall, water wheel and ponds of the "water" item, the ticket booths, the helipad, the bunting and
// each world's installations, penguins or seals) are items of their own, flagged Info.IsOutsideFeature 1, shown in no
// build menu (Info.WhichUIType 4) and modelled where they stand (Info.DontApplyOffset 1), which Theme Park Inc placed when
// a park started. Theme Park World places only its gates and lights that way (CWorld::SetupNewLevel), so the converted
// worlds lacked them: no decor outside the park, and the ponds of the level's ground showed the clear colour (flat cyan)
// with no water item over them.
// The converter (assets/tpi_convert.cpp, TpiOutsideSam) writes the converted ids of a world's outside features into the
// world as TpiOutside.sam, one "Outside <id>" line each; CWorld::SetupNewLevel reads it (TpiParseOutside) into
// gTpiOutside and places each one as it places the gates (a feature object on map cell 1). A park keeps its objects, so
// one saved before converter 28 has none: on loading, each outside feature the park does not have is placed then
// (TpiOutsideToPlace, RepairTpiOutsideFeatures in src/game/save/SaveLoad.cpp), and a second load changes nothing.
// Header only and pure, so the host test sees what the game does.
#pragma once


// TPW_PORT_FIX mods TpiOutside | Mac: no such header; Theme Park World places only its gates and lights | Port: the outside features' file, its reader and which of them a loaded park lacks, for the mod's worlds (TPW_MOD_HOOK tpi_worlds) | Why: tpw-mods tpi-worlds; Theme Park Inc's scenery round the park (waterfall, water wheel, ponds, ticket booths) was never placed (Addable13, Discord, 0.5.6)

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace tpw {

// The file the converter writes into each of the mod's worlds that has outside features (data/levels/<world>/TpiOutside.sam).
inline constexpr char kTpiOutsideFile[] = "TpiOutside.sam";
inline constexpr int32_t kTpiMaxOutside = 16;   // a world's (Theme Park Inc's have 6)

struct TpiOutsideTable {
    int32_t count = 0;
    uint16_t ids[kTpiMaxOutside] = {};   // converted Info.Id, in the file's order, each once
};

// The outside features of the world being played (TPW_MOD_HOOK tpi_worlds, gModTpiWorlds): CWorld::SetupNewLevel reads
// them from the world's TpiOutside.sam in one of the mod's worlds (TpiParseOutside); empty in any other.
inline TpiOutsideTable gTpiOutside;

// Reads a TpiOutside.sam: each "Outside <id>" line (a '#' starts a comment) adds the id, 1 to 65534, once; other lines,
// bad ids, repeats and ids past kTpiMaxOutside are skipped. True when at least one id was read.
inline bool TpiParseOutside(const char* text, size_t size, TpiOutsideTable& table) {
    table = TpiOutsideTable{};
    const char* at = text;
    const char* end = text + size;
    while (text != nullptr && at < end) {
        const char* line = at;
        while (at < end && *at != '\n') ++at;
        const char* lineEnd = at;
        if (at < end) ++at;
        for (const char* c = line; c < lineEnd; ++c)
            if (*c == '#') {
                lineEnd = c;
                break;
            }
        const char* p = line;
        while (p < lineEnd && (*p == ' ' || *p == '\t')) ++p;
        static constexpr char kWord[] = "Outside";
        const size_t n = sizeof kWord - 1;
        if (static_cast<size_t>(lineEnd - p) <= n || std::strncmp(p, kWord, n) != 0 || (p[n] != ' ' && p[n] != '\t'))
            continue;
        p += n;
        while (p < lineEnd && (*p == ' ' || *p == '\t')) ++p;
        char buf[16];
        int32_t len = 0;
        while (p < lineEnd && len < 15 && *p != ' ' && *p != '\t' && *p != '\r') buf[len++] = *p++;
        buf[len] = 0;
        while (p < lineEnd && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
        if (len == 0 || p != lineEnd) continue;
        char* stop = nullptr;
        const long v = std::strtol(buf, &stop, 10);
        if (*stop != 0 || v < 1 || v > 65534 || table.count >= kTpiMaxOutside) continue;
        bool seen = false;
        for (int32_t i = 0; i < table.count; ++i) seen = seen || table.ids[i] == static_cast<uint16_t>(v);
        if (!seen) table.ids[table.count++] = static_cast<uint16_t>(v);
    }
    return table.count > 0;
}

// Which of the world's outside features a loaded park lacks: each id of `table` that no placed object's type
// (`placed`, `placedCount`, CObject::mObjectType) is, in the table's order, into `out` (room for kTpiMaxOutside); the count.
inline int32_t TpiOutsideToPlace(const TpiOutsideTable& table, const uint16_t* placed, int32_t placedCount, uint16_t* out) {
    int32_t count = 0;
    for (int32_t i = 0; i < table.count; ++i) {
        bool there = false;
        for (int32_t j = 0; j < placedCount && !there; ++j) there = placed[j] == table.ids[i];
        if (!there) out[count++] = table.ids[i];
    }
    return count;
}

// Places each of `ids` (converted outside features' ids) as CWorld::SetupNewLevel places the gates: a feature object on map
// cell 1. An id the ride database has not got is skipped. The count placed. Defined in src/game/World.cpp.
int32_t TpiPlaceOutside(const uint16_t* ids, int32_t count);

}  // namespace tpw

