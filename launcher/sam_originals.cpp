#include "sam_originals.h"

#include "sam_defaults.h"

namespace tpw::launcher {
namespace {
using tpw::assets::SamDefault;
#include "sam_originals_data.inc"
}  // namespace

void RegisterSamOriginals() { tpw::assets::SetSamDefaults(kSamDefaults, SamOriginalCount()); }
size_t SamOriginalCount() { return sizeof kSamDefaults / sizeof *kSamDefaults; }

}  // namespace tpw::launcher
