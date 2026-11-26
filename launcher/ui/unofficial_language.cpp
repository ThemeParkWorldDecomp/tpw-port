#include "unofficial_language.h"
#ifdef TPW_PORT_FIX
// TPW_PORT_FIX packlanguage ImportUnofficialLanguages | Mac: the installer replaces a language in the game folder | Port: validate a supplied translation and keep it as a separate selectable overlay | Why: a translation can be tried and switched off without changing the game files
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include "path_text.h"
#include "texts.h"

namespace tpw::assets {
namespace {
namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;
constexpr uint64_t kSmallLimit = 16ull << 20;
constexpr uint64_t kPackLimit = 8ull << 30;
std::string Lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}
uint16_t U16(const Bytes& b, size_t p) {
    return b[p] | (uint16_t(b[p + 1]) << 8);
}
uint32_t U32(const Bytes& b, size_t p) {
    return b[p] | (uint32_t(b[p + 1]) << 8) | (uint32_t(b[p + 2]) << 16) | (uint32_t(b[p + 3]) << 24);
}
bool SafePath(const std::string& s) {
    if (s.empty() || s[0] == '/' || s.find('\\') != std::string::npos || s.find(':') != std::string::npos ||
        s.find('\t') != std::string::npos || s.find('\n') != std::string::npos || s.find('\r') != std::string::npos)
        return false;
    std::istringstream in(s);
    std::string part;
    while (std::getline(in, part, '/'))
        if (part.empty() || part == "." || part == "..") return false;
    return true;
}
std::string OriginalName(const std::string& s) {
    for (size_t i = 0; i < ManifestLanguageCount(); ++i)
        if (Lower(ManifestLanguages()[i].name) == Lower(s)) return ManifestLanguages()[i].name;
    return {};
}
bool Read(Source& source, const Entry& e, Bytes& bytes, std::string& err) {
    auto stream = source.Open(e, err);
    return stream && ReadAll(*stream, kSmallLimit, bytes, err);
}
bool Official(const Bytes& bytes) {
    const auto hash = Sha256::Of(bytes.data(), bytes.size());
    return !FindByContent(bytes.size(), hash.bytes).empty();
}
// Where each byte of a translation's text is used: by table (its name in lower case), the first lines it is on (numbered
// from 1) and how many.
struct Uses {
    std::vector<uint32_t> lines;
    uint32_t count = 0;
};
using Where = std::map<uint8_t, std::map<std::string, Uses>>;
// Resource IDs and table lengths are format metadata. The two older lengths are repaired by old_text_tables.cpp.
struct Table {
    const char* file;
    uint32_t id, count;
};
constexpr Table kTables[] = {
    {"errormsg.str", 1000, 89},         {"ingredient.str", 1001, 4},         {"tag_system.str", 1002, 332},
    {"thoughts.str", 1003, 18},         {"kidstates.str", 1004, 22},         {"staffstates.str", 1005, 6},
    {"uitext.str", 1006, 474},          {"uihelptext.str", 1007, 589},       {"chat_commands.str", 1008, 182},
    {"themenames.str", 1009, 4},        {"object_names.str", 1010, 374},     {"handyman_names.str", 1011, 35},
    {"mechanic_names.str", 1012, 35},   {"entertainer_names.str", 1013, 35}, {"guard_names.str", 1014, 35},
    {"researcher_names.str", 1015, 35}, {"female_names.str", 1016, 21},      {"itemtypes.str", 1017, 26},
    {"loannames.str", 1018, 18},        {"staff_types.str", 1019, 10},       {"keyboard.str", 1020, 21},
};
bool CheckTable(const std::string& file, const Bytes& b, std::set<uint8_t>& glyphs, std::array<uint64_t, 256>& letters,
                Where& where, std::string& err) {
    const Table* t = nullptr;
    for (const Table& known : kTables)
        if (known.file == file) t = &known;
    if (!t || b.size() < 12 || std::memcmp(b.data(), "BFST", 4) || U32(b, 4) != t->id) {
        err = file + ": not a supported game text table";
        return false;
    }
    const uint32_t count = U32(b, 8);
    if (count != t->count && !(t->id == 1006 && count == 473) && !(t->id == 1008 && count == 176)) {
        err = file + ": its line count differs from the game's (" + std::to_string(count) + ")";
        return false;
    }
    const size_t data = 12, first = size_t(count) * 4;
    if (first > b.size() - data) {
        err = file + ": truncated offset table";
        return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t offset = U32(b, data + i * 4);
        size_t at = data + offset;
        if (offset < first || (offset & 3) || at >= b.size()) {
            err = file + ": invalid string offset";
            return false;
        }
        bool ended = false;
        std::array<bool, 256> onLine{};
        while (at + 4 <= b.size()) {
            const uint32_t word = U32(b, at), op = word & 255, n = word >> 8;
            at += 4;
            if (op == 0) {
                ended = true;
                break;
            }
            if (op > 6) break;
            const uint64_t size = op == 1 ? (uint64_t(n) + 3) & ~uint64_t(3) : op == 3 ? uint64_t(n) * 4 : 0;
            if (size > b.size() - at) break;
            if (op == 1) {
                for (uint32_t j = 0; j < n; ++j) {
                    const uint8_t c = b[at + j];
                    ++letters[c];
                    if (c < 32) continue;
                    glyphs.insert(c);
                    if (onLine[c]) continue;
                    onLine[c] = true;
                    Uses& uses = where[c][file];
                    if (uses.lines.size() < 4) uses.lines.push_back(i + 1);
                    ++uses.count;
                }
            }
            at += static_cast<size_t>(size);
        }
        if (!ended) {
            err = file + ": a string is damaged or unterminated";
            return false;
        }
    }
    return true;
}
// A sound font, and in `blank` the bytes of the text (`glyphs`) whose glyph in it is empty, for a font that draws text.
bool CheckFont(const std::string& file, const Bytes& b, const std::set<uint8_t>& glyphs, bool coverage,
               std::set<uint8_t>& blank, std::string& err) {
    if (b.size() < 8 || std::memcmp(b.data(), "F4FB", 4) || b[5] == 0) {
        err = file + ": damaged font";
        return false;
    }
    const uint16_t count = U16(b, 6);
    if (!count || size_t(count) * 4 > b.size() - 8) {
        err = file + ": truncated font";
        return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t offset = U32(b, 8 + i * 4);
        if (offset < 8 + uint32_t(count) * 4 || offset > b.size() || b.size() - offset < 24) {
            err = file + ": invalid glyph offset";
            return false;
        }
        const uint32_t stored = U32(b, offset + 4), unpacked = U32(b, offset + 8), flags = U32(b, offset + 12);
        const uint64_t pixels = uint64_t(U16(b, offset + 16)) * U16(b, offset + 18);
        if (stored > b.size() - offset - 24 || unpacked > kSmallLimit || pixels > uint64_t(unpacked) * 2 || flags > 3) {
            err = file + ": damaged glyph data";
            return false;
        }
        if (flags & 1) {
            size_t nibble = 0;
            uint64_t produced = 0;
            bool ended = false;
            const auto next = [&]() -> unsigned {
                const uint8_t byte = b[offset + 24 + nibble / 2];
                return (nibble++ & 1) ? byte & 15 : byte >> 4;
            };
            while (nibble < uint64_t(stored) * 2) {
                if (next())
                    ++produced;
                else {
                    if (nibble >= uint64_t(stored) * 2) break;
                    const unsigned run = next();
                    if (!run) {
                        ended = true;
                        break;
                    }
                    if (nibble >= uint64_t(stored) * 2) break;
                    next();
                    produced += run;
                }
                if (produced > uint64_t(unpacked) * 2) break;
            }
            // As the game's decoder (Tb4BitFontRenderMethod::DrawChar): the end mark within the data, no more pixels than
            // the glyph's buffer holds. Fewer than width x height is how the editions' own fonts end some glyphs early
            // (the US DATEBIG.bf4 has ten such), so it is not damage.
            if (!ended || produced > uint64_t(unpacked) * 2) {
                err = file + ": damaged compressed glyph";
                return false;
            }
        } else if (flags & 2) {
            if (uint64_t(stored) * 8 < uint64_t(unpacked) * 2) {
                err = file + ": truncated one-bit glyph";
                return false;
            }
        } else if (stored < unpacked) {
            err = file + ": truncated glyph pixels";
            return false;
        }
        if (coverage && glyphs.count(static_cast<uint8_t>(i + 1)) && i < 255 && i + 1 > 32 &&
            (pixels == 0 || U16(b, offset + 22) == 0))
            blank.insert(static_cast<uint8_t>(i + 1));
    }
    return true;
}
bool GeneralFont(const std::string& name) {
    return name.size() > 4 && name.substr(name.size() - 4) == ".bf4" && name.compare(0, 4, "date") != 0 &&
           name.compare(0, 4, "cash") != 0 && name != "matisse36.bf4";
}
struct Candidate {
    std::string prefix, language;
    std::map<std::string, const Entry*> text;
};
// Whose own file `bytes` is: "the US edition's" or "the European edition's", when it is one game's only; else empty.
std::string Origin(const Bytes& bytes) {
    const auto hash = Sha256::Of(bytes.data(), bytes.size());
    bool us = false, eur = false;
    for (const ManifestRecord* r : FindByContent(bytes.size(), hash.bytes)) {
        us = us || (r->editions & (kEditionUs | kEditionUsPc));
        eur = eur || (r->editions & (kEditionEur | kEditionEurPc));
    }
    return us == eur ? "" : us ? "the US edition's" : "the European edition's";
}
// An edition's fonts have as many glyphs as its code page (MBToUni.dat) has characters: the US edition's 248, the
// European edition's 249, which adds a character before the letters, so a letter has another byte in each.
std::string CodePageOf(uint16_t characters) {
    return characters == 248 ? "the US edition's" : characters == 249 ? "the European edition's" : "";
}
std::string FileName(const Entry& e) {
    return e.path.substr(e.path.find_last_of('/') + 1);
}
std::string Listed(const std::vector<std::string>& names) {
    std::string out;
    for (size_t i = 0; i < names.size(); ++i)
        out += (i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + names[i];
    return out;
}
std::string FontsText(const std::vector<std::string>& fonts) {
    return fonts.size() > 3 ? fonts.front() + " and " + std::to_string(fonts.size() - 1) + " other fonts" : Listed(fonts);
}
// A byte of the text for the player: the byte, and the letter its code page `map` gives it, in quotes.
std::string Letter(uint8_t byte, const Bytes& map) {
    char hex[8];
    std::snprintf(hex, sizeof hex, "0x%02X", byte);
    const std::string number = std::string("byte ") + hex;
    if (map.size() < 8 || std::memcmp(map.data(), "BFMU", 4) || byte > U16(map, 6) || map.size() < 8u + 2u * byte)
        return number;
    const uint16_t u = U16(map, 8 + 2 * (byte - 1));
    if (!((u > 0x20 && u < 0x7f) || (u > 0xa0 && u <= 0xff))) return number;
    const std::string letter =
        u < 0x80 ? std::string(1, char(u)) : std::string{char(0xc0 | (u >> 6)), char(0x80 | (u & 0x3f))};
    return number + " (\"" + letter + "\")";
}
// The tables and lines that use `byte`: "UITEXT.str lines 3 and 10 and ERRORMSG.str line 4".
std::string Used(uint8_t byte, const Where& where, const Candidate& c) {
    std::vector<std::string> tables;
    const auto it = where.find(byte);
    if (it == where.end()) return {};
    for (const auto& table : it->second) {
        std::vector<std::string> lines;
        for (uint32_t line : table.second.lines) lines.push_back(std::to_string(line));
        if (table.second.count > table.second.lines.size())
            lines.push_back(std::to_string(table.second.count - table.second.lines.size()) + " others");
        tables.push_back(FileName(*c.text.at(table.first)) + (table.second.count == 1 ? " line " : " lines ") +
                         Listed(lines));
    }
    return Listed(tables);
}
// The letters of the text that fonts draw as nothing (`blank`: each byte with the text fonts whose glyph is empty, of
// `fonts` text fonts), the first three with the fonts and the lines.
std::string Undrawn(const std::map<uint8_t, std::vector<std::string>>& blank, size_t fonts, const Where& where,
                    const Candidate& c, const Bytes& map) {
    std::vector<std::string> parts;
    for (const auto& item : blank) {
        if (parts.size() == 3) {
            parts.push_back("and " + std::to_string(blank.size() - 3) + " other letters");
            break;
        }
        const std::vector<std::string>& lacking = item.second;
        parts.push_back((lacking.size() == fonts ? std::string("no font in it draws ")
                                                 : FontsText(lacking) + (lacking.size() == 1 ? " does" : " do") +
                                                       " not draw ") +
                        Letter(item.first, map) + ", used in " + Used(item.first, where, c));
    }
    std::string out;
    for (const std::string& part : parts) out += (out.empty() ? "" : "; ") + part;
    return out;
}
// The tables the pack took from the edition whose code page its text fonts are not drawn for (`fontPage`, CodePageOf
// theirs): their letters have that edition's bytes, so every one shows as another. Empty when there are none. The code
// page files used instead (`made`) are not named.
std::string OtherEdition(const Candidate& c, const std::map<std::string, Bytes>& bytes, const std::string& fontPage,
                         const std::map<std::string, Bytes>& made, Source& source) {
    if (fontPage.empty()) return {};
    std::vector<std::string> tables, pages;
    std::string origin;
    for (const auto& item : bytes) {
        const std::string from = item.first.substr(item.first.size() - 4) == ".str" ? Origin(item.second) : "";
        if (from.empty() || from == fontPage) continue;
        origin = from;
        tables.push_back(FileName(*c.text.at(item.first)));
    }
    if (tables.empty()) return {};
    for (const char* name : {"mbtouni.dat", "unitomb.dat"}) {
        const auto it = c.text.find(name);
        Bytes b;
        std::string err;
        if (it != c.text.end() && !made.count(name) && Read(source, *it->second, b, err) && Origin(b) == origin)
            pages.push_back(FileName(*it->second));
    }
    return Listed(tables) + (tables.size() == 1 ? " is " : " are ") + origin + " own, written in its character table, but " +
           "its fonts are drawn for " + fontPage + ", so every letter in " + (tables.size() == 1 ? "it" : "them") +
           " shows as another one" +
           (pages.empty() ? "" : " (its " + Listed(pages) + (pages.size() == 1 ? " is " : " are ") + origin + " too)");
}
// The code page pair of the edition `page` (CodePageOf) made from the other edition's MBToUni.dat (`map`), when both
// are, by hash, that edition's own files.
bool FittingCodePage(const Bytes& map, const std::string& page, Bytes& mbToUni, Bytes& uniToMb) {
    return !page.empty() && OtherCodePage(map, mbToUni, uniToMb) && Origin(mbToUni) == page && Origin(uniToMb) == page;
}
bool SpeechPath(const std::string& path) {
    if (path.compare(0, 19, "data/global/speech/") == 0) return true;
    if (path.compare(0, 12, "data/levels/") != 0) return false;
    const size_t world = path.find('/', 12);
    return world != std::string::npos && path.compare(world, 8, "/speech/") == 0;
}
bool SpeechFile(const std::string& path) {
    if (!SpeechPath(path)) return false;
    const std::string ext = path.substr(path.find_last_of('.') + 1);
    return ext == "sdt" || ext == "map" || ext == "lip" || path == "data/global/speech/lips.wad";
}
// Content identification only: hashes, no translation material. The Russian Project's translation in its two observed
// revisions: the first on both independently observed discs, the second in the torr.ru disc's two language folders and
// the Mir SIM disc's English one. A renamed archive alone cannot claim to be that translation.
constexpr const char* kRussianProject = "Russian (unofficial translation, 2000)";
bool RussianProject(const std::map<std::string, Bytes>& text) {
    auto hash = [&](const char* name) {
        const auto it = text.find(name);
        return it == text.end() ? std::string() : Sha256::Of(it->second.data(), it->second.size()).Hex();
    };
    const std::string uitext = hash("uitext.str"), help = hash("uihelptext.str");
    return (uitext == "1311342d8ec9816ac516b728e7b495aebf0e1243e82280bcea2dd91967b963f5" &&
            help == "a8b2058fb78bf75297408e949ac341c62cf38ef869eafe545a58b97e2fa82e00") ||
           (uitext == "8549570ad7176aa8c987a882368b6263cef42b5cc210986aad696fbd559eed32" &&
            help == "63911c59bb5fc0b369986a166d4b8fcdaeb09d3a4e02133919389c8650412d63");
}
// The language a translation's text is written in, from its letters: the count of each byte of its strings, read
// through the code page `map` (MBToUni.dat: "BFMU", the count at 6, then byte b's character at 8 + 2 * (b - 1)), a
// Latin-1 one. A fan translation for an alphabet the game has no code page for draws its own letters where Latin-1's
// are, in the Windows code page 1251's order (Cyrillic at 0xC0 to 0xFF), so text mostly of Latin-1's own letters is
// read as that code page, and a Cyrillic one is named by the letters only its language uses. Empty when not sure: Latin
// text, too little text, a mix, or a language not told apart here (Serbian and Macedonian share their letters).
std::string TextLanguage(const std::array<uint64_t, 256>& letters, const Bytes& map) {
    if (map.size() < 8 || std::memcmp(map.data(), "BFMU", 4)) return {};
    const size_t count = std::min<size_t>(U16(map, 6), (map.size() - 8) / 2);
    std::array<uint64_t, 256> page{};   // the letters as code page 1251 bytes
    uint64_t latin = 0, high = 0;
    for (size_t c = 1; c <= count && c < 256; ++c) {
        const uint16_t u = U16(map, 8 + (c - 1) * 2);
        if ((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z')) latin += letters[c];
        if (u >= 0xC0 && u <= 0xFF && u != 0xD7 && u != 0xF7) high += letters[c];
        if (u >= 0x80 && u <= 0xFF) page[u] += letters[c];
    }
    if (high <= latin) return {};
    const auto sum = [&](std::initializer_list<uint8_t> bytes) {
        uint64_t n = 0;
        for (uint8_t b : bytes) n += page[b];
        return n;
    };
    // Each in both cases: Serbian and Macedonian's own letters, Belarusian's short u, Ukrainian's four, Russian's two
    // (yery, e), the hard sign, yo, Belarusian's dotted i (and Ukrainian's), and i, which Belarusian has not.
    const uint64_t serbian = sum({0x80, 0x90, 0x81, 0x83, 0x8A, 0x9A, 0x8C, 0x9C, 0x8D, 0x9D, 0x8E, 0x9E, 0x8F, 0x9F,
                                  0xA3, 0xBC, 0xBD, 0xBE}),
                   shortU = sum({0xA1, 0xA2}), ukrainian = sum({0xA5, 0xB4, 0xAA, 0xBA, 0xAF, 0xBF, 0xB2, 0xB3}),
                   russian = sum({0xDB, 0xFB, 0xDD, 0xFD}), hard = sum({0xDA, 0xFA}), yo = sum({0xA8, 0xB8}),
                   dotted = sum({0xB2, 0xB3}), i = sum({0xC8, 0xE8});
    uint64_t cyrillic = serbian + shortU + ukrainian + yo;
    for (unsigned b = 0xC0; b <= 0xFF; ++b) cyrillic += page[b];
    // Two thirds of the letters Cyrillic, and enough of them to count.
    if (cyrillic < 200 || cyrillic * 3 < (cyrillic + latin) * 2) return {};
    const auto atLeast = [&](uint64_t n, uint64_t perThousand) { return n * 1000 >= cyrillic * perThousand; };
    if (atLeast(serbian, 10)) return {};
    if (atLeast(shortU, 5)) return i * 4 < dotted ? "Belarusian" : "";
    if (atLeast(ukrainian, 20)) return (russian + hard + yo) * 500 < cyrillic ? "Ukrainian" : "";
    if (atLeast(russian, 5) && !atLeast(ukrainian, 5)) return "Russian";
    if (!atLeast(russian, 1) && atLeast(hard, 10)) return "Bulgarian";
    return {};
}
// Whether text tables (`tables`, checked by CheckTable, their strings read through the code page `map` as TextLanguage
// reads them) are English, the game's own language: almost only the letters A to Z (at most one other letter in 500;
// the editions' own English has one in 60000) and at least one word in ten of the commonest English ones (the editions'
// own English has more than one in five, their Dutch, Italian and Polish fewer than one in 200).
bool EnglishText(const std::map<std::string, Bytes>& tables, const Bytes& map) {
    static const std::set<std::string> kCommon = {"the",  "of",   "to",   "and",  "you", "your", "this", "with",
                                                  "for",  "have", "will", "are",  "be",  "it",   "not",  "can"};
    const size_t count =
        map.size() >= 8 && !std::memcmp(map.data(), "BFMU", 4) ? std::min<size_t>(U16(map, 6), (map.size() - 8) / 2) : 0;
    uint64_t latin = 0, other = 0, words = 0, common = 0;
    std::string word;
    const auto endWord = [&] {
        if (word.empty()) return;
        ++words;
        common += kCommon.count(word);
        word.clear();
    };
    for (const auto& table : tables) {
        const Bytes& b = table.second;
        if (b.size() < 12) continue;
        const uint32_t lines = U32(b, 8);
        for (uint32_t i = 0; i < lines && 16 + size_t(i) * 4 <= b.size(); ++i) {
            size_t at = 12 + size_t(U32(b, 12 + size_t(i) * 4));
            while (at + 4 <= b.size()) {
                const uint32_t w = U32(b, at), op = w & 255, n = w >> 8;
                at += 4;
                if (op == 0 || op > 6) break;
                if (op == 3) at += size_t(n) * 4;
                if (op != 1) continue;
                for (uint32_t j = 0; j < n && at + j < b.size(); ++j) {
                    const uint8_t c = b[at + j];
                    const uint16_t u = !count ? c : c && c <= count ? U16(map, 8 + 2 * (c - 1)) : 0;
                    if ((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z')) {
                        ++latin;
                        word += static_cast<char>(u | 0x20);
                        continue;
                    }
                    endWord();
                    // Letters beyond A to Z: Latin-1's and Latin Extended's, and every alphabet past them; not the
                    // punctuation and signs between (U+2019, the euro, the trade mark).
                    if ((u >= 0xC0 && u <= 0x24F && u != 0xD7 && u != 0xF7) || (u >= 0x370 && u < 0x2000) || u >= 0x3000)
                        ++other;
                }
                endWord();
                at += (size_t(n) + 3) & ~size_t(3);
            }
        }
    }
    return words >= 100 && other * 500 <= latin && common * 10 >= words;
}
// The result line's name for a kept translation: a known release by its description, any other by its language.
std::string Label(const std::string& id, const std::string& description) {
    if (description == kRussianProject) return description;
    return (id.compare(0, 11, "Unofficial-") == 0 ? id.substr(11) : id) + " (unofficial translation)";
}
// An inventory's file lines (after its five header lines): two translations with the same are the same files.
std::string InventoryFiles(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::string line, out;
    for (int i = 0; i < 5 && std::getline(in, line); ++i) {
    }
    while (std::getline(in, line)) out += line + '\n';
    return out;
}
// Lower case for ASCII and the Cyrillic capitals (UTF-8), for matching a dropped file's name.
std::string FoldName(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const unsigned char next = i + 1 < s.size() ? static_cast<unsigned char>(s[i + 1]) : 0;
        if (c >= 'A' && c <= 'Z')
            out += static_cast<char>(c + 32);
        else if (c == 0xD0 && next >= 0x80 && next <= 0x8F) {   // U+0400..040F to U+0450..045F
            out += '\xD1';
            out += static_cast<char>(next + 0x10);
            ++i;
        } else if (c == 0xD0 && next >= 0x90 && next <= 0x9F) {   // U+0410..041F to U+0430..043F
            out += '\xD0';
            out += static_cast<char>(next + 0x20);
            ++i;
        } else if (c == 0xD0 && next >= 0xA0 && next <= 0xAF) {   // U+0420..042F to U+0440..044F
            out += '\xD1';
            out += static_cast<char>(next - 0x20);
            ++i;
        } else if (c == 0xD2 && next == 0x90) {   // Ukrainian ghe with upturn
            out += "\xD2\x91";
            ++i;
        } else
            out += static_cast<char>(c);
    }
    return out;
}
// Russian, or Unofficial- and a language's English name in letters: the IDs a kept translation's folder may have.
bool TranslationId(const std::string& id) {
    if (id == "Russian") return true;
    if (id.compare(0, 11, "Unofficial-") != 0 || id.size() == 11 || id.size() > 40) return false;
    return std::all_of(id.begin() + 11, id.end(), [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); });
}
std::string ReadLine(std::istream& in) {
    std::string s;
    std::getline(in, s);
    return s;
}
}  // namespace

bool OtherCodePage(const std::vector<uint8_t>& map, std::vector<uint8_t>& mbToUni, std::vector<uint8_t>& uniToMb) {
    if (map.size() < 8 || std::memcmp(map.data(), "BFMU", 4)) return false;
    const size_t count = U16(map, 6);
    if (map.size() < 8 + 2 * count) return false;
    std::vector<uint16_t> characters;
    for (size_t i = 0; i < count; ++i) characters.push_back(U16(map, 8 + 2 * i));
    if (count == 249 && characters[20] == 0x2019)
        characters.erase(characters.begin() + 20);
    else if (count == 248)
        characters.insert(characters.begin() + 20, 0x2019);
    else
        return false;
    const auto put = [](Bytes& b, uint16_t v) {
        b.push_back(static_cast<uint8_t>(v));
        b.push_back(static_cast<uint8_t>(v >> 8));
    };
    mbToUni.assign(map.begin(), map.begin() + 6);
    put(mbToUni, static_cast<uint16_t>(characters.size()));
    for (uint16_t u : characters) put(mbToUni, u);
    std::map<uint16_t, uint16_t> bytes;   // each character's byte, the first that has it
    for (size_t i = 0; i < characters.size(); ++i) bytes.emplace(characters[i], static_cast<uint16_t>(i + 1));
    std::vector<std::pair<uint16_t, uint16_t>> runs;
    for (const auto& item : bytes) {
        if (!runs.empty() && runs.back().second + 1 == item.first)
            runs.back().second = item.first;
        else
            runs.emplace_back(item.first, item.first);
    }
    std::vector<uint16_t> tree;
    const auto build = [&](auto& self, size_t first, size_t last) -> void {
        if (first == last) return;
        const size_t middle = first + (last - first - 1) / 2, at = tree.size();
        const uint16_t low = runs[middle].first, high = runs[middle].second;
        tree.insert(tree.end(), {low, 0, high, 0});
        for (uint32_t u = low; u <= high; ++u) tree.push_back(bytes[static_cast<uint16_t>(u)]);
        if (middle > first) tree[at + 1] = static_cast<uint16_t>((tree.size() - at) * 2);
        self(self, first, middle);
        if (middle + 1 < last) tree[at + 3] = static_cast<uint16_t>((tree.size() - at) * 2);
        self(self, middle + 1, last);
    };
    build(build, 0, runs.size());
    uniToMb.assign({'B', 'F', 'U', 'M'});
    for (uint16_t v : tree) put(uniToMb, v);
    return true;
}

SupplementResult ImportUnofficialLanguages(Source& source, const std::string& assetsDir,
                                           const std::atomic<bool>& cancel) {
    SupplementResult result;
    std::map<std::string, Candidate> candidates;
    for (const Entry& e : source.Entries()) {
        if (e.kind != EntryKind::Data || e.unreadable || !SafePath(e.path)) continue;
        const std::string path = Lower(e.path), token = "data/language/";
        size_t at = path.find(token);
        if (at == std::string::npos || (at && path[at - 1] != '/')) continue;
        const size_t start = at + token.size(), slash = path.find('/', start);
        if (slash == std::string::npos || path.find('/', slash + 1) != std::string::npos) continue;
        const std::string language = OriginalName(path.substr(start, slash - start));
        if (language.empty()) continue;
        Candidate& c = candidates[path.substr(0, slash + 1)];
        c.prefix = path.substr(0, at);
        c.language = language;
        c.text.emplace(path.substr(slash + 1), &e);
    }
    // A known release's other language folder is part of the same release (the Russian Project put its translation in
    // both), so it is not offered as a second translation of its own.
    std::set<std::string> knownReleases;
    for (const auto& pair : candidates) {
        std::map<std::string, Bytes> text;
        std::string err;
        for (const char* name : {"uitext.str", "uihelptext.str"}) {
            const auto it = pair.second.text.find(name);
            Bytes b;
            if (it != pair.second.text.end() && Read(source, *it->second, b, err)) text.emplace(name, std::move(b));
        }
        if (RussianProject(text)) knownReleases.insert(pair.second.prefix);
    }
    // Within a release, the folder with its own code page first, then American, then English: of two folders with the
    // same name, the one most likely complete is kept.
    std::vector<const Candidate*> order;
    for (const auto& pair : candidates) order.push_back(&pair.second);
    std::stable_sort(order.begin(), order.end(), [](const Candidate* a, const Candidate* b) {
        const auto rank = [](const Candidate* c) {
            return (c->text.count("mbtouni.dat") ? 0 : 2) + (c->language == "American" ? 0 : 1);
        };
        return a->prefix != b->prefix ? a->prefix < b->prefix : rank(a) < rank(b);
    });
    // A folder's own code page, kept with it; empty when it has none.
    const auto own = [&](const Candidate& folder) {
        Bytes b;
        std::string err;
        const auto it = folder.text.find("mbtouni.dat");
        if (it != folder.text.end() && !Read(source, *it->second, b, err)) b.clear();
        return b;
    };
    // The code page a folder's text is read through to name its language: its own, else another folder's of its release,
    // else the installed edition's for its language, else the other edition's.
    const auto codePage = [&](const Candidate& c) {
        Bytes map = own(c);
        for (const Candidate* other : order)
            if (map.empty() && other->prefix == c.prefix) map = own(*other);
        if (!map.empty()) return map;
        const std::vector<std::string> editions = c.language == "American"
                                                      ? std::vector<std::string>{"us", "us-pc", "eur", "eur-pc"}
                                                      : std::vector<std::string>{"eur", "eur-pc", "us", "us-pc"};
        for (const std::string& edition : editions) {
            for (const char* language : {"English", "American"}) {
                const fs::path file =
                    fs::u8path(assetsDir) / edition / "Theme Park Data/data/Language" / language / "MBToUni.dat";
                std::error_code ec;
                const uintmax_t size = fs::file_size(file, ec);
                if (ec || size > kSmallLimit) continue;
                std::ifstream in(file, std::ios::binary);
                map.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                if (map.size() == size) return map;
            }
        }
        return Bytes();
    };
    std::set<std::string> done;
    std::map<std::string, bool> seen;   // each release's text sets so far, and whether each was kept
    for (const Candidate* candidate : order) {
        if (cancel) break;
        const Candidate& c = *candidate;
        std::map<std::string, Bytes> bytes;
        std::string err;
        bool unknownText = false;
        for (const auto& item : c.text) {
            if (item.first.size() < 4 || item.first.substr(item.first.size() - 4) != ".str") continue;
            Bytes b;
            if (!Read(source, *item.second, b, err)) {
                unknownText = true;
                break;
            }
            unknownText = unknownText || !Official(b);
            bytes.emplace(item.first, std::move(b));
        }
        if (!unknownText) continue;
        const bool russian = err.empty() && RussianProject(bytes);
        const auto claim = [&] {
            for (const auto& item : c.text) result.claimed.insert(item.second->path);
        };
        if (!russian && knownReleases.count(c.prefix)) {
            claim();
            continue;
        }
        // An English and an American folder of one release with the same text are one translation, kept once.
        std::string textKey = c.prefix + '\n';
        for (const auto& b : bytes) textKey += b.first + ' ' + Sha256::Of(b.second.data(), b.second.size()).Hex() + '\n';
        if (err.empty() && seen.count(textKey)) {
            if (seen[textKey]) claim();
            continue;
        }
        seen[textKey] = false;
        const auto fail = [&](const std::string& reason) {
            result.errors.push_back(tpw::ui::kLanguagePackRefused + reason);
        };
        if (!err.empty()) {
            fail(err);
            continue;
        }
        if (!bytes.count("uitext.str")) {
            fail("the pack has no UITEXT.str text table");
            continue;
        }
        // This version accepts replacement English/American encodings only. Other alphabets need their own verified
        // multibyte converters; guessing one would display the wrong text or read past a glyph table.
        if (c.language != "English" && c.language != "American") {
            fail("this unofficial " + c.language + " encoding is not supported");
            continue;
        }
        std::set<uint8_t> glyphs;
        std::array<uint64_t, 256> letters{};
        Where where;
        for (const auto& b : bytes)
            if (!CheckTable(b.first, b.second, glyphs, letters, where, err)) break;
        if (!err.empty()) {
            fail(err);
            continue;
        }
        // English text of no known language is the game's own (another pressing's, as on the US 1.0 and 1.1 discs and
        // the European English, Dutch, Italian and Polish one), not a translation: it is left to the edition's import,
        // never offered or asked about.
        const Bytes map = codePage(c);
        if (!russian && TextLanguage(letters, map).empty() && EnglishText(bytes, map)) continue;
        for (const auto& item : c.text) {
            if (item.first.size() < 4 || item.first.substr(item.first.size() - 4) != ".bf4") continue;
            Bytes b;
            if (!Read(source, *item.second, b, err)) break;
            bytes.emplace(item.first, std::move(b));
        }
        // The game draws byte b with a font's glyph b - 1 (TbIRLE4bitFont::GetCharIndex). A byte past a text font's last
        // glyph would be read from outside the font, so it refuses the pack. A glyph that is empty draws nothing: a few
        // such letters are said with the translation, so many that the text would not show refuse it.
        std::map<uint8_t, std::vector<std::string>> blank;
        std::set<uint16_t> counts;
        size_t textFonts = 0;
        for (const auto& item : bytes) {
            std::set<uint8_t> empty;
            const bool general = GeneralFont(item.first);
            if (!err.empty()) break;
            if (item.first.substr(item.first.size() - 4) != ".bf4" ||
                !CheckFont(item.first, item.second, glyphs, general, empty, err) || !general)
                continue;
            const std::string font = FileName(*c.text.at(item.first));
            const uint16_t count = U16(item.second, 6);
            ++textFonts;
            counts.insert(count);
            for (uint8_t b : empty) blank[b].push_back(font);
            if (!glyphs.empty() && *glyphs.rbegin() > count)
                err = Letter(*glyphs.rbegin(), map) + ", used in " + Used(*glyphs.rbegin(), where, c) +
                      ", is past the last of " + font + "'s " + std::to_string(count) +
                      (count == 1 ? " letter" : " letters") + ", so the game would draw it from outside the font";
        }
        uint64_t undrawn = 0, shown = 0;
        for (unsigned b = 33; b < 256; ++b) shown += letters[b];
        for (const auto& item : blank) undrawn += letters[item.first];
        if (err.empty() && undrawn * 100 > shown)
            err = "its fonts draw " + std::to_string(undrawn) + " of its " + std::to_string(shown) +
                  " letters as nothing: " + Undrawn(blank, textFonts, where, c, map);
        // Code page files of the other edition than the fonts give the names the game spells itself (the park's) that
        // edition's bytes, one letter off in these fonts: the fonts' edition's own pair is used instead.
        const std::string fontPage = counts.size() == 1 ? CodePageOf(*counts.begin()) : "";
        std::map<std::string, Bytes> made;
        std::string tablesFrom;
        if (const auto it = c.text.find("mbtouni.dat"); err.empty() && it != c.text.end() && !fontPage.empty()) {
            Bytes own, mbToUni, uniToMb;
            if (Read(source, *it->second, own, err) && !Origin(own).empty() && Origin(own) != fontPage &&
                FittingCodePage(own, fontPage, mbToUni, uniToMb)) {
                std::vector<std::string> names;
                for (const char* name : {"mbtouni.dat", "unitomb.dat"})
                    if (c.text.count(name)) names.push_back(FileName(*c.text.at(name)));
                tablesFrom = "its " + Listed(names) + (names.size() == 1 ? " is " : " are ") + Origin(own) +
                             ", but its fonts are drawn for " + fontPage + ", so " + fontPage +
                             " own are used instead, and the names the game writes itself, like the park's, show right";
                made.emplace("mbtouni.dat", std::move(mbToUni));
                made.emplace("unitomb.dat", std::move(uniToMb));
            }
        }
        std::string warning = OtherEdition(c, bytes, fontPage, made, source);
        if (!blank.empty()) warning += (warning.empty() ? "" : "; ") + Undrawn(blank, textFonts, where, c, map);
        if (!err.empty()) {
            fail(err);
            continue;
        }
        if (!glyphs.empty() && *glyphs.rbegin() > 127) {
            // Every general-purpose font the original index uses must supply the translation's alphabet. Numeric
            // date/cash fonts and the decorative Matisse36 font do not render arbitrary translated UI text.
            const std::string prefix = "data/Language/English/";
            for (size_t i = 0; i < ManifestRecordCount(); ++i) {
                const ManifestRecord& r = ManifestRecords()[i];
                const std::string dest = r.dest;
                if (dest.compare(0, prefix.size(), prefix) == 0 && GeneralFont(Lower(dest.substr(prefix.size()))) &&
                    !bytes.count(Lower(dest.substr(prefix.size())))) {
                    err = "missing glyph fonts: " + dest.substr(prefix.size());
                    break;
                }
            }
        }
        if (!err.empty()) {
            fail(err);
            continue;
        }
        // Named by a known release's hashes, else by its own letters; else it keeps the folder's name until the player
        // names it (NameUnofficialLanguage). A Russian one is kept as Russian, any other as Unofficial-<language>.
        const std::string name = russian ? "Russian" : TextLanguage(letters, map);
        const std::string id = name == "Russian" ? name : "Unofficial-" + (name.empty() ? c.language : name);
        if (!done.insert(id).second) {
            claim();
            continue;
        }
        const std::string description = russian         ? kRussianProject
                                         : name.empty() ? tpw::ui::kUnofficialUnknown
                                                        : "Its text reads as " + name;
        std::string label = Label(id, description);
        std::map<std::string, const Entry*> files;
        size_t textCount = 0, speechCount = 0, fontCount = 0;
        for (const auto& item : bytes) {
            const Entry* e = c.text.at(item.first);
            files["data/Language/English/" + item.first] = e;
            files["data/Language/American/" + item.first] = e;
            if (item.first.substr(item.first.size() - 4) == ".str")
                ++textCount;
            else
                ++fontCount;
        }
        // Keep only verified ancillary language resources. An unknown index or code-page tree could make even a
        // well-formed text table unsafe; none is allowed to replace a base resource by its filename alone.
        for (const auto& item : c.text) {
            if (bytes.count(item.first)) continue;
            if (made.count(item.first)) {
                result.claimed.insert(item.second->path);
                continue;
            }
            if (item.first != "residx.dat" && item.first != "swears.txt" && item.first != "alloweds.txt" &&
                item.first.substr(item.first.find_last_of('.') + 1) != "dat" &&
                item.first.substr(item.first.find_last_of('.') + 1) != "md2")
                continue;
            Bytes b;
            if (!Read(source, *item.second, b, err)) break;
            if (!Official(b)) {
                err = item.first + ": unrecognised index, code page or ancillary resource";
                break;
            }
            // The base edition's current index supplies missing tables and date fonts, with old table repair by count.
            if (item.first == "residx.dat") continue;
            files["data/Language/English/" + item.first] = item.second;
            files["data/Language/American/" + item.first] = item.second;
        }
        for (const Entry& e : source.Entries()) {
            const std::string path = Lower(e.path);
            if (e.kind != EntryKind::Data || e.unreadable || !SafePath(e.path) ||
                path.compare(0, c.prefix.size(), c.prefix) != 0)
                continue;
            const std::string tail = path.substr(c.prefix.size());
            if (SpeechFile(tail)) {
                files.emplace(tail, &e);
                ++speechCount;
            } else if (tail.compare(0, 10, "data/init/") == 0 && tail.size() > 4 &&
                       tail.substr(tail.size() - 4) == ".tga")
                files.emplace(tail, &e);
            else if (tail == "data/ui.wad") {
                auto blob = OpenEntryBlob(std::shared_ptr<Source>(&source, [](Source*) {}), e, err);
                if (!blob || !OpenWad(blob, err)) {
                    if (err.empty()) err = "damaged ui.wad";
                    break;
                }
                files.emplace(tail, &e);
            }
        }
        if (!err.empty()) {
            fail(err);
            continue;
        }
        std::string changes = textCount ? "text" : "";
        if (fontCount) changes += (changes.empty() ? "" : ", ") + std::string("fonts");
        if (speechCount) changes += (changes.empty() ? "" : ", ") + std::string("speech");
        if (files.count("data/ui.wad")) changes += ", interface pictures";
        const fs::path root = fs::u8path(assetsDir), dest = root / "lang" / id;
        std::error_code ec;
        // A sound copy of the same files is left alone; a different translation dropped under the same name replaces it
        // (the player's newest drop wins), and a broken copy is restored by dropping its source again.
        auto installed = UnofficialLanguages(assetsDir);
        const bool replacing =
            std::any_of(installed.begin(), installed.end(), [&](const LanguageStatus& s) { return s.name == id; });
        const fs::path stage = root / ".staging" / ("language-" + id);
        fs::remove_all(stage, ec);
        ec.clear();
        fs::create_directories(stage, ec);
        if (ec) {
            fail("cannot create the language staging folder: " + ec.message());
            continue;
        }
        std::ofstream inventory(stage / ".unofficial", std::ios::binary);
        inventory << "1\n"
                  << id << '\n'
                  << description << '\n'
                  << changes << '\n'
                  << textCount + fontCount << ' ' << speechCount << '\n';
        uint64_t total = 0;
        for (const auto& item : made)
            for (const char* folder : {"data/Language/English/", "data/Language/American/"}) {
                const fs::path out = stage / fs::u8path(folder + item.first);
                fs::create_directories(out.parent_path(), ec);
                std::ofstream stream(out, std::ios::binary);
                stream.write(reinterpret_cast<const char*>(item.second.data()),
                             static_cast<std::streamsize>(item.second.size()));
                stream.close();
                if (ec || !stream) err = "cannot write the language files";
                inventory << folder << item.first << '\t' << item.second.size() << '\t'
                          << Sha256::Of(item.second.data(), item.second.size()).Hex() << '\n';
            }
        std::array<char, 1 << 16> buffer;
        for (const auto& file : files) {
            if (!err.empty()) break;
            if (cancel) {
                err = "cancelled";
                break;
            }
            if (file.second->size > kPackLimit - total) {
                err = "the translation is too large";
                break;
            }
            total += file.second->size;
            const fs::path out = stage / fs::u8path(file.first);
            fs::create_directories(out.parent_path(), ec);
            auto in = source.Open(*file.second, err);
            std::ofstream stream(out, std::ios::binary);
            if (ec || !in || !stream) {
                err = "cannot write the language files";
                break;
            }
            Sha256 sha;
            uint64_t size = 0;
            int64_t n = 0;
            while (!cancel && (n = in->Read(buffer.data(), buffer.size(), err)) > 0) {
                size += static_cast<uint64_t>(n);
                if (size > file.second->size) {
                    err = "a language file grew while reading";
                    break;
                }
                sha.Update(buffer.data(), static_cast<size_t>(n));
                stream.write(buffer.data(), n);
                if (!stream) {
                    err = "cannot write the language files";
                    break;
                }
            }
            stream.close();
            if (cancel || n < 0 || size != file.second->size || !stream || !err.empty()) {
                if (err.empty()) err = cancel ? "cancelled" : "cannot read the whole language file";
                break;
            }
            // Small validated files are read again while staging; reject a source changed since validation.
            const auto slash = file.first.find_last_of('/');
            const auto checked = bytes.find(file.first.substr(slash + 1));
            const std::string hash = sha.Final().Hex();
            if (file.first.compare(0, 14, "data/Language/") == 0 && checked != bytes.end() &&
                hash != Sha256::Of(checked->second.data(), checked->second.size()).Hex()) {
                err = "a language file changed while reading";
                break;
            }
            inventory << file.first << '\t' << size << '\t' << hash << '\n';
        }
        inventory.close();
        if (!inventory && err.empty()) err = "cannot write the language inventory";
        // The inventories list every file with its size and hash, so equal ones are the same translation.
        bool same = false;
        if (err.empty() && name.empty()) {
            // One the player has named is found by its files under that name, so they are asked once.
            const std::string kept = InventoryFiles(stage / ".unofficial");
            for (const LanguageStatus& s : installed) {
                if (s.name == id || InventoryFiles(root / "lang" / fs::u8path(s.name) / ".unofficial") != kept) continue;
                label = Label(s.name, s.description);
                same = true;
                break;
            }
        }
        if (err.empty() && replacing && !same) {
            const auto text = [](const fs::path& file) {
                std::ifstream in(file, std::ios::binary);
                std::ostringstream out;
                out << in.rdbuf();
                return out.str();
            };
            same = text(stage / ".unofficial") == text(dest / ".unofficial");
        }
        if (err.empty() && !same) {
            fs::create_directories(dest.parent_path(), ec);
            // Replacing a damaged translation never touches verified edition files.
            fs::remove_all(dest, ec);
            ec.clear();
            fs::rename(stage, dest, ec);
            if (ec) err = "cannot keep the language: " + ec.message();
        }
        if (!err.empty()) {
            fs::remove_all(stage, ec);
            if (!cancel) fail(err);
        } else if (same) {
            seen[textKey] = true;
            fs::remove_all(stage, ec);
            result.alreadyThere += files.size() + 2 * made.size();
            for (const auto& file : files) result.claimed.insert(file.second->path);
            result.lines.push_back(label + " was already added");
            if (!tablesFrom.empty()) result.lines.push_back(label + ": " + tablesFrom);
            if (!warning.empty()) result.lines.push_back(label + ": some of it will not show right: " + warning);
        } else {
            seen[textKey] = true;
            result.written += files.size() + 2 * made.size();
            for (const auto& file : files) result.claimed.insert(file.second->path);
            result.lines.push_back(label + (replacing ? " updated: " : " added: ") + changes);
            if (!tablesFrom.empty()) result.lines.push_back(label + ": " + tablesFrom);
            if (!warning.empty()) result.lines.push_back(label + ": some of it will not show right: " + warning);
        }
    }
    return result;
}

void ConfigureUnofficialLanguages(ImporterConfig& config) {
    const std::string assets = config.dataRoot + "/assets";
    config.sourceSupplement = [assets](Source& source, const std::atomic<bool>& cancel) {
        return ImportUnofficialLanguages(source, assets, cancel);
    };
    config.extraLanguages = [assets] { return UnofficialLanguages(assets); };
}

std::vector<LanguageStatus> UnofficialLanguages(const std::string& assetsDir) {
    std::vector<LanguageStatus> out;
    std::error_code ec;
    const fs::path root = fs::u8path(assetsDir) / "lang";
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string id = tpw::PathText(it->path().filename());
        if (!TranslationId(id)) continue;
        std::ifstream in(it->path() / ".unofficial", std::ios::binary);
        if (ReadLine(in) != "1" || ReadLine(in) != id) continue;
        LanguageStatus status;
        status.name = id;
        status.unofficial = true;
        status.description = ReadLine(in);
        status.changes = ReadLine(in);
        std::istringstream counts(ReadLine(in));
        if (!(counts >> status.text >> status.speech)) continue;
        bool valid = true;
        size_t entries = 0;
        std::string line;
        while (std::getline(in, line)) {
            const size_t tab = line.find('\t'), last = line.rfind('\t');
            if (tab == std::string::npos || tab == last) {
                valid = false;
                break;
            }
            const std::string path = line.substr(0, tab), hash = line.substr(last + 1);
            uint64_t size = 0;
            std::istringstream length(line.substr(tab + 1, last - tab - 1));
            if (!SafePath(path) || !(length >> size) || size > kPackLimit || hash.size() != 64) {
                valid = false;
                break;
            }
            const fs::path file = it->path() / fs::u8path(path);
            std::error_code error;
            if (!fs::is_regular_file(file, error) || fs::file_size(file, error) != size || error) {
                valid = false;
                break;
            }
            // Recheck small text/font resources on each refresh. Speech banks are size checked, as the normal startup
            // verifier checks unchanged files by their stat record rather than rereading hundreds of MB.
            if (path.compare(0, 14, "data/Language/") == 0) {
                std::ifstream bytes(file, std::ios::binary);
                if (size > kSmallLimit) {
                    valid = false;
                    break;
                }
                Bytes b(static_cast<size_t>(size));
                if (!bytes.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(size)) ||
                    Sha256::Of(b.data(), b.size()).Hex() != hash) {
                    valid = false;
                    break;
                }
            }
            ++entries;
        }
        if (!valid || entries < 2 || !status.text) continue;
        status.textPresent = status.text;
        status.speechPresent = status.speech;
        status.state = LanguageStatus::State::Available;
        out.push_back(std::move(status));
    }
    return out;
}

