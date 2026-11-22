// host_unofficial_language.cpp: a player's own translation dropped on the importer (launcher/ui/unofficial_language.h),
// with synthetic text tables and fonts made here: a sound one is kept apart from the game as its own language, said in the
// result's lines and not counted as unknown files; a damaged copy is not offered; a glyph that ends early, as the
// editions' own fonts do, is taken; the same translation dropped again is kept and a changed one replaces it; and a
// table far off the game's line count, a bad string offset, an unterminated string, a damaged glyph, a letter past a
// font's last glyph or an unknown code page are refused, never written over the game folder. A translation is named by its text's letters
// (Russian, Ukrainian, Belarusian, Bulgarian; Latin, mixed and Serbian text is not named; English text is another
// pressing's game text, neither added nor asked about), its English and American
// folders with the same text are one translation, and one not named is named by the player once: dropped again, it is
// found under that name. A few letters a font draws as nothing are said with the translation, naming the font, the
// letter and its lines; so many that the text would not show refuse it. One edition's code page pair is made from the
// other's MBToUni.dat both ways, its UniToMB.dat finding every character as the game looks it up. No game or translation files: the text is made-up sentences, the code page an identity one.
// Run by ctest ("unofficial_language"); exits nonzero on a failure.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "importer.h"
#include "texts.h"
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

// An F4FB font of one run-length glyph, `width` x 1 pixels in a buffer of `buffer` bytes, its data `data`.
Bytes PackedFont(const Bytes& data, uint8_t width, uint32_t buffer) {
    Bytes b{'F', '4', 'F', 'B', 0, 8, 1, 0};
    Put32(b, 12);
    const size_t at = b.size();
    b.resize(at + 24);
    Set32(b, at + 4, static_cast<uint32_t>(data.size()));
    Set32(b, at + 8, buffer);
    Set32(b, at + 12, 1);
    b[at + 16] = width;
    b[at + 18] = 1;
    b[at + 22] = width;
    b.insert(b.end(), data.begin(), data.end());
    return b;
}

// A UITEXT.str of the strings `lines`: their bytes as the code page numbers the letters.
Bytes TextLines(const std::vector<std::string>& lines) {
    Bytes b{'B', 'F', 'S', 'T'};
    Put32(b, 1006);
    const uint32_t count = static_cast<uint32_t>(lines.size());
    Put32(b, count);
    uint32_t offset = count * 4;
    for (const std::string& text : lines) {
        Put32(b, offset);
        offset += 4 + ((static_cast<uint32_t>(text.size()) + 3) & ~3u) + 4;
    }
    for (const std::string& text : lines) {
        Put32(b, (static_cast<uint32_t>(text.size()) << 8) | 1);
        b.insert(b.end(), text.begin(), text.end());
        b.resize(b.size() + (((text.size() + 3) & ~size_t(3)) - text.size()));
        Put32(b, 0);
    }
    return b;
}

// A UITEXT.str of `count` strings, each `text`.
Bytes TextTable(const std::string& text, uint32_t count = 474) {
    return TextLines(std::vector<std::string>(count, text));
}

// `font` with the glyph of byte `letter` emptied: no pixels, no width.
Bytes Blank(Bytes font, uint8_t letter) {
    const size_t at = 8 + 4 * (letter - 1);
    const uint32_t glyph = font[at] | (font[at + 1] << 8) | (font[at + 2] << 16) | (uint32_t(font[at + 3]) << 24);
    for (size_t i : {16, 17, 18, 19, 22, 23}) font[glyph + i] = 0;
    return font;
}

