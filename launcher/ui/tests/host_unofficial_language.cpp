// host_unofficial_language.cpp: a player's own translation dropped on the importer (launcher/ui/unofficial_language.h),
// with synthetic text tables and fonts made here: a sound one is kept apart from the game as its own language, said in the
// result's lines and not counted as unknown files; a damaged copy is not offered; and a table far off the game's line
// count, a bad string offset, an unterminated string, missing glyphs or an unknown code page are refused, never written
// over the game folder. No game or translation files. Run by ctest ("unofficial_language"); exits nonzero on a failure.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "importer.h"
#include "unofficial_language.h"

namespace fs = std::filesystem;
using namespace tpw::assets;
using Bytes = std::vector<uint8_t>;

namespace {

int gFailures = 0;
int gChecks = 0;

void Check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        std::printf("FAILED: %s\n", what.c_str());
        ++gFailures;
    }
}

void Put32(Bytes& b, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(n >> (i * 8)));
}

void Set32(Bytes& b, size_t at, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(n >> (i * 8));
}

// A UITEXT.str of `count` one-letter strings: the BFST header, the offsets, then each string's record.
Bytes Table(uint32_t count = 474, uint8_t letter = 'A') {
    Bytes b{'B', 'F', 'S', 'T'};
    Put32(b, 1006);
    Put32(b, count);
    for (uint32_t i = 0; i < count; ++i) Put32(b, count * 4 + i * 12);
    for (uint32_t i = 0; i < count; ++i) {
        Put32(b, (2 << 8) | 1);
        b.insert(b.end(), {letter, 0, 0, 0});
        Put32(b, 0);
    }
    return b;
}

// An F4FB font of `count` one-pixel glyphs.
Bytes Font(uint16_t count = 1) {
    Bytes b{'F', '4', 'F', 'B', 0, 8, static_cast<uint8_t>(count), static_cast<uint8_t>(count >> 8)};
    for (uint16_t i = 0; i < count; ++i) Put32(b, 8 + 4 * count + 25 * i);
    for (uint16_t i = 0; i < count; ++i) {
        const size_t at = b.size();
        b.resize(at + 25);
        Set32(b, at + 4, 1);
        Set32(b, at + 8, 1);
        b[at + 16] = 1;
        b[at + 18] = 1;
        b[at + 22] = 1;
        b[at + 24] = 255;
    }
    return b;
}

void Write(const fs::path& p, const Bytes& b) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

bool HasError(const ImportReport& report, const std::string& s) {
    for (const std::string& e : report.errors)
        if (e.find(s) != std::string::npos) return true;
    return false;
}

