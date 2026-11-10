// mods.h: the tpw-mods mods compiled into the launcher (the build's mods folder, TPW_MODS_DIR: tpw-mods next to this
// repository, mods/ in tpw-port; platform/mods.cmake). Each mod is a folder of tpw-mods with a mod.json, read at
// configure time into the catalog below, and a function that switches on the hooks it uses. A hook is a small point in
// the game's code, behind TPW_PORT_FIX with a "TPW_MOD_HOOK <name>" comment, that does nothing unless a mod switches it
// on; with every mod off (the default: launcher.ini's mod.<id> key absent or "0") the game is the original. Built only
// with TPW_PORT_FIX: a build without the port's fixes has no mods. launcher/main.cpp switches the mods that are on
// before the game starts.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tpw::mods {

// The editions a mod supports (mod.json's "editions": "US", "European", "Gold").
enum Edition : uint8_t {
    kUs = 1,
    kEuropean = 2,
    kGold = 4,
};

// What the mods that are on switch on, one field a hook (its TPW_MOD_HOOK name). All off: the original game.
struct Hooks {
    bool strikeGate = false;          // strike_gate: CStaffHQ::UpdateMonthly considers strikes in an open park with visitors in it
    bool strikeMessages = false;      // strike_messages: the advisor's "last warning" a week before a strike and "strike over" at its end
    bool staffBridgeWander = false;   // staff_bridge_wander: a staff member's random walk (CPeep::SetRandomDestination) may cross a footbridge
    bool ps1Badge = false;            // ps1_badge: the PlayStation rides have the "PS1" tag on the Buy Ride panel (include/RideBadges.h), while "Show Ride Badges" is on
    bool ps1Ground = false;           // ps1_ground: a PlayStation ride's own ground is blended into the park's ground cell by cell (CalculateSkirtVertices)
    bool ps1SaveRepair = false;       // ps1_save_repair: a park's PlayStation rides whose saved doors are not their record's have them moved as it loads (include/Ps1SaveRepair.h)
};

// A compiled-in mod, as its mod.json describes it.
struct Mod {
    std::string id;        // its folder's name and launcher.ini's mod.<id> key: enable-strikes
    std::string name;      // Enable strikes
    std::string version;   // 1.0.0
    std::string author;
    std::string license;
    std::string text;      // what it does, for the player
    uint8_t editions = 0;  // Edition bits
    std::vector<std::string> conflicts;   // ids of mods it cannot be on with
    bool changesSaves = false;            // a saved game made with it on differs from the original's
    std::vector<std::string> hooks;       // the TPW_MOD_HOOK names it switches on
    void (*switchOn)(Hooks& hooks) = nullptr;   // its own code: sets the fields of its hooks
    std::string content;   // its content's folder in the data folder's assets (mod.json's "content", optional): ps1.
                           // The launcher lays it over the game folder (platform/content_overlay.h) with the mod on,
                           // and keeps the mod locked until the folder holds it
};

// The mods of this build, in their folders' order (mods.cmake's TPW_MODS); empty when the build found no mods folder.
// Generated at configure time (mods_catalog.cpp in the build folder, from platform/mods.cmake).
const std::vector<Mod>& Catalog();

// launcher.ini's key of a mod: "1" on, absent or "0" off.
inline std::string Key(const Mod& mod) { return "mod." + mod.id; }

// Puts `hooks` into the game (the hooks' flags, include/ThingReceivers.h's gModStrikeGate, gModStrikeMessages and
// gModStaffBridgeWander and gModPs1Ground, and include/RideBadges.h's gModPs1Badge). Before the game starts, once; never called, the
// game's hooks stay off.
void Apply(const Hooks& hooks);

}  // namespace tpw::mods
