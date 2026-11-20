// The original .sam files of each Theme Park World edition, built in (launcher/sam_originals_data.inc, written by
// devtools/gen_sam_defaults.py from the original discs). The maintainer's exception of 2026-11-17: used by the import only to
// repair a player's own damaged .sam file line by line (assets/sam_defaults.h RepairSam), never to stand in for a
// missing disc. Theme Park Inc's .sam files are not included.
#pragma once

#include <cstddef>

namespace tpw::launcher {

// Hands them to the importer (assets/sam_defaults.h SetSamDefaults). Called once, before an Importer opens.
void RegisterSamOriginals();
size_t SamOriginalCount();

}  // namespace tpw::launcher