// A code page (MBToUni.dat) of `count` characters that gives each byte its own Latin-1 number, the way a fan
// translation's fonts are drawn.
Bytes IdentityCodePage(uint8_t count = 255) {
    Bytes b{'B', 'F', 'M', 'U', 0, 0, count, 0};
    for (unsigned c = 1; c <= count; ++c) b.insert(b.end(), {static_cast<uint8_t>(c), 0});
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

std::optional<LanguageStatus> Find(const std::vector<LanguageStatus>& list,
                                   const std::string& name = "Unofficial-English") {
    for (const LanguageStatus& s : list)
        if (s.name == name) return s;
    return std::nullopt;
}

size_t Unofficial(const std::vector<LanguageStatus>& list) {
    return static_cast<size_t>(std::count_if(list.begin(), list.end(), [](const LanguageStatus& s) { return s.unofficial; }));
}

// A pack of the text `text` in data/Language/<folder>/, with every general font the game's index uses, each covering
// all 255 glyphs.
void WritePack(const fs::path& pack, const std::string& text, const char* folder = "English") {
    const fs::path language = pack / "data/Language" / folder;
    Write(language / "UITEXT.str", TextTable(text));
    const std::string prefix = "data/Language/English/";
    for (size_t i = 0; i < ManifestRecordCount(); ++i) {
        const std::string dest = ManifestRecords()[i].dest;
        if (dest.compare(0, prefix.size(), prefix) != 0 || dest.size() < 4 || dest.substr(dest.size() - 4) != ".bf4") continue;
        Write(language / dest.substr(prefix.size()), Font(255));
    }
}

// The byte a UniToMB.dat range tree gives `value`, walked as the game's TbIUnicodeToMB::LocateValue does; 0 for none.
uint16_t Locate(const Bytes& tree, uint16_t value) {
    const auto word = [&](size_t at) { return static_cast<uint16_t>(tree[4 + at] | (tree[5 + at] << 8)); };
    size_t node = 0;
    for (int steps = 0; steps < 64; ++steps) {
        if (value < word(node)) {
            if (!word(node + 2)) return 0;
            node += word(node + 2);
        } else if (value > word(node + 4)) {
            if (!word(node + 6)) return 0;
            node += word(node + 6);
        } else
            return word(node + 8 + 2 * (value - word(node)));
    }
    return 0;
}

int Index(const char* language) {
    const auto& list = TranslationLanguages();
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].name == std::string(language)) return static_cast<int>(i);
    return -2;
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
    std::optional<LanguageStatus> language = Find(languages);
    Check(language && language->unofficial && language->state == LanguageStatus::State::Available,
          "the translation is an available unofficial language");
    Check(language && language->changes == "text", "its changes are what it holds");
    Check(language && language->description == tpw::ui::kUnofficialUnknown, "one whose language is not known is said to be");
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
    Check(HasError(report, "Language pack refused: byte 0xC8, used in UITEXT.str lines 1, 2, 3, 4 and 470 others, is past "
                           "the last of MENUMED.bf4's 1 letter, so the game would draw it from outside the font"),
          "a letter past a font's last glyph is refused, naming the letter, its lines and the font" + std::string(": ") +
              (report.errors.empty() ? std::string() : report.errors.front()));
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

    // A glyph whose run-length data ends before its last pixel, as some of the editions' own fonts do (tpw-port #128),
    // is not damage; one without its end mark, or with more pixels than its buffer, is.
    const fs::path pack = root / "pack", packData = root / "pack-data";
    ImporterConfig packConfig;
    packConfig.dataRoot = packData.string();
    ConfigureUnofficialLanguages(packConfig);
    Importer packImporter(packConfig);
    Check(packImporter.Open(err), "the importer opens: " + err);
    Write(pack / "data/Language/English/UITEXT.str", Table());
    const fs::path date = pack / "data/Language/English/DATEBIG.bf4";
    Write(date, PackedFont({0xff}, 2, 1));
    report = packImporter.Import({pack.string()});
    Check(HasError(report, "datebig.bf4: damaged compressed glyph") && report.written == 0,
          "a glyph without its end mark is refused");
    Write(date, PackedFont({0x0f, 0xf0, 0x00}, 2, 1));
    report = packImporter.Import({pack.string()});
    Check(HasError(report, "datebig.bf4: damaged compressed glyph") && report.written == 0,
          "a glyph with more pixels than its buffer is refused");
    Write(date, PackedFont({0xf0, 0x00}, 2, 1));
    report = packImporter.Import({pack.string()});
    Check(report.errors.empty() && report.written == 4 && report.translations.size() == 1 &&
              report.translations.front() == "English (unofficial translation) added: text, fonts",
          "a glyph that ends a pixel early is taken, as the game draws it");

    // Dropping the same translation again keeps it; a different one under the same name replaces it, and says so.
    report = packImporter.Import({pack.string()});
    Check(report.written == 0 && report.alreadyThere == 4 && report.translations.size() == 1 &&
              report.translations.front() == "English (unofficial translation) was already added",
          "the same translation dropped again is kept as it is");
    Write(pack / "data/Language/English/UITEXT.str", Table(474, 'B'));
    report = packImporter.Import({pack.string()});
    Check(report.errors.empty() && report.written == 4 && report.translations.size() == 1 &&
              report.translations.front() == "English (unofficial translation) updated: text, fonts",
          "a changed translation dropped again replaces the one kept");
    std::ifstream kept(packData / "assets/lang/Unofficial-English/data/Language/English/uitext.str", std::ios::binary);
    const Bytes keptBytes{std::istreambuf_iterator<char>(kept), std::istreambuf_iterator<char>()};
    Check(keptBytes == Table(474, 'B') && Find(packImporter.Languages()), "the newest drop is the language offered");
    Check(!fs::exists(packData / "assets/.staging/language-Unofficial-English"), "no staging folder is left behind");

    // Named by its letters: read through the installed edition's code page, as the pack brings none.
    const fs::path namedData = root / "named-data";
    ImporterConfig namedConfig;
    namedConfig.dataRoot = namedData.string();
    ConfigureUnofficialLanguages(namedConfig);
    Importer named(namedConfig);
    Check(named.Open(err), "the importer opens: " + err);
    const std::string russianText =
        "\xC4\xEE\xE1\xF0\xEE \xEF\xEE\xE6\xE0\xEB\xEE\xE2\xE0\xF2\xFC \xE2 \xE2\xE0\xF8 \xEF\xE0\xF0\xEA! \xDD\xF2\xEE "
        "\xEC\xFB, \xB8\xEB\xEA\xE8.";
    WritePack(root / "russian", russianText);
    report = named.Import({(root / "russian").string()});
    Check(report.errors.empty() && report.translations.size() == 1 &&
              report.translations.front() == "English (unofficial translation) added: text, fonts" &&
              Find(named.Languages()),
          "without a code page to read it through, a translation is not named");
    const fs::path codePage = namedData / "assets/eur/Theme Park Data/data/Language/English/MBToUni.dat";
    Write(codePage, IdentityCodePage());
    report = named.Import({(root / "russian").string()});
    language = Find(named.Languages(), "Russian");
    Check(report.errors.empty() && report.translations.size() == 1 &&
              report.translations.front() == "Russian (unofficial translation) added: text, fonts" && language &&
              language->description == "Its text reads as Russian" && fs::exists(namedData / "assets/lang/Russian/.unofficial"),
          "Cyrillic text with Russian's own letters is Russian");
    const std::pair<const char*, std::string> kLetters[] = {
        {"Ukrainian",
         "\xCB\xE0\xF1\xEA\xE0\xE2\xEE \xEF\xF0\xEE\xF1\xE8\xEC\xEE \xE4\xEE \xE2\xE0\xF8\xEE\xE3\xEE \xEF\xE0\xF0\xEA\xF3, "
         "\xBF\xE6\xE0 \xB3 \xF0\xEE\xE7\xE2\xE0\xE3\xE8 \xBA \xF2\xF3\xF2."},
        {"Belarusian",
         "\xD1\xE0\xF0\xE4\xFD\xF7\xED\xE0 \xE7\xE0\xEF\xF0\xE0\xF8\xE0\xE5\xEC \xF3 \xE2\xE0\xF8 \xEF\xE0\xF0\xEA \xB3 "
         "\xA2\xF1\xE5 \xE0\xF2\xF0\xE0\xEA\xF6\xFB\xB8\xED\xFB."},
        {"Bulgarian",
         "\xC4\xEE\xE1\xF0\xE5 \xE4\xEE\xF8\xEB\xE8 \xE2\xFA\xE2 \xE2\xE0\xF8\xE8\xFF \xEF\xE0\xF0\xEA, \xEA\xFA\xE4\xE5\xF2\xEE "
         "\xE8\xEC\xE0 \xE2\xF1\xE8\xF7\xEA\xEE."},
    };
    for (const auto& letters : kLetters) {
        const fs::path pack = root / letters.first;
        WritePack(pack, letters.second);
        report = named.Import({pack.string()});
        language = Find(named.Languages(), std::string("Unofficial-") + letters.first);
        Check(report.errors.empty() && report.translations.size() == 1 &&
                  report.translations.front() == std::string(letters.first) + " (unofficial translation) added: text, fonts" &&
                  language && language->description == std::string("Its text reads as ") + letters.first,
              std::string("Cyrillic text with ") + letters.first + "'s own letters is " + letters.first);
    }
    // Serbian and Macedonian share their letters, Latin text names no alphabet, and a mix is not sure: none is named.
    const std::pair<const char*, std::string> kUnnamed[] = {
        {"serbian",
         "\xC4\xEE\xE1\xF0\xEE\xE4\xEE\xF8\xEB\xE8 \xF3 \xE2\xE0\xF8 \xEF\xE0\xF0\xEA, \x9A\xF3\xE1\xE0\xE2 \xE8 \x9F\xE5\xEC "
         "\xF1\xF3 \xF2\xF3."},
        {"latin", "Bem-vindo ao seu parque, divirta-se!"},
        {"mixed", "Park \xEF\xE0\xF0\xEA Welcome to your park."},
    };
    for (const auto& unnamed : kUnnamed) {
        const fs::path pack = root / "unnamed" / unnamed.first;
        WritePack(pack, unnamed.second);
        report = named.Import({pack.string()});
        language = Find(named.Languages());
        Check(report.errors.empty() && report.translations.size() == 1 &&
                  report.translations.front().compare(0, 33, "English (unofficial translation) ") == 0 && language &&
                  language->description == tpw::ui::kUnofficialUnknown,
              std::string("the ") + unnamed.first + " text is not named");
    }
    // English text, in bytes other than any edition's own, is another pressing's game text, not a translation: in the
    // English or the American folder it is neither added nor asked about, and its files are left to the edition.
    for (const char* folder : {"English", "American"}) {
        const fs::path pack = root / "english" / folder, englishData = root / "english-data" / folder;
        WritePack(pack, "Welcome to your park! Build the rides you like and keep your visitors happy.", folder);
        ImporterConfig englishConfig;
        englishConfig.dataRoot = englishData.string();
        ConfigureUnofficialLanguages(englishConfig);
        Importer english(englishConfig);
        Check(english.Open(err), "the importer opens: " + err);
        report = english.Import({pack.string()});
        Check(report.errors.empty() && report.translations.empty() && Unofficial(english.Languages()) == 0 &&
                  !fs::exists(englishData / "assets/lang/Unofficial-English") &&
                  !fs::exists(englishData / "assets/lang/Unofficial-American"),
              std::string("English text in the ") + folder + " folder is not a translation");
    }

    // An English and an American folder with the same text are one translation; different text in each is two.
    const fs::path twins = root / "twins";
    WritePack(twins, "Bem-vindo ao seu parque!", "English");
    WritePack(twins, "Bem-vindo ao seu parque!", "American");
    const fs::path twinData = root / "twin-data";
    ImporterConfig twinConfig;
    twinConfig.dataRoot = twinData.string();
    ConfigureUnofficialLanguages(twinConfig);
    Importer twin(twinConfig);
    Check(twin.Open(err), "the importer opens: " + err);
    report = twin.Import({twins.string()});
    Check(report.errors.empty() && report.translations.size() == 1 &&
              report.translations.front() == "American (unofficial translation) added: text, fonts" &&
              Unofficial(twin.Languages()) == 1 && Find(twin.Languages(), "Unofficial-American") &&
              report.unknownFiles == 0,
          "the same text in English and American is one translation, all of its files claimed");
    Write(twins / "data/Language/English/UITEXT.str", TextTable("Bem-vindo, outra vez!"));
    report = twin.Import({twins.string()});
    Check(report.errors.empty() && report.translations.size() == 2 && Unofficial(twin.Languages()) == 2,
          "different text in each is two translations");

    // The player names one whose language is not known, once: dropped again, it is found under that name.
    const fs::path latin = root / "unnamed" / "latin";
    std::string newId;
    Check(!NameUnofficialLanguage((namedData / "assets").string(), "Unofficial-English", "Klingon", newId, err),
          "a name not in the list is refused");
    Check(!NameUnofficialLanguage((namedData / "assets").string(), "Russian", "Polish", newId, err),
          "a named translation is not named again");
    report = named.Import({latin.string()});
    Check(report.translations.size() == 1 && report.translations.front().find(" updated: ") != std::string::npos,
          "the Latin text is kept, unnamed");
    Check(NameUnofficialLanguage((namedData / "assets").string(), "Unofficial-English", "Portuguese", newId, err) &&
              newId == "Unofficial-Portuguese",
          "an unnamed translation is named: " + err);
    language = Find(named.Languages(), "Unofficial-Portuguese");
    Check(language && language->state == LanguageStatus::State::Available &&
              language->description == "Named Portuguese when it was added" && !Find(named.Languages()) &&
              !fs::exists(namedData / "assets/lang/Unofficial-English"),
          "the named translation is offered under its name, and only there");
    report = named.Import({latin.string()});
    Check(report.written == 0 && report.translations.size() == 1 &&
              report.translations.front() == "Portuguese (unofficial translation) was already added" &&
              !Find(named.Languages()),
          "dropped again, the named translation is found under its name");
    Check(!fs::exists(namedData / "assets/.staging/language-Unofficial-Portuguese") &&
              !fs::exists(namedData / "assets/.staging/language-Unofficial-English"),
          "no staging folder is left behind");

    // A letter a text font draws as nothing: a few are said with the translation (the font, the letter, its lines), so
    // many that the text would not show refuse it. The date and cash fonts draw only digits and are not asked.
    const fs::path blank = root / "blank", blankData = root / "blank-data";
    WritePack(blank, "Bem-vindo ao seu parque");
    std::vector<fs::path> fonts;
    for (const auto& file : fs::directory_iterator(blank / "data/Language/English")) {
        const std::string name = file.path().filename().string();
        if (file.path().extension() == ".bf4" && name.compare(0, 4, "DATE") != 0 && name.compare(0, 4, "CASH") != 0 &&
            name != "MATISSE36.bf4")
            fonts.push_back(file.path());
    }
    std::sort(fonts.begin(), fonts.end());
    std::vector<std::string> lines(474, "Bem-vindo ao seu parque");
    lines[2] = lines[9] = "Bem-vindo ao seu parque!";
    Write(blank / "data/Language/English/UITEXT.str", TextLines(lines));
    Write(fonts[0], Blank(Font(255), '!'));
    for (const auto& file : fs::directory_iterator(blank / "data/Language/English"))
        if (file.path().filename().string().compare(0, 4, "DATE") == 0) Write(file.path(), Blank(Font(255), '!'));
    ImporterConfig blankConfig;
    blankConfig.dataRoot = blankData.string();
    ConfigureUnofficialLanguages(blankConfig);
    Importer blanks(blankConfig);
    Check(blanks.Open(err), "the importer opens: " + err);
    Write(blankData / "assets/eur/Theme Park Data/data/Language/English/MBToUni.dat", IdentityCodePage());
    report = blanks.Import({blank.string()});
    const std::string said = "English (unofficial translation): some of it will not show right: ";
    Check(report.errors.empty() && report.translations.size() == 2 &&
              report.translations[1] == said + fonts[0].filename().string() +
                                            " does not draw byte 0x21 (\"!\"), used in UITEXT.str lines 3 and 10" &&
              Find(blanks.Languages()),
          "a letter one font draws as nothing is said with the translation, which is kept: " +
              (report.translations.size() > 1 ? report.translations[1] : std::string()));
    for (const fs::path& font : fonts) Write(font, Blank(Font(255), '!'));
    report = blanks.Import({blank.string()});
    Check(report.errors.empty() && report.translations.size() == 2 &&
              report.translations[1] == said + "no font in it draws byte 0x21 (\"!\"), used in UITEXT.str lines 3 and 10",
          "a letter no font draws is said so");
    lines.assign(474, "Bem-vindo ao seu parque!");
    Write(blank / "data/Language/English/UITEXT.str", TextLines(lines));
    report = blanks.Import({blank.string()});
    Check(report.errors.size() == 1 &&
              HasError(report, "Language pack refused: its fonts draw 474 of its 9954 letters as nothing: no font in it "
                               "draws byte 0x21 (\"!\"), used in UITEXT.str lines 1, 2, 3, 4 and 470 others") &&
              report.translations.empty(),
          "so many letters drawn as nothing that the text would not show refuse it" + std::string(": ") +
              (report.errors.empty() ? std::string() : report.errors.front()));

    // The name a dropped file or folder suggests.
    Check(SuggestedTranslationLanguage({"/drops/Russian.zip"}) == Index("Russian") &&
              SuggestedTranslationLanguage({"/drops/\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9 "
                                            "\xD0\xBF\xD0\xB5\xD1\x80\xD0\xB5\xD0\xB2\xD0\xBE\xD0\xB4.rar"}) ==
                  Index("Russian") &&
              SuggestedTranslationLanguage({"/drops/sim_theme_park_full_rus.iso"}) == Index("Russian") &&
              SuggestedTranslationLanguage({"/drops/\xD0\xA3\xD0\x9A\xD0\xA0\xD0\x90\xD0\x87\xD0\x9D\xD0\xA1\xD0\xAC\xD0\x9A\xD0\x90/"}) ==
                  Index("Ukrainian") &&
              SuggestedTranslationLanguage({"/drops/Tpworld.mdf", "/drops/TPW-Portugues.7z"}) == Index("Portuguese") &&
              SuggestedTranslationLanguage({"/drops/Tpworld.mdf"}) == -1 &&
              SuggestedTranslationLanguage({"/drops/FullRus.zip"}) == -1,
          "a file or folder name that names a language suggests it");

    // A pack's code page pair of the other edition than its fonts is replaced by the fonts' edition's own, made from it:
    // the European one is the US one with U+2019 at byte 0x15. Made-up characters, in runs with gaps between them.
    Bytes us{'B', 'F', 'M', 'U', 0, 0, 248, 0};
    for (unsigned b = 1; b <= 248; ++b) {
        const unsigned u = b <= 20 ? 0x30 + b : b <= 120 ? 0x100 + b : 0x500 + b;
        us.insert(us.end(), {static_cast<uint8_t>(u), static_cast<uint8_t>(u >> 8)});
    }
    Bytes eur, eurTree, back, usTree;
    Check(OtherCodePage(us, eur, eurTree) && eur.size() == us.size() + 2 && eur[6] == 249 && eur[8 + 2 * 20] == 0x19 &&
              eur[9 + 2 * 20] == 0x20 && std::equal(us.begin() + 8, us.begin() + 48, eur.begin() + 8) &&
              std::equal(us.begin() + 48, us.end(), eur.begin() + 50),
          "a 248-character code page gets U+2019 at byte 0x15 and every later character one byte up");
    Check(OtherCodePage(eur, back, usTree) && back == us, "the 249-character one made from it gives the first back");
    bool mapped = true;
    for (unsigned b = 1; b <= 248; ++b) {
        const uint16_t u = static_cast<uint16_t>(us[8 + 2 * (b - 1)] | (us[9 + 2 * (b - 1)] << 8));
        mapped = mapped && Locate(usTree, u) == b && Locate(eurTree, u) == (b < 21 ? b : b + 1);
    }
    Check(mapped && Locate(eurTree, 0x2019) == 21 && Locate(usTree, 0x2019) == 0 && Locate(usTree, 0x60) == 0 &&
              Locate(usTree, 0x700) == 0 && usTree.size() == 4 + 2 * (3 * 4 + 248) && eurTree.size() == 4 + 2 * (4 * 4 + 249),
          "each UniToMB.dat range tree gives every character its byte and nothing to the others");
    Bytes none, noneTree;
    Check(!OtherCodePage(IdentityCodePage(), none, noneTree) && !OtherCodePage(Bytes{'B', 'F', 'M', 'U'}, none, noneTree),
          "a code page of another size is not one of the editions'");

    fs::remove_all(root, ec);
    std::printf("unofficial_language: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
