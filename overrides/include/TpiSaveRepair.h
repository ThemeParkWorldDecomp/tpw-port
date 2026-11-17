// TpiSaveRepair.h: the TPI Worlds and Rides mod's repairs of parks saved before a converter showed more of a world's ground
// (TPW_MOD_HOOK tpi_worlds). A park file keeps every map cell's mode (CCellBase::Serialise), and a cell whose height field
// quad was hidden when the park started was given the hidden quad's mode by CMapCell::Reset (2, 0x1e or 7, from the
// attribute byte): no land to buy and no path. Converter 14 shows the quads of the bridges' walkways and ramps as land
// (the walkable bridges) and those of Land of Invention's gate, so a new park gets them as ordinary land, but a park saved
// with converter 12 still holds the hidden quad's mode on them. Loading such a park draws each cell
// again from its saved mode, and a mode 7 cell gets the plain tile, which hides its quad once more (SetHeightfieldTexture),
// so the bridges stay hidden ground that no path can cross. On loading a park in one of the mod's worlds, each map cell
// that still has the hidden quad's mode of its attribute, belongs to nothing and is not a path edge, while the level's own
// quad (gHeightfieldCopy, kept as the level was read) is shown, gets that quad back and is reset as a new park resets it
// (CMapCell::Reset), keeping whether it was inside the park (flag 0x40); its track cell is reset too when it has the hidden
// quad's mode 7. A cell the park has built on never matches, and a second load changes nothing.
// Converter 16 makes the walkways footbridge cells like Lost Kingdom's instead (their quads hidden again, base.map
// attribute 0x80 alone: CMapCell::Reset's mode 0x1e, which guests cross on their way to something and nothing is built
// on), since a path built under the deck is hidden by it and parks were left with gaps no guest could cross (the maintainer's Land
// of Invention park, 0.5.3: no path on the two cells over mid-river), and the shown quads fought the deck. A park keeps
// the attribute byte and mode of every cell it saved, so on loading, each walkway cell (the level's attribute 0x80 alone
// over a quad the level hides) that belongs to nothing and holds land, a path or a hidden quad's mode 2, 5 or 7 is made a
// footbridge as a new park makes it: a path loses its links both ways first (CMapCell::Delete), the quad is the level's
// again and the cell takes the level's attribute and is reset (mode 0x1e), keeping its saved flag 0x40
// (TpiRepairedFlags), with its track cell when that belongs to nothing (mode 7). A cell something is built on stays as
// it is, and a second load changes nothing.
// A hoarding post (track type 0x19) belongs to nothing too, and 0.5.5 reset the posts of a fence that crossed a walkway
// with the rest (a player's Arabian Nights park: the old east fence over the tunnel), leaving a hole the next land bought walked to
// and never left (LLCC_BuildHoardingDB), and block cells naming a post that was gone. The walkway repair now keeps posts
// (TpiWalkwayTrackToReset), and on loading, each post a park saved after such a reset (its footbridge cell's track
// cell parentless and no post, its three block cells still naming it) is a post again, linked to its neighbours
// (RepairTpiLostHoardingPosts); a second load changes nothing.
// The same mod's repair of the shops of parks saved before converter 15: a park keeps each object type's control
// (CControlManager, made once from the record when the type is first seen), so its shops keep the RipOffOK converter 14
// gave them (0 where the shops folder named none) and visitors still walk away from their prices; on loading, a shop
// type's control whose RipOffOK is below its record's gets the record's. And a photo booth placed before converter 15
// keeps the object flags its record gave it then (CObject::mFlags, saved): without flag 4 (Info.IsChoosable) no visitor
// chooses it, and the state it was built in (kStateThree, which ModelState skips); a placed shop whose record now says
// Info.IsChoosable gets flag 4, and state 3 becomes the normal state a new booth gets.
// And the same mod's repair of the research of parks saved before converter 14: converter 14 added Land of Invention's
// staff room (features/nstaff) in the id its security camera had (7730), moving the camera to the next one (7740). A
// park keeps each type's control (mAvailable, made from the record when the type is first seen) and the research
// projects its lab made when it started (CResearchLab::SetupProjects, new parks only), so in a park started before it
// the staff room keeps the camera's control (unresearched, mAvailable 0, so no build list shows it, and the camera's
// price, CostOfUpgrade) and the camera's project, while the camera's new id has no project and can never be researched.
// On loading, the control of a type whose record has no research cost, saved unavailable or named by a stale project
// (both mean it was made from another record; a statistics upgrade, upgrade level 1 or more, is not stale), is made again from its record as CheckAllControlsInitialised makes it,
// available as SetupProject makes it in a new park, keeping the park's count and first build turn; a project (not a
// statistics upgrade) whose type's record now has no research cost goes to the type it was made for (of its category,
// with its score as research cost and no project of its own), keeping its progress, and a finished one makes that type
// available; and each type with a research cost, no project and no availability gets a new one. A camera such a park
// placed is saved under 7730 (CObject::Serialise's mId) with the camera's object flags (0x10, ProvidesSecurity, and not
// the staff room's 2), so it loads as a staff room's building that is no staff room; before the engine objects are
// restored, a placed object of a type whose project goes to another type is given that type when its saved flags say
// what that type's record makes and not what its saved type's record now makes, its building (the ride instance the save
// kept, which is the staff room's mesh) made again as that type's, and the park's counts of the two types
// (ObjectControl::mNumberInPark) are those of their placed objects.
// And the same mod's repair of the rides placed before 0.5.2: a park keeps each placed ride's script whole (RSSE_Save, its
// variables with it), so a Golden Ticket ride placed before CObject::CreateRideScript set SPECIAL (include/TpiRideScript.h)
// still loops its O animation waiting on it: parts hidden, no riders, the queue only growing. On loading, a placed ride
// that would start with SPECIAL set (tpw::TpiRideScriptStartsSpecial) but whose saved SPECIAL is 0 gets it, as at
// placement; the script reads it on its next tick and carries on as a new ride does. A ride placed since already has it.
// And the same mod's repair of the special rides of parks saved before converter 28: Theme Park Inc's special rides
// (Info.IsSpecial: "they can't be rebuilt") never wear out, and converter 28 gives them Theme Park World's no wear, a wear
// rate of 0 at every upgrade level (CObject::AddWearAndTear takes nothing off then), but a park keeps each ride's remaining
// life (CObject::mF44) as it was worn, and a ride whose life is worn to nothing breaks down after every repair until it is
// condemned. On loading, a placed ride that no longer wears (its record's wear rate at its upgrade level is 0), is not
// condemned and has less than all its life gets all of it back, as a new ride has it; a second load changes nothing.
// Header only and pure, so the host test sees the rule the game uses (src/game/save/SaveLoad.cpp).
#pragma once


