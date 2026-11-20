#include "unofficial_language.h"
#ifdef TPW_PORT_FIX
// TPW_PORT_FIX packlanguage ImportUnofficialLanguages | Mac: the installer replaces a language in the game folder | Port: validate a supplied translation and keep it as a separate selectable overlay | Why: a translation can be tried and switched off without changing the game files
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
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
bool CheckTable(const std::string& file, const Bytes& b, std::set<uint8_t>& glyphs, std::string& err) {
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
                    if (c >= 32) glyphs.insert(c);
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
bool CheckFont(const std::string& file, const Bytes& b, const std::set<uint8_t>& glyphs, bool coverage,
               std::string& err) {
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
            if (!ended || produced > uint64_t(unpacked) * 2 || produced < pixels) {
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
            (pixels == 0 || U16(b, offset + 22) == 0)) {
            err = file + ": missing glyphs for the translation";
            return false;
        }
    }
    if (coverage && !glyphs.empty() && *glyphs.rbegin() > count) {
        err = file + ": missing glyphs for the translation";
        return false;
    }
    return true;
}
struct Candidate {
    std::string prefix, language;
    std::map<std::string, const Entry*> text;
};
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
bool GeneralFont(const std::string& name) {
    return name.size() > 4 && name.substr(name.size() - 4) == ".bf4" && name.compare(0, 4, "date") != 0 &&
           name.compare(0, 4, "cash") != 0 && name != "matisse36.bf4";
}
// Content identification only: hashes, no translation material. Both independently observed Russian Project discs
// have these two tables. A renamed archive alone cannot claim to be that translation.
bool RussianProject(const std::map<std::string, Bytes>& text) {
    auto hash = [&](const char* name) {
        const auto it = text.find(name);
        return it == text.end() ? std::string() : Sha256::Of(it->second.data(), it->second.size()).Hex();
    };
    return hash("uitext.str") == "1311342d8ec9816ac516b728e7b495aebf0e1243e82280bcea2dd91967b963f5" &&
           hash("uihelptext.str") == "a8b2058fb78bf75297408e949ac341c62cf38ef869eafe545a58b97e2fa82e00";
}
std::string ReadLine(std::istream& in) {
    std::string s;
    std::getline(in, s);
    return s;
}
}  // namespace

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
    std::set<std::string> done;
    for (const auto& pair : candidates) {
        if (cancel) break;
        const Candidate& c = pair.second;
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
        if (!russian && knownReleases.count(c.prefix)) {
            for (const auto& item : c.text) result.claimed.insert(item.second->path);
            continue;
        }
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
        for (const auto& b : bytes)
            if (!CheckTable(b.first, b.second, glyphs, err)) break;
        if (!err.empty()) {
            fail(err);
            continue;
        }
        for (const auto& item : c.text) {
            if (item.first.size() < 4 || item.first.substr(item.first.size() - 4) != ".bf4") continue;
            Bytes b;
            if (!Read(source, *item.second, b, err) || !CheckFont(item.first, b, glyphs, GeneralFont(item.first), err))
                break;
            bytes.emplace(item.first, std::move(b));
        }
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
        const std::string id = russian ? "Russian" : "Unofficial-" + c.language;
        if (!done.insert(id).second) continue;
        const std::string description =
            russian ? "Russian (unofficial translation, 2000)" : "Not a known official release";
        const std::string label = russian ? description : c.language + " (unofficial translation)";
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
        // Existing safe copies are left alone. A broken inventory can be restored by dropping its source again.
        auto installed = UnofficialLanguages(assetsDir);
        if (std::any_of(installed.begin(), installed.end(), [&](const LanguageStatus& s) { return s.name == id; })) {
            result.alreadyThere += files.size();
            for (const auto& file : files) result.claimed.insert(file.second->path);
            result.lines.push_back(label + " was already added");
            continue;
        }
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
        std::array<char, 1 << 16> buffer;
        for (const auto& file : files) {
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
        if (err.empty()) {
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
        } else {
            result.written += files.size();
            for (const auto& file : files) result.claimed.insert(file.second->path);
            result.lines.push_back(label + " added: " + changes);
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
        if (id != "Russian" && id != "Unofficial-English" && id != "Unofficial-American") continue;
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
}  // namespace tpw::assets
#endif
