// OptionsBalance.cpp: main app COptionsBalance. Original offsets are PowerPC offsets.
#pragma once
#include "CBalanceTable.h"

class COptionsBalance : public CBalanceTable {
public:
    COptionsBalance() { Init(); }  // inline in 0x100215a0
    ~COptionsBalance();  // 0x100216c0, nonvirtual
    const BalanceDescription* GetBalanceDescription(int32_t row) override; // 0x10021628
    BalanceValue* GetBalanceValuesStart() override; // 0x10021678
    BalanceValue mValues[28];  // 0x08, includes reserved slot zero
};
extern const BalanceDescription gOptionsBalanceDescriptions[32]; // 0x102990a8
extern COptionsBalance gOptionsBalance; // 0x102b4da8, original symbol name not recovered

// TPW_PORT_FIX samoptions gPortSamOptions | Mac: every option comes from the options file of the player's detail level (low.sam, med.sam or high.sam) | Port: a value the launcher's Graphics settings give replaces the file's in its slot when the file is loaded at start (EngineStartup_OptionsFile); -1 keeps the file's | Why: the player chooses these original .sam options in the launcher, each among the values the files' own comments list
// The launcher's choice for each slot of mValues (the rows of gOptionsBalanceDescriptions), or -1 for the options file's
// value. Every slot is -1 unless the launcher sets it, so the differential tests run the original.
inline int32_t gPortSamOptions[28] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                                      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
