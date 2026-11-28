// host_unofficial_language.cpp: a player's own translation dropped on the importer (launcher/ui/unofficial_language.h),
// with synthetic text tables and fonts made here: a sound one is kept apart from the game as its own language, said in the
// result's lines and not counted as unknown files; a damaged copy is not offered; a glyph that ends early, as the
// editions' own fonts do, is taken; the same translation dropped again is kept and a changed one replaces it; and a
// table far off the game's line count, a bad string offset, an unterminated string, a damaged glyph, a letter past a
// font's last glyph or an unknown code page are refused, never written over the game folder. A translation is named by its text's letters
// (Russian, Ukrainian, Belarusian, Bulgarian, Polish, Czech, Hungarian, Romanian, Greek, Turkish; Latin-1's own
// languages, mixed, unaccented Greek and Serbian text is not named; English text is another pressing's game text,
// neither added nor asked about, and said so), its English and American folders with the same text are one
// translation, and one not named is named by the player once: dropped again, it is found under that name. It is found
// by its text tables wherever they are (data/Language, language folders on their own, a folder of any name, nested, a
// zip's or RAR's root, folders named outside ASCII), and a program in the drop is named, never run. A few letters a font draws as nothing are said with the translation, naming the font, the
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

#include "test_archives.h"
#include "test_installshield.h"
#include "importer.h"
#include "texts.h"
#include "unofficial_language.h"

#ifdef _WIN32
extern "C" __declspec(dllimport) unsigned int __stdcall GetACP(void);   // as platform/path_text.h
#endif

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

// A language folder of the text `text`, with every general font the game's index uses, each covering all 255 glyphs.
void WriteLanguage(const fs::path& language, const std::string& text) {
    Write(language / "UITEXT.str", TextTable(text));
    const std::string prefix = "data/Language/English/";
    for (size_t i = 0; i < ManifestRecordCount(); ++i) {
        const std::string dest = ManifestRecords()[i].dest;
        if (dest.compare(0, prefix.size(), prefix) != 0 || dest.size() < 4 || dest.substr(dest.size() - 4) != ".bf4") continue;
        Write(language / dest.substr(prefix.size()), Font(255));
    }
}

