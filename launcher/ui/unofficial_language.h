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
// The other edition's code page pair made from one edition's MBToUni.dat (`map`): the European one is the US one with
// U+2019 at byte 0x15. UniToMB.dat is the game's range tree of it (TbIUnicodeToMB::LocateValue): the runs of consecutive
// characters, the middle run first, then the tree of the runs below it and of those above it. False for any other table.
bool OtherCodePage(const std::vector<uint8_t>& map, std::vector<uint8_t>& mbToUni, std::vector<uint8_t>& uniToMb);

// A translation is named by a known release's hashes first, then by the letters of its own text; one that neither names
// (description tpw::ui::kUnofficialUnknown, ID Unofficial-English or Unofficial-American) is named by the player, from
// this list: each language's English name and, where the launcher's Latin-1 font draws it, its own ("Polski"; empty
// otherwise).
struct TranslationLanguage {
    const char* name;
    const char* own;
};
const std::vector<TranslationLanguage>& TranslationLanguages();
// The TranslationLanguages() index the name of a dropped zip, image or folder names ("Russian.zip", "Русский",
// "russkiy"), the first that names one; -1 when none does.
int SuggestedTranslationLanguage(const std::vector<std::string>& paths);
// Names the unnamed translation `id` `language` (a TranslationLanguages() name): its folder becomes `newId` (Russian, or
// Unofficial-<language>), replacing a translation kept under that ID, and its description says the player named it.
// Dropping the same files again then finds it under its new name and asks nothing.
bool NameUnofficialLanguage(const std::string& assetsDir, const std::string& id, const std::string& language,
                            std::string& newId, std::string& err);
#endif
}  // namespace tpw::assets