// TPW_PORT_FIX mods TpiSaveRepair | Mac: no such header; Theme Park World has no converted worlds | Port: which map and track cells of a park saved before converter 14 RepairTpiHiddenCells resets as land on loading in the mod's worlds (TPW_MOD_HOOK tpi_worlds), which shop controls and placed shops of a park saved before converter 15 RepairTpiShops repairs, which walkway cells of a park saved before converter 16 RepairTpiWalkways makes footbridge cells, which hoarding posts on them it keeps and which lost ones RepairTpiLostHoardingPosts restores, which controls, research projects and placed objects of a park saved before converter 14 RepairTpiPlacedTypes and RepairTpiResearch repair, which placed rides' scripts RepairTpiRideScripts starts, and which placed rides that no longer wear RepairTpiSpecialRides gives their life back | Why: tpw-mods tpi-worlds; parks saved with 0.5.1 kept Land of Invention's walkable bridges hidden, so no path could cross them, parks saved before 0.5.3 kept shop prices visitors walked away from and an unchoosable photo booth, parks saved before 0.5.4 kept gaps in the paths under the bridges' decks that the decks hid, and quads that fought the decks, parks saved with 0.5.5 lost the fence's posts over the walkways, so buying land froze the game, parks started before converter 14 never showed Land of Invention's staff room and turned their placed security cameras into staff room buildings, Golden Ticket rides placed before 0.5.2 kept waiting on SPECIAL, their parts hidden and no riders, and special rides worn before 0.5.6 kept their worn life though they no longer wear

