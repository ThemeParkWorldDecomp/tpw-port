// language_pack.cpp: the chosen language laid over the game folder (language_pack.h).
#include "language_pack.h"

#include <cctype>
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
    return {};
}

bool UseLanguagePack(const fs::path& root, const std::string& name) {
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
        if (it->is_directory(kind) && Lower(it->path().filename().string()) == Lower(spelled)) {
            folder = it->path();
            break;
        }
    }
    if (folder.empty()) return false;
    overlay = {spelled, folder};
    return true;
}

const std::string& LanguagePackName() { return Overlay().name; }
const fs::path& LanguagePackFolder() { return Overlay().folder; }

bool LanguagePackCovers(const std::vector<std::string>& parts) {
    const LanguageOverlay& overlay = Overlay();
    if (overlay.name.empty() || parts.size() < 2 || Lower(parts[0]) != "data") return false;
    const std::string second = Lower(parts[1]);
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