// A pack of the text `text` in data/Language/<folder>/.
void WritePack(const fs::path& pack, const std::string& text, const char* folder = "English") {
    WriteLanguage(pack / "data/Language" / folder, text);
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
    Check(report.translatedOnly == std::vector<std::string>{"data/Language/English/UITEXT.str"},
          "the game file its table stands for is in the drop only in translation");
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

    // The contents of a data folder hold its translation as the folder around them does: this text replaces the one kept.
    const fs::path flat = root / "flat";
    Write(flat / "Language/English/UITEXT.str", Table());
    report = importer.Import({flat.string()});
    Check(report.errors.empty() && report.written == 2 && report.translations.size() == 1 &&
              report.translations.front() == "English (unofficial translation) updated: text" &&
              Find(importer.Languages()),
          "a data folder's own Language folder is a translation");

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
        // Latin text read as Windows-1250 (Central European) and 1254 (Turkish), Greek as 1253.
        {"Polish",
         "Witaj w swoim parku! Zbuduj karuzel\xEA, kt\xF3r\xB9 lubi\xB9 go\x9C" "cie, i zadbaj o ich "
         "szcz\xEA\x9C" "cie. \xA3\xF3" "d\x9F p\xB3ynie, \xBF\xF3\xB3w \x9Cpi, ma\xB3" "a \xB3" "awka."},
        {"Czech",
         "V\xEDtejte ve sv\xE9m parku! Postavte atrakce, kter\xE9 maj\xED n\xE1v\x9At\xECvn\xED" "ci "
         "r\xE1" "di, a dbejte na jejich \x9At\xECst\xED. \xD8" "eka te\xE8" "e p\xF8" "es d\xF9m, "
         "t\xF8i m\xECsta."},
        {"Hungarian",
         "\xDC" "dv\xF6z\xF6lj\xFCk a parkj\xE1" "ban! \xC9p\xEDtsen k\xF6rhint\xE1t, amelyet a l\xE1togat\xF3k "
         "szeretnek, \xE9s \xF5rizze meg a j\xF3kedv\xFCket. \xD5 \xFBrhaj\xF3s, \xF6r\xF6m, ny\xF5, "
         "f\xFB."},
        {"Romanian",
         "Bun venit \xEEn parcul t\xE3u! Construie\xBAte atrac\xFEii pe care vizitatorii le \xEEndr\xE3gesc "
         "\xBAi \xFEine-i ferici\xFEi. A\xBA" "a e m\xE2ine, cas\xE3 mare."},
        {"Greek",
         "\xCA\xE1\xEB\xFE\xF2 \xDE\xF1\xE8\xE1\xF4\xE5 \xF3\xF4\xEF \xF0\xDC\xF1\xEA\xEF \xF3\xE1\xF2! "
         "\xD7\xF4\xDF\xF3\xF4\xE5 \xF4\xE1 \xF0\xE1\xE9\xF7\xED\xDF\xE4\xE9\xE1 \xF0\xEF\xF5 \xE1\xE3\xE1\xF0\xEF\xFD\xED "
         "\xEF\xE9 \xE5\xF0\xE9\xF3\xEA\xDD\xF0\xF4\xE5\xF2 \xEA\xE1\xE9 \xEA\xF1\xE1\xF4\xDE\xF3\xF4\xE5 "
         "\xF4\xEF\xF5\xF2 \xF7\xE1\xF1\xEF\xFD\xEC\xE5\xED\xEF\xF5\xF2."},
        {"Turkish",
         "Park\xFDn\xFDza ho\xFE geldiniz! Ziyaret\xE7ilerin sevdi\xF0i oyuncaklar\xFD kurun ve onlar\xFD "
         "mutlu tutun. I\xFE\xFDkl\xFD a\xF0" "a\xE7, \xDDstanbul, k\xFD\xFE\xFDn."},
    };
    for (const auto& letters : kLetters) {
        const fs::path pack = root / letters.first;
        WritePack(pack, letters.second);
        report = named.Import({pack.string()});
        language = Find(named.Languages(), std::string("Unofficial-") + letters.first);
        Check(report.errors.empty() && report.translations.size() == 1 &&
                  report.translations.front() == std::string(letters.first) + " (unofficial translation) added: text, fonts" &&
                  language && language->description == std::string("Its text reads as ") + letters.first,
              std::string("text with ") + letters.first + "'s own letters is " + letters.first);
    }
    // Serbian and Macedonian share their letters, Latin text of no letters but A to Z names no language, and a mix is not
    // sure: none is named.
    const std::pair<const char*, std::string> kUnnamed[] = {
        {"serbian",
         "\xC4\xEE\xE1\xF0\xEE\xE4\xEE\xF8\xEB\xE8 \xF3 \xE2\xE0\xF8 \xEF\xE0\xF0\xEA, \x9A\xF3\xE1\xE0\xE2 \xE8 \x9F\xE5\xEC "
         "\xF1\xF3 \xF2\xF3."},
        {"latin", "Bem-vindo ao seu parque, divirta-se!"},
        {"mixed", "Park \xEF\xE0\xF0\xEA Welcome to your park."},
        // Greek capitals without accents are not sure (and not Cyrillic, whose a they lack); Latin-1's own languages,
        // with letters Windows-1250 and 1254 have elsewhere, are none of theirs.
        {"greekcaps",
         "\xCA\xC1\xCB\xD9\xD3 \xC7\xD1\xC8\xC1\xD4\xC5 \xD3\xD4\xCF \xD0\xC1\xD1\xCA\xCF \xD3\xC1\xD3! "
         "\xD7\xD4\xC9\xD3\xD4\xC5 \xD4\xC1 \xD0\xC1\xC9\xD7\xCD\xC9\xC4\xC9\xC1."},
        {"french",
         "Bienvenue dans votre parc ! Construisez les man\xE8ges que vos visiteurs aiment, \xE0 c\xF4t\xE9 "
         "du ch\xE2teau, s\xFBr."},
        {"portuguese",
         "Bem-vindo ao seu parque! Construa as atra\xE7\xF5" "es que os visitantes adoram, n\xE3o "
         "\xE9? A\xE7\xE3o, emo\xE7\xE3o, cora\xE7\xE3o, 1\xBA lugar."},
        {"spanish",
         "\xBF" "D\xF3nde est\xE1 el parque? \xA1" "Construya atracciones para los ni\xF1os, se\xF1or!"},
        {"icelandic",
         "Velkomin \xED gar\xF0inn \xFEinn! Bygg\xF0u t\xE6ki sem gestirnir elska, \xFEv\xED \xFE" "a\xF0 "
         "er gaman, \xFD\xFDr."},
        {"danish",
         "Velkommen til din park! Byg forlystelser, som g\xE6sterne elsker, og hold dem glade p\xE5 "
         "\xF8" "en, f\xF8r."},
        {"italian",
         "Benvenuti nel vostro parco! Costruite le giostre che piacer\xE0 ai visitatori, cos\xEC l\xEC, "
         "pi\xF9 su, perch\xE9."},
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
    // English or the American folder it is neither added nor asked about, and its files are left to the edition. The
    // result says so (tpw-port #128), naming the folder.
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
        Check(report.notTranslations.size() == 1 &&
                  report.notTranslations.front().find(std::string("The text files in data/Language/") + folder +
                                                      " are in English") != std::string::npos,
              std::string("English text in the ") + folder + " folder is said as not a translation: " +
                  (report.notTranslations.empty() ? std::string("nothing said") : report.notTranslations.front()));
        Check(report.Summary().find(" are in English") != std::string::npos,
              std::string("the import's summary says why the ") + folder + " folder is not a translation: " +
                  report.Summary());
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

    // A translation's language folders copied out of the game folder (tpw-port #128: a zip of American and English, as
    // the Russian Project's text was passed on) are found as in data/Language, one translation, its files claimed; so
    // are a Languages folder of them, a data folder's contents with its Init pictures, one language folder dropped by
    // itself, and text tables in a folder of any name, at any depth. A folder with a language's name but no text table
    // is not one.
    struct Layout {
        const char* name;
        std::vector<std::string> folders;
    };
    const Layout kLayouts[] = {
        {"top", {"American", "English"}},
        {"languages", {"Languages/American", "Languages/English"}},
        {"data", {"Language/English"}},
        {"alone", {""}},
        // By its text, not its folders' names (tpw-port #128): a folder of any name, nested ones, and a language folder
        // of a name the game has not in the game folder's data/Language.
        {"named", {"Russian"}},
        {"nested", {"Mods/Translations/RU v2/American", "Mods/Translations/RU v2/English"}},
        {"game", {"data/Language/Russian"}},
    };
    for (const Layout& layout : kLayouts) {
        const fs::path drop = root / "loose" / layout.name, looseData = root / "loose-data" / layout.name;
        for (const std::string& folder : layout.folders) WriteLanguage(drop / folder, russianText);
        if (std::string(layout.name) == "data") Write(drop / "Init/Loading.tga", {'t', 'g', 'a'});
        ImporterConfig looseConfig;
        looseConfig.dataRoot = looseData.string();
        ConfigureUnofficialLanguages(looseConfig);
        Importer loose(looseConfig);
        Check(loose.Open(err), "the importer opens: " + err);
        Write(looseData / "assets/eur/Theme Park Data/data/Language/English/MBToUni.dat", IdentityCodePage());
        report = loose.Import({drop.string()});
        Check(report.errors.empty() && report.translations.size() == 1 &&
                  report.translations.front() == "Russian (unofficial translation) added: text, fonts" &&
                  Unofficial(loose.Languages()) == 1 && Find(loose.Languages(), "Russian") && report.unknownFiles == 0 &&
                  fs::exists(looseData / "assets/lang/Russian/data/Language/American/uitext.str") &&
                  fs::exists(looseData / "assets/lang/Russian/data/Language/English/uitext.str"),
              std::string("a translation's language folders outside data/Language are found (") + layout.name + ")" +
                  (report.errors.empty() ? std::string() : ": " + report.errors.front()));
        Check(std::string(layout.name) != "data" || fs::exists(looseData / "assets/lang/Russian/data/init/loading.tga"),
              "a data folder's Init pictures are found with its Language folder");
    }
    const fs::path fontsOnly = root / "loose/fonts", fontsData = root / "loose-data/fonts";
    WriteLanguage(fontsOnly / "American", russianText);
    fs::remove(fontsOnly / "American/UITEXT.str");
    ImporterConfig fontsConfig;
    fontsConfig.dataRoot = fontsData.string();
    ConfigureUnofficialLanguages(fontsConfig);
    Importer fontsImporter(fontsConfig);
    Check(fontsImporter.Open(err), "the importer opens: " + err);
    report = fontsImporter.Import({fontsOnly.string()});
    Check(report.errors.empty() && report.translations.empty() && Unofficial(fontsImporter.Languages()) == 0,
          "a folder named for a language without a text table is not a translation");

    // In any packing the importer opens (tpw-port #128): a zip or a RAR (7z is read as RAR is, by libarchive) of a
    // translation's language folders, at the archive's root, is found as a folder of them is. A program, such as a
    // translation's own installer, is never run: dropped by itself or inside an archive, it is named for the result.
    const fs::path packed = root / "packed/files";
    for (const char* folder : {"American", "English"}) WriteLanguage(packed / folder, russianText);
    std::vector<tpwtest::ZipMember> zipMembers;
    std::vector<test::RarMember> rarMembers;
    for (const auto& file : fs::recursive_directory_iterator(packed)) {
        if (!file.is_regular_file()) continue;
        std::ifstream in(file.path(), std::ios::binary);
        const std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        const std::string name = fs::relative(file.path(), packed).generic_string();
        std::string rarName = name;
        std::replace(rarName.begin(), rarName.end(), '/', '\\');
        zipMembers.push_back({name, bytes});
        rarMembers.push_back({rarName, bytes});
    }
    const auto writeBytes = [](const fs::path& p, const std::string& bytes) { Write(p, Bytes(bytes.begin(), bytes.end())); };
    writeBytes(root / "packed/Languages.zip", tpwtest::ZipArchiveOf(zipMembers));
    writeBytes(root / "packed/Languages.rar", test::RarOf(rarMembers)[0]);
    std::string program = "MZ";
    program.resize(4096, '\x90');
    writeBytes(root / "packed/RussianSetup.exe", program);
    writeBytes(root / "packed/Installer.zip", tpwtest::ZipArchiveOf({{"Russian/Setup.exe", program}}));
    const auto packedImport = [&](const char* name) {
        const fs::path packedData = root / "packed-data" / name;
        ImporterConfig packedConfig;
        packedConfig.dataRoot = packedData.string();
        ConfigureUnofficialLanguages(packedConfig);
        Importer packedImporter(packedConfig);
        Check(packedImporter.Open(err), "the importer opens: " + err);
        Write(packedData / "assets/eur/Theme Park Data/data/Language/English/MBToUni.dat", IdentityCodePage());
        ImportReport result = packedImporter.Import({(root / "packed" / name).string()});
        Check(Unofficial(packedImporter.Languages()) == (result.translations.empty() ? 0u : 1u),
              std::string(name) + ": the languages offered are the ones added");
        return result;
    };
    for (const char* archive : {"Languages.zip", "Languages.rar"}) {
        report = packedImport(archive);
        Check(report.errors.empty() && report.translations.size() == 1 &&
                  report.translations.front() == "Russian (unofficial translation) added: text, fonts" &&
                  report.unknownFiles == 0 && report.programs.empty() &&
                  fs::exists(root / "packed-data" / archive / "assets/lang/Russian/data/Language/American/uitext.str"),
              std::string("a translation's language folders at an archive's root are found (") + archive + ")" +
                  (report.errors.empty() ? std::string() : ": " + report.errors.front()));
    }
    report = packedImport("RussianSetup.exe");
    Check(report.translations.empty() && report.written == 0 &&
              report.programs == std::vector<std::string>{"RussianSetup.exe"},
          "a program dropped by itself is named, not run");
    Check(report.Summary().find("RussianSetup.exe is a program: not run, unpack it first.") != std::string::npos,
          "the import's summary names the program: " + report.Summary());
    report = packedImport("Installer.zip");
    Check(report.translations.empty() && report.written == 0 && report.programs.size() == 1 &&
              report.programs.front().find("Setup.exe") != std::string::npos,
          "a program inside an archive is named, not run");

    // Folders and files named outside ASCII (a Cyrillic and an accented folder around the language folders, a Cyrillic
    // readme and installer beside them): in a zip, whose names are UTF-8, the translation is found and the program and
    // the not-a-translation note name them as they are. Dropped as a folder, the same is found wherever narrow paths
    // hold those names (platform/path_text.h); on a Windows whose code page has not their letters (this test has not the
    // launcher's UTF-8 manifest) the drop is refused cleanly, never thrown or half taken.
    const std::string cyrillic = "\xD0\x9F\xD0\xB5\xD1\x80\xD0\xB5\xD0\xB2\xD0\xBE\xD0\xB4",              // Perevod
        nested = cyrillic + "/Traducci\xC3\xB3n",
        readme = cyrillic + "/\xD0\x9F\xD1\x80\xD0\xBE\xD1\x87\xD1\x82\xD0\xB8.txt",                           // Prochti
        installer = cyrillic + "/\xD0\xA3\xD1\x81\xD1\x82\xD0\xB0\xD0\xBD\xD0\xBE\xD0\xB2\xD0\xBA\xD0\xB0.exe";  // Ustanovka
    std::vector<tpwtest::ZipMember> namedMembers, englishMembers;
    for (const tpwtest::ZipMember& member : zipMembers) namedMembers.push_back({nested + "/" + member.name, member.data});
    namedMembers.push_back({readme, "readme"});
    namedMembers.push_back({installer, program});
    writeBytes(root / "packed/Named.zip", tpwtest::ZipArchiveOf(namedMembers));
    const fs::path englishFiles = root / "packed/english-files";
    WriteLanguage(englishFiles / "American", "Welcome to your park! Build the rides you like and keep your visitors happy.");
    for (const auto& file : fs::recursive_directory_iterator(englishFiles)) {
        if (!file.is_regular_file()) continue;
        std::ifstream in(file.path(), std::ios::binary);
        englishMembers.push_back({cyrillic + "/" + fs::relative(file.path(), englishFiles).generic_string(),
                                  std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()}});
    }
    writeBytes(root / "packed/English.zip", tpwtest::ZipArchiveOf(englishMembers));
    const fs::path namedFolder = root / "packed/named-folder";
    for (const tpwtest::ZipMember& member : namedMembers) writeBytes(namedFolder / fs::u8path(member.name), member.data);
    const auto foundNamed = [&](const ImportReport& r, const char* name) {
        Check(r.errors.empty() && r.translations.size() == 1 &&
                  r.translations.front() == "Russian (unofficial translation) added: text, fonts" &&
                  fs::exists(root / "packed-data" / name / "assets/lang/Russian/data/Language/American/uitext.str") &&
                  fs::exists(root / "packed-data" / name / "assets/lang/Russian/data/Language/English/uitext.str"),
              std::string("a translation in folders named outside ASCII is found (") + name + ")" +
                  (r.errors.empty() ? std::string() : ": " + r.errors.front()));
        Check(r.programs.size() == 1 && r.programs.front().find(installer.substr(cyrillic.size() + 1)) != std::string::npos,
              std::string("a program named outside ASCII is named as it is (") + name + "): " +
                  (r.programs.empty() ? std::string("none") : r.programs.front()));
    };
    foundNamed(packedImport("Named.zip"), "Named.zip");
    report = packedImport("English.zip");
    Check(report.translations.empty() && report.notTranslations.size() == 1 &&
              report.notTranslations.front().find("The text files in " + cyrillic + "/American are in English") !=
                  std::string::npos,
          "English text in a folder named outside ASCII is said as not a translation, the folder named as it is: " +
              (report.notTranslations.empty() ? std::string("nothing said") : report.notTranslations.front()));
    bool narrowNames = true;   // as the folder reading's narrow paths (PathText) and fopen take them
    for (const tpwtest::ZipMember& member : namedMembers) {
        try {
            const fs::path p = fs::u8path(member.name);
            narrowNames = narrowNames && fs::path(p.string()) == p;
        } catch (...) {
            narrowNames = false;
        }
    }
#ifdef _WIN32
    narrowNames = narrowNames || GetACP() == 65001;
#endif
    report = packedImport("named-folder");
    if (narrowNames)
        foundNamed(report, "named-folder");
    else
        Check(report.translations.empty() && report.written == 0 &&
                  !fs::exists(root / "packed-data/named-folder/assets/lang/Russian"),
              "a folder of names the code page has not is refused cleanly");

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
