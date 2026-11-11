// Ps1PickRotation.h: the ps1-rides mod's starting turn of its rides picked from Buy Ride (TPW_MOD_HOOK ps1_pick_rotation).
// A PC ride's footprint has its entrance on its south side, so at the default camera (north at the top) a ride picked
// from Buy Ride comes up with its entrance at the bottom. The PlayStation rides the mod converts have theirs on the
// north side of their footprint (the first line of Info.Shape), so they came up with the entrance at the top. With the
// hook on, picking one of the mod's rides from Buy Ride (PlaceRide, src/game/ui/ui_ridepanel.cpp) adds half a turn to
// the ride's rotation, the turn measured in game that puts its entrance where a PC ride's is. Nothing else changes: the
// converted rides, a saved park and the rides already placed keep their rotation, and the player turns the held ride
// as before. The original keeps the rotation of a ride still held when another is picked; so does the hook, the half
// turn it added for the ride held taken away first, so that picking again does not add it twice. Only with the port's
// fixes: off (the default) or in a build without them, every ride starts as the original's.
#pragma once


#include <cstdint>
#include <string>
#include <vector>

// TPW_PORT_FIX mods gModPs1PickRotation | Mac: no such flag | Port: TPW_MOD_HOOK ps1_pick_rotation, set by the launcher while a tpw-mods mod that switches it on is on (platform/mods.h); set, picking one of that mod's rides from Buy Ride starts it half a turn round (Ps1PickRotation) | Why: the PlayStation rides' entrance is on the north side of their footprint, a PC ride's on the south, so they came up facing the other way
inline bool gModPs1PickRotation = false;

// The rides the hook turns, by RideBadgeKey of their folder (include/RideBadges.h): the content rides of the mod that
// switched the hook on, filled by the launcher with the hook. Empty, no ride is turned.
inline std::vector<std::string> gPs1PickRotationRides;

// The turn the hook added to the ride last picked (0 or kPs1PickTurn), taken away again when another ride is picked
// while that one is still held.
inline int32_t gPs1PickTurn = 0;

// The turn, in degrees, that puts a PlayStation ride's entrance where a PC ride's is at the default camera: two presses
// of "turn clockwise" (90 each), measured in game on the Inca Pot, the Aztec Bounce and the Diplo-Dip.
constexpr int32_t kPs1PickTurn = 180;

// The rotation (0, 90, 180 or 270) a ride picked from Buy Ride starts with: `current` is the rotation before the pick,
// `held` whether a ride was still held then (the cell system in ride placement mode, 4), `lastTurn` the turn the hook
// added to that ride, `ps1` whether the ride picked is one of the hook's. Sets `turn` to the turn added this time.
inline int32_t Ps1PickRotation(int32_t current, bool held, int32_t lastTurn, bool ps1, int32_t& turn) {
    const int32_t base = held ? ((current - lastTurn) % 360 + 360) % 360 : current;
    turn = ps1 ? kPs1PickTurn : 0;
    return (base + turn) % 360;
}

