// TPOptions.cpp: name proven by __sinit_TPOptions_cpp in the main application.

#include "OptionsBalance.h"
#include "TimeSig.h"
#include "TPDialog.h"
#include "TPOptions.h"
#include "World.h"

COptions gOptions; // __sinit_TPOptions_cpp at 0x1015f40c, object at 0x1038070c
COptions::~COptions() {}

namespace {
// TPW_PORT_FIX numkids NumKids | Mac: (none: the getters read GameOptions.NUMKIDS from gOptionsBalance) | Port: NUMKIDS as the launcher's "Visitor looks" sets it (gPortNumKids, include/TPOptions.h), else the options file's value | Why: the player's choice of an original .sam option (low.sam, med.sam and high.sam, GameOptions.NUMKIDS) outranks the detail level's
int32_t NumKids() {
    return gPortNumKids >= 0 ? gPortNumKids : gOptionsBalance.mValues[24].mInteger;   // original object +0x68, NUMKIDS
}
}  // namespace

int32_t COptions::GetNumKidSprites() const {
    if (GetOptionsSetting() == 0x4000) {
        switch (::GetNumKidSprites()) {
            case 8: return 8;
            case 16: return 6;
            case 32: return 4;
            default: return 0;
        }
    }
    // TPW_PORT_FIX numkids COptions::GetNumKidSprites | Mac: the number of kid sprite files from GameOptions.NUMKIDS (low.sam, med.sam or high.sam) | Port: from NumKids, the launcher's "Visitor looks" when it is set | Why: the player chooses this original .sam option in the launcher's Settings box
    switch (NumKids()) {
        case 0: return 2;
        case 1: return 4;
        case 2: return 6;
        default: return 8;
    }
}

int32_t COptions::GetNumStaffSprites() const {
    if (GetOptionsSetting() == 0x4000) {
        switch (::GetNumKidSprites()) {
            case 8: case 16: return 2;
            case 32: return 1;
            default: return 0;
        }
    }
    // TPW_PORT_FIX numkids COptions::GetNumStaffSprites | Mac: one look a staff type when GameOptions.NUMKIDS is 0, else two | Port: from NumKids, the launcher's "Visitor looks" when it is set | Why: NUMKIDS sets the staff looks with the kids', so the player's choice applies to both, as a detail level's does
    return NumKids() == 0 ? 1 : 2;
}

// CTimeSig::Serialise (0x1015ef5c) and CTimeSig::SecondsSinceLastUpdate (0x1015f124).
// Both are defined inline in include/TimeSig.h (the class's inline members); this file includes the header so the
// unit library compiles them, and holds no other code.
