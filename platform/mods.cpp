// mods.cpp: the hooks the mods that are on switch on, put into the game (mods.h).
#include "mods.h"

#include "RideBadges.h"
#include "ThingReceivers.h"

namespace tpw::mods {

void Apply(const Hooks& hooks) {
    gModStrikeGate = hooks.strikeGate;
    gModStrikeMessages = hooks.strikeMessages;
    gModStaffBridgeWander = hooks.staffBridgeWander;
    gModPs1Badge = hooks.ps1Badge;
    gModPs1Ground = hooks.ps1Ground;
}

}  // namespace tpw::mods
