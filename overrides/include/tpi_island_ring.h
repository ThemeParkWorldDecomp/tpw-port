// tpi_island_ring.h: the island screen's second ring for the TPI Worlds and Rides mod (tpw-mods tpi-worlds; TPW_MOD_HOOK
// tpi_islands, include/TpiWorlds.h). The lobby's description (lobby.txt) has ten camera places, and the original four islands
// stand at the first four, a square of side 200 round (500, 500). With the mod on, the three worlds of platform/tpi_content.h
// (Arabian Nights, Polar Zone, Land of Invention) stand in a row 200 apart at z 930, north of the first square: inside the
// lobby's sea (lobby.wad's terrain/Base.MD2, a disk at least 523 in radius round (550.5, 553.1); each island's centre is 96 or
// more inside its edge) and out of the first ring's fog (the lobby's fog ends 250 away, and the nearest ring-two island is
// 263 from the farthest place the browsing camera reaches in ring one), as list entries 4, 5 and 6. Tab (and a right click on
// the view) flies the camera between the rings; the left and right arrows and the panel's buttons browse the ring the camera
// is in.
// The functions here are the pure part (which ring a world is in, which island comes next, where the camera lands, the way it
// flies there), so a
// host test (tests/host_tpi_island_ring.cpp) can run them without the game; src/game/online/lobby_island.cpp and
// lobby_parser.cpp apply them to the screen's list. Only with the port's fixes (the original has one ring and no Tab).
#pragma once

// TPW_PORT_FIX mods tpi_island_ring | Mac: one ring of four islands, no such header | Port: TPW_MOD_HOOK tpi_islands (gModTpiIslands, include/TpiWorlds.h): the pure ring functions (which ring a world is in, the next island in a ring, where the camera lands, Tab's flight over the islands) that src/game/online/lobby_island.cpp and lobby_parser.cpp apply with the mod on | Why: tpw-mods tpi-worlds: the mod's three worlds form a second ring of islands | Off: nothing, the header is empty

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "TpiWorlds.h"

