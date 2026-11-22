// language_pack.cpp: the chosen language laid over the game folder (language_pack.h).
#include "language_pack.h"
#include "path_text.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace tpw {

static std::string Lower(const std::string& text) {
    std::string lower = text;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower;
}

struct LanguageOverlay {
    std::string name;    // as the original spells it; empty when off
    fs::path folder;     // root/lang/<name>
    bool unofficial = false;
    std::string id;      // a player-supplied translation's ID (Russian, Unofficial-<language>)
};

static LanguageOverlay& Overlay() {
    static LanguageOverlay overlay;
    return overlay;
}

std::string LanguageName(const std::string& name) {
    // FS_Registry.cpp's 16, in its order.
    static const char* const kNames[] = {"English", "French",   "German", "Spanish", "Swedish", "Italian",
                                         "Dutch",   "Polish",   "Danish", "Japanese", "TraditionalChinese",
                                         "SimplifiedChinese", "Thai", "Korean", "Hebrew", "American"};
    const std::string lower = Lower(name);
    for (const char* known : kNames)
        if (lower == Lower(known)) return known;
#ifdef TPW_PORT_FIX
    // The IDs of validated player-supplied replacements: Russian, or Unofficial- and the translation's language in letters
    // (launcher/ui/unofficial_language.h). The game's own language name is used at run time so its missing resources fall
    // back to that edition's files; these IDs never enter the original's language registry.
    if (lower == "russian") return "Russian";
    if (lower.compare(0, 11, "unofficial-") == 0 && lower.size() > 11 && lower.size() <= 40 &&
        std::all_of(lower.begin() + 11, lower.end(), [](char c) { return c >= 'a' && c <= 'z'; }))
        return "Unofficial-" + name.substr(11);
#endif
    return {};
}

bool UseLanguagePack(const fs::path& root, const std::string& name, const std::string& own) {
    LanguageOverlay& overlay = Overlay();
    overlay = {};
    const std::string spelled = LanguageName(name);
    if (spelled.empty() || root.empty()) return false;
    // The folder by its own name in any case, as an importer or a copy by hand may have written it.
    std::error_code ec;
    fs::path folder;
    const fs::path lang = root / "lang";
    for (fs::directory_iterator it(lang, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code kind;
        if (it->is_directory(kind) && Lower(tpw::PathText(it->path().filename())) == Lower(spelled)) {
            folder = it->path();
            break;
        }
    }
    if (folder.empty()) return false;
#ifdef TPW_PORT_FIX
    if (spelled == "Russian" || spelled.compare(0, 11, "Unofficial-") == 0) {
        std::ifstream marker(folder / ".unofficial");
        std::string version, key;
        std::getline(marker, version);
        std::getline(marker, key);
        if (version != "1" || Lower(key) != Lower(spelled) || (own != "English" && own != "American")) return false;
        overlay = {own, folder, true, spelled};
        return true;
    }
#else
    (void)own;
#endif
    overlay = {spelled, folder, false, {}};
    return true;
}

const std::string& LanguagePackName() { return Overlay().name; }
const fs::path& LanguagePackFolder() { return Overlay().folder; }

int LanguagePackCodePage() {
#ifdef TPW_PORT_FIX
    const LanguageOverlay& overlay = Overlay();
    if (!overlay.unofficial) return 0;
    std::string language = Lower(overlay.id);
    if (language.compare(0, 11, "unofficial-") == 0) language = language.substr(11);
    for (const char* cyrillic : {"russian", "ukrainian", "belarusian", "bulgarian", "serbian", "macedonian"})
        if (language == cyrillic) return 1251;
#endif
    return 0;
}

bool LanguagePackCovers(const std::vector<std::string>& parts) {
    const LanguageOverlay& overlay = Overlay();
    if (overlay.name.empty() || parts.size() < 2 || Lower(parts[0]) != "data") return false;
    const std::string second = Lower(parts[1]);
#ifdef TPW_PORT_FIX
    if (overlay.unofficial && second == "ui.wad" && parts.size() == 2) return true;
#endif
    if (second == "init") return true;
    if (second == "language") return parts.size() >= 3 && Lower(parts[2]) == Lower(overlay.name);
    if (second == "global") return parts.size() >= 3 && Lower(parts[2]) == "speech";
    if (second == "levels") return parts.size() >= 4 && Lower(parts[3]) == "speech";
    return false;
}

bool LanguagePackLists(const std::vector<std::string>& parts, const std::string& name) {
    const LanguageOverlay& overlay = Overlay();
    if (overlay.name.empty()) return false;
    if (parts.size() == 2 && Lower(parts[0]) == "data" && Lower(parts[1]) == "language")
        return Lower(name) == Lower(overlay.name);
    return LanguagePackCovers(parts);
}

}  // namespace tpw
