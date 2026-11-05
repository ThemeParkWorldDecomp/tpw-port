// mods.cpp: the hooks the mods that are on switch on, put into the game (mods.h).
#include "mods.h"

#include "ThingReceivers.h"

namespace tpw::mods {

void Apply(const Hooks& hooks) {
    gModStrikeGate = hooks.strikeGate;
    gModStrikeMessages = hooks.strikeMessages;
    gModStaffBridgeWander = hooks.staffBridgeWander;
}

}  // namespace tpw::mods
