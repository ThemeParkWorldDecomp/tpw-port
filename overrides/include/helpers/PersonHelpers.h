// PersonHelpers.h: file-local helpers that src/game/people/Person.cpp shares with Peep.cpp and PersonQueue.cpp. Before
// the regroup, the functions using them shared a source file (3 files in all), which had one copy of each; that copy
// now lives here, static inline, so each file that includes this header keeps its own internal copy.
#pragma once

#include "ThingReceivers.h"

static inline uint8_t* Raw(const void* p) { return reinterpret_cast<uint8_t*>(const_cast<void*>(p)); }

template <class T>
static inline T& Fld(uint8_t* base, uint32_t off) { return *reinterpret_cast<T*>(base + off); }

// A one word MapPos pair from a map cell id: x = low 7 bits of id - 1 and an offset, y = the high bits and an offset.
static inline void CellPair(uint16_t id, uint8_t offX, uint8_t offY, MapPos& x, MapPos& y) {
    uint32_t v = static_cast<uint32_t>(id) - 1u;
    x.mCell = static_cast<uint8_t>(v & 0x7f);
    x.mOffset = offX;
    y.mCell = static_cast<uint8_t>((v >> 7) & 0xff);
    y.mOffset = offY;
}

// Fld_2: renamed from Fld, which is defined differently above (the files that used this one now call it so).
template <class T> static inline T& Fld_2(uint8_t* base, uint32_t offset) { return *reinterpret_cast<T*>(base + offset); }

#include "RideDB.h"
#include "TpiGuests.h"
#include "TpiToilets.h"
#include "World.h"

// TPW_PORT_FIX mods TpiToiletFor | Mac: none, every visitor uses every toilet | Port: TPW_MOD_HOOK tpi_worlds (include/TpiToilets.h): whether visitor `person` may use `object`, false only in one of the mod's worlds for the men's or women's toilet of the other sex while the park has both kinds (counted over the park's object list, open or not), so CPerson::MakeMajorDecision, CPerson::LookForMinorDest and CPerson::ProcessStateWalksToMajor pass over it | Why: tpw-mods tpi-worlds; Theme Park Inc's mtoilet is for boys and its ftoilet for girls (Info.GenderSpecific 1 and 2)
static inline bool TpiToiletFor(CObject* object, const void* person) {
    if (!tpw::TpiWorldInPlay()) return true;
    const RideInfo* info = object->GetDBInfo();
    const int8_t sex = info != nullptr ? tpw::TpiToiletSex(info->mFolder) : -1;
    if (sex < 0) return true;
    const int8_t female = tpw::TpiGuestFemale(person);
    if (female < 0 || female == sex) return true;
    int32_t kinds[2] = {0, 0};
    for (uint16_t id = gWorld->mF1DA746; id != 0;) {
        CObject* other = static_cast<CObject*>(gThingArray[id].mThing);
        if (other == nullptr) break;
        const RideInfo* otherInfo = other->GetDBInfo();
        const int8_t otherSex = otherInfo != nullptr ? tpw::TpiToiletSex(otherInfo->mFolder) : -1;
        if (otherSex >= 0) kinds[otherSex]++;
        id = Fld<uint16_t>(Raw(other), 0xc);
    }
    return tpw::TpiToiletOpenTo(sex, female, kinds[0], kinds[1]);
}
