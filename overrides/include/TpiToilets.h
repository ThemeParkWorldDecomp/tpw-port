// TpiToilets.h: the TPI Worlds and Rides mod's men's and women's toilets (TPW_MOD_HOOK tpi_worlds).
// Theme Park Inc has two toilets, mtoilet ("for boys", Info.GenderSpecific 1) and ftoilet ("for girls", 2); the converter
// writes both as Theme Park World features (features/mtoilet and features/ftoilet, "Toilet - Male" and "Toilet - Female"),
// toilets every visitor uses. In the mod's worlds, while the park has at least one of each (built, open or not), a man or
// boy uses only the men's and a woman or girl only the women's; with one kind only, everyone uses it, as before. A
// visitor's sex is its look's (include/TpiGuests.h, TpiGuestFemale). Theme Park World's own toilet stays everyone's.
// Applied where a visitor picks a ride (CPerson::MakeMajorDecision, CPerson::LookForMinorDest) and where it reaches the
// back of the queue (CPerson::ProcessStateWalksToMajor), so a visitor never joins the other kind's queue. Nothing is
// saved: the kind is the item's folder, the counts are the park's objects, the sex the look.
// Header only and pure, so the host test sees what the game does.
#pragma once


// TPW_PORT_FIX mods TpiToilets | Mac: no such header; every visitor uses every toilet | Port: which toilet kind an item is and whether a visitor of a sex may use it, read by TpiToiletFor (include/helpers/PersonHelpers.h) in the mod's worlds (TPW_MOD_HOOK tpi_worlds) | Why: tpw-mods tpi-worlds; Theme Park Inc's mtoilet is for boys and ftoilet for girls (Info.GenderSpecific 1 and 2)

#include <cstdint>

namespace tpw {

// The kind of toilet of the item in `folder` (RideInfo::mFolder, a path whose last part is the item's name, any case,
// ':' '/' or '\\' apart, a ".wad" ending dropped): 0 the men's (mtoilet), 1 the women's (ftoilet), -1 anything else.
inline int8_t TpiToiletSex(const char* folder) {
    if (folder == nullptr) return -1;
    const char* end = folder;
    while (*end != 0) ++end;
    while (end > folder && (end[-1] == ':' || end[-1] == '/' || end[-1] == '\\')) --end;
    if (end - folder >= 4 && end[-4] == '.' && (end[-3] | 0x20) == 'w' && (end[-2] | 0x20) == 'a' && (end[-1] | 0x20) == 'd')
        end -= 4;
    const char* stem = end;
    while (stem > folder && stem[-1] != ':' && stem[-1] != '/' && stem[-1] != '\\') --stem;
    if (end - stem != 7) return -1;
    const char* const rest = "toilet";
    for (int32_t i = 0; i < 6; ++i)
        if ((stem[1 + i] | 0x20) != rest[i]) return -1;
    const char first = static_cast<char>(stem[0] | 0x20);
    return first == 'm' ? 0 : first == 'f' ? 1 : -1;
}

// Whether a visitor of sex `female` (1 a woman or girl, 0 a man or boy, -1 not known) may use a toilet of kind `sex`
// (TpiToiletSex) in a park with `men` men's and `women` women's toilets: always, but for the other kind's when the park
// has both kinds and the visitor's sex is known.
inline bool TpiToiletOpenTo(int8_t sex, int8_t female, int32_t men, int32_t women) {
    if (sex < 0 || female < 0 || men <= 0 || women <= 0) return true;
    return sex == female;
}

}  // namespace tpw