const std::vector<TranslationLanguage>& TranslationLanguages() {
    static const std::vector<TranslationLanguage> kLanguages = {
        {"Belarusian", ""}, {"Bulgarian", ""},      {"Czech", ""},
        {"Greek", ""},      {"Hungarian", "Magyar"}, {"Macedonian", ""},
        {"Polish", "Polski"}, {"Portuguese", "Portugu\xC3\xAAs"}, {"Romanian", ""},
        {"Russian", ""},    {"Serbian", "Srpski"},   {"Turkish", "T\xC3\xBCrk\xC3\xA7" "e"},
        {"Ukrainian", ""},
    };
    return kLanguages;
}

int SuggestedTranslationLanguage(const std::vector<std::string>& paths) {
    // Each language's names as a file name may spell them: English, its own (UTF-8, lower case), a transliteration. A
    // short one only as a whole word ("full_rus"), a long one anywhere in the name ("SimThemeParkRussian").
    static const std::vector<std::pair<const char*, std::vector<const char*>>> kNames = {
        {"Belarusian", {"belarus", "\xD0\xB1\xD0\xB5\xD0\xBB\xD0\xB0\xD1\x80\xD1\x83\xD1\x81",
                        "\xD0\xB1\xD0\xB5\xD0\xBB\xD0\xBE\xD1\x80\xD1\x83\xD1\x81"}},
        {"Bulgarian", {"bulgar", "\xD0\xB1\xD1\x8A\xD0\xBB\xD0\xB3\xD0\xB0\xD1\x80",
                       "\xD0\xB1\xD0\xBE\xD0\xBB\xD0\xB3\xD0\xB0\xD1\x80"}},
        {"Czech", {"czech", "cesky", "cestina", "\xC4\x8D" "esk"}},
        {"Greek", {"greek", "\xCE\xB5\xCE\xBB\xCE\xBB\xCE\xB7\xCE\xBD"}},
        {"Hungarian", {"hungar", "magyar"}},
        {"Macedonian", {"macedon", "makedon", "\xD0\xBC\xD0\xB0\xD0\xBA\xD0\xB5\xD0\xB4\xD0\xBE\xD0\xBD"}},
        {"Polish", {"polish", "polski", "polsku"}},
        {"Portuguese", {"portug"}},
        {"Romanian", {"romanian", "romana", "rom\xC3\xA2n"}},
        {"Russian", {"russian", "russk", "\xD1\x80\xD1\x83\xD1\x81\xD1\x81\xD0\xBA", "rus"}},
        {"Serbian", {"serbian", "srpski", "\xD1\x81\xD1\x80\xD0\xBF\xD1\x81\xD0\xBA",
                     "\xD1\x81\xD0\xB5\xD1\x80\xD0\xB1\xD1\x81\xD0\xBA"}},
        {"Turkish", {"turkish", "turkce", "t\xC3\xBCrk"}},
        {"Ukrainian", {"ukrain", "\xD1\x83\xD0\xBA\xD1\x80\xD0\xB0\xD1\x97\xD0\xBD",
                       "\xD1\x83\xD0\xBA\xD1\x80\xD0\xB0\xD0\xB8\xD0\xBD", "ukr"}},
    };
    // The paths and the spellings are UTF-8, so the name is read back as UTF-8 too: in a process whose code page is not
    // UTF-8 (a test, an older Windows), PathText's string() cannot narrow a Cyrillic name and throws.
    for (const std::string& path : paths) {
        std::string file = fs::u8path(path).filename().u8string();
        if (file.empty()) file = fs::u8path(path).parent_path().filename().u8string();   // "folder/"
        const std::string folded = FoldName(file);
        std::vector<std::string> words(1);
        for (char c : folded) {
            if ((c >= 'a' && c <= 'z') || (static_cast<unsigned char>(c) & 0x80))
                words.back() += c;
            else if (!words.back().empty())
                words.emplace_back();
        }
        for (const auto& language : kNames)
            for (const char* spelling : language.second) {
                const std::string s = spelling;
                const bool found = s.size() > 3 ? folded.find(s) != std::string::npos
                                                : std::find(words.begin(), words.end(), s) != words.end();
                if (!found) continue;
                const auto& list = TranslationLanguages();
                for (size_t i = 0; i < list.size(); ++i)
                    if (list[i].name == std::string(language.first)) return static_cast<int>(i);
            }
    }
    return -1;
}

