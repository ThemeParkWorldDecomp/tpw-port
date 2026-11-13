// TpiRideScript.h: the TPI Worlds and Rides mod's start of a ride script (TPW_MOD_HOOK tpi_worlds, CObject::CreateRideScript).
// Theme Park Inc's Golden Ticket rides (Spin Djinn, The Scorpions and others in all three zones) have a ride script that
// loops the ride's O animation and waits for the script variable SPECIAL (number 10) before it plays the C animation and
// starts taking riders. Theme Park World's engine never sets that variable (no Theme Park World ride script waits on it),
// so placed, such a ride stays in its O pose for ever: the parts the O animation hides stay hidden, the first visitor let
// on waits for ever and the queue only grows. Theme Park World's own Golden Ticket rides work as soon as they are placed,
// so in the mod's worlds a ride whose animation set has an O group starts with SPECIAL set. Theme Park Inc's litter bins
// read SPECIAL for something else and have no O group, so they are left as they are. Header only and pure, so the host
// test sees what the game does.
#pragma once


// TPW_PORT_FIX mods TpiRideScriptStartsSpecial | Mac: no such function; no ride script starts with SPECIAL set | Port: in the mod's worlds (TPW_MOD_HOOK tpi_worlds, CObject::CreateRideScript) a ride whose animation set has an O group starts with SPECIAL set | Why: tpw-mods tpi-worlds; Theme Park Inc's Golden Ticket rides wait on SPECIAL, which Theme Park World never sets, so placed they stayed in their O pose for ever

#include <cstdint>

namespace tpw {

// The ride script variable a Theme Park Inc Golden Ticket ride waits on (SPECIAL, the eleventh name of a script's table).
inline constexpr int32_t kRideScriptVarSpecial = 10;
// The O animation's group in a ride's animation set (the letters CDILSMEUWBRO of src/game/rides/TPRide.cpp, from 0).
inline constexpr int32_t kRideAnimKindO = 11;

// Whether a ride script starts with SPECIAL set: its ride's O group has an animation (the group's flags & 3, as the game
// tests a group) and the script has a variable SPECIAL.
inline bool TpiRideScriptStartsSpecial(uint32_t oGroupFlags, int32_t varCount) {
    return (oGroupFlags & 3u) != 0 && varCount > kRideScriptVarSpecial;
}

}  // namespace tpw

