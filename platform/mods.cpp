// mods.cpp: the hooks the mods that are on switch on, put into the game (mods.h).
#include "mods.h"

#include "Ps1PickRotation.h"
#include "Ps1SaveRepair.h"
#include "RideBadges.h"
#include "ThingReceivers.h"

namespace tpw::mods {

void Apply(const Hooks& hooks) {
    gModStrikeGate = hooks.strikeGate;
    gModStrikeMessages = hooks.strikeMessages;
    gModStaffBridgeWander = hooks.staffBridgeWander;
    gModPs1Badge = hooks.ps1Badge;
    gModPs1Ground = hooks.ps1Ground;
    gModPs1SaveRepair = hooks.ps1SaveRepair;
    gModPs1PickRotation = hooks.ps1PickRotation;
}

}  // namespace tpw::mods