#include <cstdint>
#include <vector>

#include "TpiRideScript.h"

namespace tpw {

// The mode CMapCell::Reset gives a map cell whose height field quad is hidden (HFieldCellStatus 0), from its attribute
// byte: 2 for bit 2, else 0x1e for bit 0x80, else 7.
inline int32_t TpiHiddenQuadMode(uint8_t attribute) {
    if ((attribute & 2) != 0) return 2;
    if ((attribute & 0x80) != 0) return 0x1e;
    return 7;
}

// A map cell (its mode, attribute byte, parent and flags) that a park saved while its quad was hidden keeps, and whose
// quad the level now shows (`levelShows`: the level's own quad, in gHeightfieldCopy, has bit 1 clear): the hidden quad's
// mode of its attribute, no parent and not a path edge (flag 0x20).
inline bool TpiStaleHiddenMapCell(int32_t mode, uint8_t attribute, uint16_t parent, uint16_t flags, bool levelShows) {
    return levelShows && parent == 0 && (flags & 0x20) == 0 && mode == TpiHiddenQuadMode(attribute);
}

// The flags a reset cell keeps: CMapCell::Reset's (`reset`), with the saved cell's flag 0x40 (outside the park), so land
// the park owned stays its own; a start path edge (Reset's 0x20, which gets no 0x40) keeps Reset's flags.
inline uint16_t TpiRepairedFlags(uint16_t saved, uint16_t reset) {
    if ((reset & 0x20u) != 0) return reset;
    return static_cast<uint16_t>((reset & ~0x40u) | (saved & 0x40u));
}

// The same for a track cell (`shown`: its quad is shown once the map cell's is back): CTrackCell::Reset gives 7 to a
// hidden quad and 0 to a shown one.
inline bool TpiStaleHiddenTrackCell(int32_t mode, uint16_t parent, bool shown) { return shown && parent == 0 && mode == 7; }

// A map cell of a bridge's or tunnel's walkway that a park saved before converter 16 or 17 keeps as something other than
// a footbridge: the level's attribute byte (`level`, as base.map has it now) is 0x80 alone, which converter 16 gives
// the bridges' walkways and converter 17 the tunnels', over a quad the level hides (`levelHides`: gHeightfieldCopy's
// bit 1), and the cell belongs to nothing and holds land (0, converters 14 and 15), a path (1, built there or the start
// path), or a hidden quad's mode of the attribute it saved (2 or 7, converter 12; 5, shown land of attribute 1). A
// footbridge (0x1e) and anything built there are kept.
inline bool TpiWalkwayToFootbridge(int32_t mode, uint8_t level, uint16_t parent, bool levelHides) {
    return levelHides && level == 0x80 && parent == 0 && (mode == 0 || mode == 1 || mode == 2 || mode == 5 || mode == 7);
}

// Whether RepairTpiWalkways resets the track cell of a walkway cell it makes a footbridge: one that belongs to nothing
// (parent 0) and is not the hidden quad's mode 7 already, as before, but never a hoarding post (type 0x19). A post has
// parent 0 too, and a reset post (mode 7, no hoarding links) left a hole in the park's fence where it crossed a
// tunnel's or bridge's walkway: the next land bought walked the fence (LLCC_BuildHoardingDB) to the hole and stood
// there forever (a player's Arabian Nights park on 0.5.5, the old east fence over the tunnel at (65,65)), and its three block
// cells (type 0xc) kept naming it, which the context help read as a track cell (a player's Polar Zone park, (49,41)).
inline bool TpiWalkwayTrackToReset(int32_t type, uint16_t parent) { return parent == 0 && type != 7 && type != 0x19; }

// One track cell as RepairTpiLostHoardingPosts sees it: its mode, parent, hoarding links (mHoardingNeighbours, the
// bits 1 up, 4 right, 0x10 down, 0x40 left), heading (mDirection), flags, and whether its map cell is a footbridge
// (mode 0x1e). The grid is 128 x 128, cell x + (y << 7), id 1 + x + (y << 7), as the world's.
struct TpiHoardingCell {
    int32_t type;
    uint16_t parent;
    uint8_t hoarding;
    uint8_t direction;
    uint16_t flags;
    int16_t overlap;
    bool footbridge;
};

constexpr int32_t kTpiHoardingGrid = 128;

// The opposite of one of the four headings (OPPOSITE in the cell system).
inline uint8_t TpiOppositeHeading(uint8_t heading) {
    switch (heading) {
        case 1: return 0x10;
        case 4: return 0x40;
        case 0x10: return 1;
        case 0x40: return 4;
        default: return 0;
    }
}

// The cell two cells away along a heading (the hoarding's step), or -1 outside the `width` x `height` level.
inline int32_t TpiHoardingStep(int32_t index, uint8_t heading, int32_t width, int32_t height) {
    int32_t x = index & 0x7f, y = index >> 7;
    switch (heading) {
        case 1: y -= 2; break;
        case 4: x += 2; break;
        case 0x10: y += 2; break;
        case 0x40: x -= 2; break;
        default: return -1;
    }
    if (x < 0 || y < 0 || x >= width || y >= height) return -1;
    return x + (y << 7);
}

// A hoarding post a park saved after a 0.5.5 load reset it (TpiWalkwayTrackToReset): its map cell is a footbridge, its
// track cell belongs to nothing and is no post, and the three cells of its block (+x, +y, +x +y), which
// LLCC_BuildHoardingDB gives every post it lays (type 0xc, parent the post's id), still name it. A reset keeps them
// (their parent is not 0), and nothing else leaves a parentless cell with such a block.
inline bool TpiLostHoardingPost(const TpiHoardingCell* cells, int32_t index, int32_t width, int32_t height) {
    const int32_t x = index & 0x7f, y = index >> 7;
    if (x + 1 >= width || y + 1 >= height) return false;
    const TpiHoardingCell& cell = cells[index];
    if (!cell.footbridge || cell.parent != 0 || cell.type == 0x19) return false;
    const uint16_t id = static_cast<uint16_t>(index + 1);
    const int32_t block[3] = {index + 1, index + kTpiHoardingGrid, index + kTpiHoardingGrid + 1};
    for (int32_t other : block)
        if (cells[other].type != 0xc || cells[other].parent != id) return false;
    return true;
}

// The overlap counter a post of these links has, as the cell system's walks set it (TrackCellShape): 1 for a corner (two
// links at a right angle) or a crossroad (more than two), else 0.
inline int16_t TpiPostOverlap(uint8_t links) {
    const uint8_t hv = links & 0x55;
    int32_t count = 0;
    for (uint8_t bit = 1; bit != 0 && bit <= 0x40; bit = static_cast<uint8_t>(bit << 2))
        if ((hv & bit) != 0) ++count;
    if (count > 2) return 1;
    return (count == 2 && hv != 0x11 && hv != 0x44) ? 1 : 0;
}

// Restores every lost post of the grid (TpiLostHoardingPosts) in place and answers how many: type 0x19 again; linked
// (mHoardingNeighbours) to each post two cells away that is linked back to it, or that is lost too; its heading the link
// that leads on from the post whose heading points at it (the fence's walk goes one way round), else its first link; its
// flags the linked post's with the bridge flag (Modify 0x85: a post on freeform ground, as a footbridge is, has flag 1
// alone of 0xf); its overlap counter that of its shape (TpiPostOverlap). A lost cell with no link stays as it is.
// `cells` holds the whole 128 x 128 grid; `restored`, when given, gets each restored cell's index.
inline int32_t TpiRestoreLostHoardingPosts(TpiHoardingCell* cells, int32_t width, int32_t height, std::vector<int32_t>* restored) {
    const uint8_t headings[4] = {1, 4, 0x10, 0x40};
    const int32_t total = kTpiHoardingGrid * kTpiHoardingGrid;
    std::vector<bool> lost(total, false), headed(total, false);
    std::vector<uint8_t> links(total, 0);
    std::vector<uint16_t> flags(total, 0);
    for (int32_t index = 0; index < total; ++index)
        if ((index & 0x7f) < width && (index >> 7) < height) lost[index] = TpiLostHoardingPost(cells, index, width, height);
    for (int32_t index = 0; index < total; ++index) {
        if (!lost[index]) continue;
        uint8_t mask = 0;
        uint16_t from = 0;
        bool haveFlags = false;
        for (uint8_t heading : headings) {
            const int32_t next = TpiHoardingStep(index, heading, width, height);
            if (next < 0) continue;
            const TpiHoardingCell& n = cells[next];
            if (lost[next] || (n.type == 0x19 && (n.hoarding & TpiOppositeHeading(heading)) != 0)) {
                mask = static_cast<uint8_t>(mask | heading);
                if (!lost[next] && !haveFlags) {
                    from = n.flags;
                    haveFlags = true;
                }
            }
        }
        links[index] = mask;
        flags[index] = static_cast<uint16_t>((from & ~0xfu) | 1u);
    }
    // The headings: a post whose neighbour's heading points at it leads on along its other link. A run of lost posts
    // takes its headings one after another from the post before it.
    for (bool changed = true; changed;) {
        changed = false;
        for (int32_t index = 0; index < total; ++index) {
            if (!lost[index] || links[index] == 0 || headed[index]) continue;
            for (uint8_t heading : headings) {
                if ((links[index] & heading) == 0) continue;
                const int32_t next = TpiHoardingStep(index, heading, width, height);
                if (next < 0) continue;
                const bool known = !lost[next] || headed[next];
                if (!known) continue;
                if (cells[next].direction != TpiOppositeHeading(heading)) continue;
                const uint8_t rest = static_cast<uint8_t>(links[index] & ~heading);
                uint8_t mine = 0;
                if ((rest & TpiOppositeHeading(heading)) != 0) mine = TpiOppositeHeading(heading);
                for (uint8_t h : headings)
                    if (mine == 0 && (rest & h) != 0) mine = h;
                cells[index].direction = mine;
                headed[index] = true;
                changed = true;
                break;
            }
        }
    }
    int32_t count = 0;
    for (int32_t index = 0; index < total; ++index) {
        if (!lost[index] || links[index] == 0) continue;
        TpiHoardingCell& cell = cells[index];
        if (!headed[index]) {
            cell.direction = 0;
            for (uint8_t h : headings)
                if (cell.direction == 0 && (links[index] & h) != 0) cell.direction = h;
        }
        cell.type = 0x19;
        cell.hoarding = links[index];
        cell.flags = flags[index];
        cell.overlap = TpiPostOverlap(links[index]);
        if (restored != nullptr) restored->push_back(index);
        ++count;
    }
    return count;
}

// A control's RipOffOK after loading (`saved`, from the park file), for an object type with the record's RipOffOK
// `record` and Info.WhichUIType `uiType`: a shop's (1) rises to its record's; nothing else changes, and nothing falls.
inline int32_t TpiRepairedRipOffOK(int32_t saved, int32_t record, int32_t uiType) {
    return uiType == 1 && record > saved ? record : saved;
}

// Whether a placed object (its saved flags) is a shop (`uiType` 1) whose record now says Info.IsChoosable
// (`recordChoosable` not 0) but that was built without flag 4.
inline bool TpiShopToMakeChoosable(uint16_t flags, int32_t recordChoosable, int32_t uiType) {
    return uiType == 1 && recordChoosable != 0 && (flags & 4u) == 0;
}

// The state such a shop gets: kStateThree (3, given at building to an object no visitor chooses) becomes kStateNormal
// (0), what CObject's constructor gives a choosable one; any other state (broken down, condemned) is kept.
inline int32_t TpiRepairedShopState(int32_t state) { return state == 3 ? 0 : state; }

// Whether a type's control (`available`, its saved mAvailable) is made again from its record on loading: its record's
// research cost (`cost`, Upgrades[0].CostOfResearch) is 0, which SetupProject makes available at once, and it is saved
// unavailable or a stale project names it (`staleProject`).
inline bool TpiFreeTypeControlToRemake(int32_t cost, int32_t available, bool staleProject) {
    return cost == 0 && (available == 0 || staleProject);
}

// Whether a saved research project (its upgrade level) is stale: not a statistics upgrade (SetupStatUpgrade gives one a
// level of 1 or more, SetupProject gives 0; both can have category 4), and its type's record (`recordCost`, the research
// cost) now has no research cost, so SetupProject would not have made it.
inline bool TpiStaleResearchProject(int32_t upgradeLevel, int32_t recordCost) { return upgradeLevel == 0 && recordCost == 0; }

// A type a research project could be made for (SetupProjects' rule: loaded, UI type below 4, a research cost): its id,
// research cost and category, and whether the lab has a project (not a statistics upgrade: upgrade level 0) for it.
struct TpiResearchType {
    uint16_t id;
    int32_t cost;
    int32_t category;
    bool hasProject;
};

// The index in `types` of the type a stale project (its category and score) was made for: the first of its category
// whose research cost is the score and that has no project; -1 for none.
inline int32_t TpiStaleProjectOwner(const TpiResearchType* types, int32_t count, int32_t category, float score) {
    for (int32_t i = 0; i < count; ++i)
        if (!types[i].hasProject && types[i].category == category && static_cast<float>(types[i].cost) == score) return i;
    return -1;
}

// Whether a type with a research cost (`hasProject`, its control's `available`) gets a new project: it has none and is
// not available, so nothing could ever make it available.
inline bool TpiResearchTypeToAdd(bool hasProject, int32_t available) { return !hasProject && available == 0; }

// The object flags CObject::CObject takes from a type's record that say what the object is: 1 relief (ProvidesRelief),
// 2 a staff room (ChillsYouOut), 0x10 security (ProvidesSecurity), 0x40 litter (HoldsLitter), 0x80 fireworks (IsFireworks).
constexpr uint16_t kTpiObjectKindFlags = 0x1 | 0x2 | 0x10 | 0x40 | 0x80;

// Whether a placed object saved under a type whose research project goes to another type (`moved`) is that other type's:
// its saved flags (`savedFlags`, CObject::mFlags) hold what the other type's record makes (`newRecordFlags`) and not
// what its saved type's record now makes (`savedRecordFlags`), as a camera built under 7730 holds 0x10 and not 2.
inline bool TpiPlacedObjectToRetype(bool moved, uint16_t savedFlags, uint16_t savedRecordFlags, uint16_t newRecordFlags) {
    const uint16_t kind = savedFlags & kTpiObjectKindFlags;
    return moved && kind == (newRecordFlags & kTpiObjectKindFlags) && kind != (savedRecordFlags & kTpiObjectKindFlags);
}

// Whether a placed ride's saved script gets SPECIAL on loading: its ride would start with SPECIAL set (the O group's flags
// and the script's variable count, tpw::TpiRideScriptStartsSpecial) and the saved SPECIAL (`special`) is still 0.
inline bool TpiRideScriptToStart(uint32_t oGroupFlags, int32_t varCount, int32_t special) {
    return special == 0 && TpiRideScriptStartsSpecial(oGroupFlags, varCount);
}

// CObject's condemned state (ObjectState kStateCondemned), which nothing takes a ride out of.
constexpr int32_t kTpiObjectCondemned = 4;

// Whether a placed ride gets all its life back (CObject::mF44 to 100) on loading: its record's wear rate at its upgrade
// level (`wearRate`) is 0, so it no longer wears, its `state` is not condemned and its saved `life` is below 100.
inline bool TpiSpecialRideLifeToRestore(int32_t wearRate, int32_t state, float life) {
    return wearRate == 0 && state != kTpiObjectCondemned && life < 100.0f;
}

}  // namespace tpw