bool NameUnofficialLanguage(const std::string& assetsDir, const std::string& id, const std::string& language,
                            std::string& newId, std::string& err) {
    const auto& list = TranslationLanguages();
    if (std::none_of(list.begin(), list.end(), [&](const TranslationLanguage& l) { return language == l.name; })) {
        err = "not a language the launcher names translations with: " + language;
        return false;
    }
    if (id != "Unofficial-English" && id != "Unofficial-American") {
        err = "only a translation whose language is not known is named";
        return false;
    }
    newId = language == "Russian" ? language : "Unofficial-" + language;
    const fs::path root = fs::u8path(assetsDir), from = root / "lang" / id, to = root / "lang" / newId;
    std::ifstream in(from / ".unofficial", std::ios::binary);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) lines.push_back(line);
    in.close();
    if (lines.size() < 5 || lines[0] != "1" || lines[1] != id) {
        err = "it is not kept as " + id;
        return false;
    }
    lines[1] = newId;
    lines[2] = "Named " + language + " when it was added";
    // Moved aside first, its inventory rewritten there, then put in place of any translation kept under the new name:
    // a failure part way leaves it where it was, or under its new name complete.
    const fs::path stage = root / ".staging" / ("language-" + newId);
    std::error_code ec;
    fs::remove_all(stage, ec);
    ec.clear();
    fs::create_directories(stage.parent_path(), ec);
    if (!ec) fs::rename(from, stage, ec);
    if (ec) {
        err = "cannot move it: " + ec.message();
        return false;
    }
    {
        std::ofstream out(stage / ".unofficial.new", std::ios::binary);
        for (const std::string& line : lines) out << line << '\n';
        out.close();
        if (!out) ec = std::make_error_code(std::errc::io_error);
    }
    if (!ec) fs::rename(stage / ".unofficial.new", stage / ".unofficial", ec);
    if (!ec) {
        fs::remove_all(to, ec);
        ec.clear();
        fs::rename(stage, to, ec);
    }
    if (ec) {
        err = "cannot rename it: " + ec.message();
        std::error_code back;
        fs::remove(stage / ".unofficial.new", back);
        if (fs::exists(stage, back)) {
            std::ofstream out(stage / ".unofficial", std::ios::binary);
            lines[1] = id;
            lines[2] = tpw::ui::kUnofficialUnknown;
            for (const std::string& line : lines) out << line << '\n';
            out.close();
            fs::rename(stage, from, back);
        }
        return false;
    }
    return true;
}
}  // namespace tpw::assets
#endif
