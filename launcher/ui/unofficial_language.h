// Player-supplied translations. Only validated language, speech and interface files are kept, apart from the game.
#pragma once
#include "importer.h"

namespace tpw::assets {
#ifdef TPW_PORT_FIX
// Attach the port's isolated translation handling to a manifest-only importer.
void ConfigureUnofficialLanguages(ImporterConfig& config);
// Called for each opened source, including cabinets inside discs. Official text contents are left to the manifest; the
// entries a translation is made of are claimed, and each translation gets a line for the result screen.
SupplementResult ImportUnofficialLanguages(Source& source, const std::string& assetsDir,
                                           const std::atomic<bool>& cancel);
// Installed translations carry a checked inventory; incomplete or changed copies are not selectable.
std::vector<LanguageStatus> UnofficialLanguages(const std::string& assetsDir);
#endif
}  // namespace tpw::assets
