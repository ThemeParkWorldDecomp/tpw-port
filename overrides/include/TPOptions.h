// TPOptions.cpp: main app COptions (no state used by its getters or destructor).
#pragma once
#include <cstdint>
class COptions {
public:
    ~COptions(); // 0x1015f1a4
    int32_t GetNumKidSprites() const;   // 0x1015f20c
    int32_t GetNumStaffSprites() const; // 0x1015f30c
};
extern COptions gOptions; // 0x1038070c
// TPW_PORT_FIX numkids gPortNumKids | Mac: (none: GameOptions.NUMKIDS comes from the detail level's options file, low.sam 0, med.sam and high.sam 2) | Port: the launcher's "Visitor looks" sets it to one of the file's own choices, 0 to 3 ("0->4, 1->6, 2->8, 3->MAX"), which COptions then reads in place of the file's value; -1, the file's value, by default | Why: an option of the original's options files (low.sam, med.sam and high.sam, GameOptions.NUMKIDS) that the game's detail levels only ever set to 0 or 2; the player chooses it in the launcher, never by editing a .sam file
// -1 unless the launcher sets it, so a program that does not (the differential tests) reads the file's value.
inline int32_t gPortNumKids = -1;
