// TpiBumperKind.h: the TPI Worlds and Rides mod's bumper ride kind (TPW_MOD_HOOK tpi_worlds, LLCR_AddRideToDB).
// A bumper ride's .sam gives its kind (Bumper.BumperType), and BMP_CreateRide copies the ride's record from the
// template of that kind: Theme Park World has 14 of them, kinds -1 to -14 (gRideTemplates). Theme Park Inc's The Scorpions
// gives kind -15, a kind of its own that Theme Park World does not have, so the copy read the 208 bytes past the end of
// the table and the first car launched found no car mesh: the game crashed as soon as the ride took a rider. In the mod's
// worlds a kind outside Theme Park World's range is built as Theme Park World's bumper cars (kind -6): the same record
// as its other bumper cars (-11, -14), the largest arena ring, steered like every arena bumper ride, and the kind Theme
// Park Inc's own Science zone bumper cars give. A park saved before 0.5.2 keeps The Scorpions' id with the stray word's kind;
// loading it, the ride gets the stand-in's kind too (TpiBumperLoadId). Header only and pure, so the host tests see what the
// game does.
#pragma once


// TPW_PORT_FIX mods TpiBumperKind | Mac: no such function; BMP_CreateRide copies the template of any kind the .sam gives | Port: the kind BMP_CreateRide is given in the mod's worlds (TPW_MOD_HOOK tpi_worlds, LLCR_AddRideToDB): one outside Theme Park World's -1 to -14 is built as -6; and the id such a ride saved in a park is loaded as (TpiBumperLoadId: BMP_Load's chunks and CSaveLoad::PostLoadOperations), the stand-in's kind with its record's index | Why: tpw-mods tpi-worlds; Theme Park Inc's The Scorpions gives kind -15 and the copy read past gRideTemplates, so the game crashed as the ride took a rider; a park saved with it before 0.5.2 keeps an id whose kind is the word past the table, and loading it rebuilt the record from far outside the table and crashed

#include <cstdint>

namespace tpw {

// Theme Park World's bumper ride kinds run from -1 to -14 (the 14 rows of gRideTemplates).
inline constexpr int32_t kBumperKindFirst = -1;
inline constexpr int32_t kBumperKindLast = -14;
// The kind a ride outside that range is built as: Theme Park World's Halloween bumper cars.
inline constexpr int32_t kBumperKindStandIn = -6;

// The kind BMP_CreateRide is given for a ride whose .sam gives `kind` (0: not a bumper ride, left as it is).
inline int32_t TpiBumperKind(int32_t kind) {
    if (kind == 0 || (kind <= kBumperKindFirst && kind >= kBumperKindLast)) return kind;
    return kBumperKindStandIn;
}

// The id a bumper ride saved in one of the mod's worlds is loaded as: one whose kind is outside Theme Park World's -1 to -14
// (a park saved with The Scorpions before 0.5.2) gets the stand-in's kind and keeps its record's index.
// A ride's id is its kind shifted by 8 or'd with its record's index (0 to 63); 0 is no ride.
inline uint32_t TpiBumperLoadId(uint32_t id) {
    const int32_t kind = static_cast<int32_t>(id) >> 8;
    if (TpiBumperKind(kind) == kind) return id;
    return (static_cast<uint32_t>(kBumperKindStandIn) << 8) | (id & 0xffu);
}

}  // namespace tpw