const LanguageStatus* Find(const std::vector<LanguageStatus>& list) {
    for (const LanguageStatus& s : list)
        if (s.name == "Unofficial-English") return &s;
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    const fs::path root = argc > 1 ? fs::u8path(argv[1]) : fs::current_path() / "unofficial-language-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    const fs::path input = root / "source", data = root / "data";
    const fs::path table = input / "wrapper/data/Language/English/UITEXT.str";
    Write(table, Table());
    // An unrelated executable, setting and bogus top-level text never become part of the language overlay.
    Write(input / "wrapper/data/global.sam", {'n', 'o'});
    Write(input / "wrapper/setup.exe", {'n', 'o'});
    ImporterConfig config;
    config.dataRoot = data.string();
    ConfigureUnofficialLanguages(config);
    Importer importer(config);
    std::string err;
    Check(importer.Open(err), "the importer opens: " + err);

    ImportReport report = importer.Import({input.string()});
    Check(report.errors.empty() && report.written == 2, "a sound translation is written for both editions");
    Check(report.translations.size() == 1 &&
              report.translations.front() == "English (unofficial translation) added: text",
          "the result says which translation was added and what it changes");
    Check(report.unknownFiles == 2 && report.differentVersions.empty(),
          "the translation's own files are neither unknown files nor other versions");
    const std::vector<LanguageStatus> languages = importer.Languages();
    const LanguageStatus* language = Find(languages);
    Check(language && language->unofficial && language->state == LanguageStatus::State::Available,
          "the translation is an available unofficial language");
    Check(language && language->changes == "text", "its changes are what it holds");
    Check(language && language->description == "Not a known official release", "an unrecognised one is said to be");
    Check(fs::exists(data / "assets/lang/Unofficial-English/data/Language/English/uitext.str") &&
              fs::exists(data / "assets/lang/Unofficial-English/data/Language/American/uitext.str"),
          "the text is kept for both editions in its own language folder");
    Check(!fs::exists(data / "assets/eur/Theme Park Data/data/Language/English/UITEXT.str"),
          "the game folder is not written");
    Check(!fs::exists(data / "assets/lang/Unofficial-English/data/global.sam"), "a setting is not part of the language");

    report = importer.Import({input.string()});
    Check(report.written == 0 && report.alreadyThere == 2, "a second drop changes nothing");
    Check(report.translations.size() == 1 &&
              report.translations.front() == "English (unofficial translation) was already added",
          "a second drop says the translation was already added");

    // Same-size damage is found before the language can be chosen, and a new drop restores it.
    Bytes altered = Table();
    altered.back() = 7;
    Write(data / "assets/lang/Unofficial-English/data/Language/English/uitext.str", altered);
    Check(!Find(importer.Languages()), "a damaged copy is not offered");
    report = importer.Import({input.string()});
    Check(report.written == 2 && Find(importer.Languages()), "a new drop restores a damaged copy");

    // An official game folder is never rewritten, even if an unsafe translation shares its paths.
    const fs::path base = data / "assets/eur/Theme Park Data/data/Language/English/UITEXT.str";
    Write(base, {'b', 'a', 's', 'e'});
    Write(table, Table(2));
    report = importer.Import({input.string()});
    Check(HasError(report, "Language pack refused: ") && HasError(report, "line count") && report.written == 0 &&
              report.translations.empty(),
          "a table far off the game's line count is refused");
    Check(fs::file_size(base) == 4, "a refused translation leaves the game folder alone");
    Write(table, Table(473));
    report = importer.Import({input.string()});
    Check(report.errors.empty(), "the older edition's UITEXT line count is taken");
    Bytes malformed = Table();
    Set32(malformed, 12, 0xfffffffc);
    Write(table, malformed);
    report = importer.Import({input.string()});
    Check(HasError(report, "string offset"), "a string offset out of the table is refused");
    malformed = Table();
    malformed.resize(malformed.size() - 4);
    Write(table, malformed);
    report = importer.Import({input.string()});
    Check(HasError(report, "unterminated"), "an unterminated string is refused");
    Write(table, Table(474, 200));
    report = importer.Import({input.string()});
    Check(HasError(report, "missing glyph fonts"), "letters beyond ASCII without their fonts are refused");
    const fs::path font = input / "wrapper/data/Language/English/MENUMED.bf4";
    Write(font, Font());
    report = importer.Import({input.string()});
    Check(HasError(report, "missing glyphs"), "a font without the text's letters is refused");
    Bytes damagedFont = Font(249);
    Set32(damagedFont, 8, 0xfffffff0);
    Write(font, damagedFont);
    report = importer.Import({input.string()});
    Check(HasError(report, "glyph offset"), "a font with a glyph offset out of the file is refused");

    // An unknown code-page tree is refused instead of sending its unchecked pointers to the original loader.
    fs::remove(font);
    Write(table, Table());
    Write(input / "wrapper/data/Language/English/UniToMB.dat", {'b', 'a', 'd'});
    report = importer.Import({input.string()});
    Check(HasError(report, "unrecognised index"), "an unknown code page is refused");
    // File names cannot pass for a recognised translation: that is by content only.
    Check(!fs::exists(data / "assets/lang/Russian"), "nothing is taken for the Russian translation by its names");

    // The loose contents of a data folder do not become a pack with another root.
    const fs::path flat = root / "flat";
    Write(flat / "Language/English/UITEXT.str", Table());
    report = importer.Import({flat.string()});
    Check(report.written == 0, "a loose Language folder is not a translation");

    fs::remove_all(root, ec);
    std::printf("unofficial_language: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