namespace tpw {

// The mod's worlds by level folder, in the order the second ring shows them: the same three, in the same order, as
// kTpiWorlds (include/TpiWorlds.h, the one list of the mod's worlds; the host test checks the two agree entry for entry).
static_assert(sizeof(kTpiWorlds) / sizeof(kTpiWorlds[0]) == 3, "the second ring is laid out for three worlds");
inline constexpr const char* kTpiIslandWorlds[] = {kTpiWorlds[0].folder, kTpiWorlds[1].folder, kTpiWorlds[2].folder};

// Ring one's places are the lobby description's 0..3; the mod's worlds take 4.. in kTpiIslandWorlds order (list order too).
inline constexpr int32_t kTpiRingOneSlots = 4;
inline constexpr size_t kTpiWorldCount = sizeof(kTpiIslandWorlds) / sizeof(kTpiIslandWorlds[0]);

// Where ring two's islands stand (x, z), by kTpiIslandWorlds index: one row inside the sea disk (no fourth place fits in the
// sea north of the first ring's fog; the first ring's square moved 600 north put its far corners past the sea's edge).
struct TpiRingPlace {
    float x;
    float z;
};
inline constexpr TpiRingPlace kTpiRingTwoPlaces[3] = {{350.0f, 930.0f}, {550.0f, 930.0f}, {750.0f, 930.0f}};
static_assert(kTpiWorldCount <= sizeof(kTpiRingTwoPlaces) / sizeof(kTpiRingTwoPlaces[0]), "a place for every world");
static_assert(kTpiRingOneSlots + static_cast<int32_t>(kTpiWorldCount) <= 10, "the lobby description has ten camera places");

// The most islands the ring logic looks at (the list is seven with the mod on; the lobby description has ten places).
inline constexpr size_t kTpiIslandMax = 16;

// The hint under the screen's top, in the game's own small font, with the mod on and ring two present: which ring Tab goes to.
inline constexpr char16_t kTpiRingHintToTwo[] = u"Tab: Arabian Nights, Polar Zone, Land of Invention";
inline constexpr char16_t kTpiRingHintToOne[] = u"Tab: the original islands";

// The index of the world whose level folder is `folder` in kTpiIslandWorlds (letters in either case, as the game compares
// theme names), or -1 for anything else (an original world, an empty or missing name).
inline int32_t TpiWorldIndex(const char* folder) {
    if (folder == nullptr || *folder == '\0') return -1;
    for (size_t i = 0; i < kTpiWorldCount; ++i) {
        const char* a = folder;
        const char* b = kTpiIslandWorlds[i];
        while (*a != '\0' && *b != '\0') {
            char ca = *a, cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
            if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
            if (ca != cb) break;
            ++a;
            ++b;
        }
        if (*a == '\0' && *b == '\0') return static_cast<int32_t>(i);
    }
    return -1;
}

// The ring an island of world `folder` is in: 1 for the mod's worlds, 0 for the original four (and anything else).
inline int32_t TpiIslandRing(const char* folder) { return TpiWorldIndex(folder) >= 0 ? 1 : 0; }

// The island after (forwards) or before the one at `current` in the same ring, wrapping round the list of `count` islands
// whose rings are `rings`: the arrows browse one ring. The current one when it is alone in its ring.
inline size_t TpiNextInRing(const int32_t* rings, size_t count, size_t current, bool forwards) {
    if (count == 0 || current >= count) return current;
    size_t at = current;
    for (size_t step = 0; step < count; ++step) {
        at = forwards ? (at + 1 == count ? 0 : at + 1) : (at == 0 ? count - 1 : at - 1);
        if (rings[at] == rings[current]) return at;
    }
    return current;
}

// Where Tab lands in ring `target`: the island last looked at there (`remembered`, an index, or -1 for none) if it is still
// an island of that ring, else the ring's first island in list order; -1 when the ring has no island (then Tab does nothing).
inline long TpiRingLanding(const int32_t* rings, size_t count, int32_t target, long remembered) {
    if (remembered >= 0 && static_cast<size_t>(remembered) < count && rings[remembered] == target) return remembered;
    for (size_t i = 0; i < count; ++i)
        if (rings[i] == target) return static_cast<long>(i);
    return -1;
}

// Tab's flight between the rings. The browsing camera chases a point circling the current island (70 out, 20 up) at a tenth of
// the way each tick unit, so a jump to another island is a straight glide from where the camera is to that island's circle:
// over 500 across from ring two to ring one it went low through the islands between (and through the landing island, coming
// in on its far side). Tab's flight instead goes up from where the camera is to kTpiFlightLift over the higher of the two
// heights, across at that height to above the landing goal, then down to it; the landing goal is on the near side of the
// target island (the circle's angle turned to face the camera), so the descent is outside the island. Each waypoint is let go
// once the camera is within kTpiFlightReach of it. Every island stands under 60 (a mesh's top over the sea) inside 50 of its
// place, and the host test flies every pair of the two rings at every start angle and frame time over all of them.
struct TpiPoint {
    float x;
    float y;
    float z;
};
inline constexpr float kTpiFlightLift = 60.0f;
inline constexpr float kTpiFlightReach = 25.0f;

// The circle's angle (as the lobby's: sine to x, cosine to z) that puts the camera on the side of the island at (`x`, `z`)
// that faces `camera`.
inline float TpiFacingAngle(const TpiPoint& camera, float x, float z) {
    return static_cast<float>(std::atan2(static_cast<double>(camera.x - x), static_cast<double>(camera.z - z)));
}

// The flight's waypoints still ahead (way[0] first), none once it is over.
struct TpiFlight {
    TpiPoint way[2];
    int32_t count;
};

// The flight from `camera` to the landing goal `goal`: straight up, then across to above the goal.
inline TpiFlight TpiPlanFlight(const TpiPoint& camera, const TpiPoint& goal) {
    const float top = (camera.y > goal.y ? camera.y : goal.y) + kTpiFlightLift;
    return TpiFlight{{{camera.x, top, camera.z}, {goal.x, top, goal.z}}, 2};
}

// The point the camera makes for this tick: the next waypoint, once those it has come within kTpiFlightReach of are let go;
// null when the flight is over (then the camera makes for its goal round the island as ever).
inline const TpiPoint* TpiFlightGoal(TpiFlight& flight, const TpiPoint& camera) {
    while (flight.count > 0) {
        const float dx = flight.way[0].x - camera.x;
        const float dy = flight.way[0].y - camera.y;
        const float dz = flight.way[0].z - camera.z;
        if (dx * dx + dy * dy + dz * dz >= kTpiFlightReach * kTpiFlightReach) break;
        flight.way[0] = flight.way[1];
        --flight.count;
    }
    return flight.count > 0 ? &flight.way[0] : nullptr;
}

}  // namespace tpw

