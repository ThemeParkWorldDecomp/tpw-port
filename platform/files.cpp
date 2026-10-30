// files.cpp: the game's view of the disk. Mac and Windows paths onto the game folder and
// the user folder, the application folder objects (UProcess, UFolder), directory searches
// (FindFirstFile), and the Mac File and Resource Manager calls the game makes itself (the
// CD check and the "debug options" resource).
#include <algorithm>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <map>
#include <system_error>

#include <sys/stat.h>

#include "gold_rides.h"
#include "language_pack.h"
#include "packs.h"
#include "platform.h"
#include "platform/macdoze.h"

namespace fs = std::filesystem;

namespace tpw {

const char kProjectDirMac[] = "TPW:Theme Park Data:";
const char kRemoteDirMac[] = "Theme Park:Theme Park Data:";

// In a double-byte code page a character's two bytes are compared as they are: a trail byte may be a letter's byte.
static bool EqualNoCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (IsGameLeadByte(static_cast<uint8_t>(a[i])) && i + 1 < a.size()) {
            if (a[i] != b[i] || a[i + 1] != b[i + 1]) return false;
            ++i;
            continue;
        }
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

static bool StartsWithNoCase(const std::string& text, const char* prefix) {
    const size_t n = std::strlen(prefix);
    return text.size() >= n && EqualNoCase(text.substr(0, n), prefix);
}

std::vector<std::string> SplitGamePath(const std::string& gamePath) {
    std::string path = gamePath;
    for (const char* prefix : {kProjectDirMac, kRemoteDirMac}) {
        if (StartsWithNoCase(path, prefix)) {
            path = path.substr(std::strlen(prefix));
            break;
        }
    }
    std::vector<std::string> parts;
    std::string part;
    auto flush = [&]() {
        // "." names the folder itself (QualifyFilename drops it too); an empty component
        // is a doubled or leading separator.
        if (!part.empty() && part != ".") parts.push_back(part);
        part.clear();
    };
    for (size_t i = 0; i < path.size(); ++i) {
        const char c = path[i];
        if (IsGameLeadByte(static_cast<uint8_t>(c)) && i + 1 < path.size()) {
            part.push_back(c);
            part.push_back(path[++i]);   // a trail byte, even 0x5c ('\\')
        } else if (c == ':' || c == '\\' || c == '/') {
            flush();
        } else {
            part.push_back(c);
        }
    }
    flush();
    return parts;
}

// A name of the game as the name of a file or folder on the host, and back. Windows refuses < > " | ? and * in a name,
// which the game's names may hold (a park saved as "Test?"; the Mac refused only ':', its separator, and the game turns a
// character it cannot store in a narrow name into '?'). There each of them, and '%' itself, is kept as '%' and its two hex
// digits, and read back so when a folder is listed; a '%' that does not start one of those is read as itself, so a file
// named before stays found. Elsewhere the names are the same. From tpw-port PR #73 by samueleagle91. The game's code page
// is turned into the host's characters first (HostNameOf, code_page.cpp), so a double-byte character's trail byte is
// never taken for one of those.
#ifdef _WIN32
static const char kEscapedInHostNames[] = "<>\"|?*%";

static bool EscapedInHostNames(wchar_t c) {
    return c != 0 && c < 0x80 && std::strchr(kEscapedInHostNames, static_cast<char>(c)) != nullptr;
}

static fs::path HostName(const std::string& name) {
    std::wstring host;
    // Held in a local: a range-for over HostNameOf(name).native() keeps only the reference, and the path it names is gone
    // before the loop starts, so a name too long for the string's own buffer (restart.INTS, autosave.TPWS) was read from freed memory.
    const fs::path base = HostNameOf(name);
    for (wchar_t c : base.native()) {
        if (EscapedInHostNames(c)) {
            static const wchar_t kHex[] = L"0123456789ABCDEF";
            host += L'%';
            host += kHex[c >> 4];
            host += kHex[c & 15];
        } else {
            host += c;
        }
    }
    return fs::path(host);
}

static std::string GameName(const fs::path& hostName) {
    const auto hex = [](wchar_t c) {
        if (c >= L'0' && c <= L'9') return static_cast<int>(c - L'0');
        if (c >= L'A' && c <= L'F') return static_cast<int>(c - L'A' + 10);
        if (c >= L'a' && c <= L'f') return static_cast<int>(c - L'a' + 10);
        return -1;
    };
    const std::wstring& host = hostName.native();
    std::wstring name;
    for (size_t i = 0; i < host.size(); ++i) {
        if (host[i] == L'%' && i + 2 < host.size() && hex(host[i + 1]) >= 0 && hex(host[i + 2]) >= 0) {
            const wchar_t c = static_cast<wchar_t>(hex(host[i + 1]) * 16 + hex(host[i + 2]));
            if (EscapedInHostNames(c)) {
                name += c;
                i += 2;
                continue;
            }
        }
        name += host[i];
    }
    return GameNameOf(fs::path(name));
}
#else
static fs::path HostName(const std::string& name) { return HostNameOf(name); }
static std::string GameName(const fs::path& hostName) { return GameNameOf(hostName); }
#endif

// `name` inside `folder`, matched ignoring case: the exact name if it exists, else the
// first entry that differs only in case. Empty if there is none. The name is the game's
// (HostName); an entry the code page cannot name is still found by its own bytes.
static fs::path FindChild(const fs::path& folder, const std::string& name) {
    std::error_code ec;
    const fs::path host = HostName(name);
    const fs::path exact = folder / host;
    if (fs::exists(exact, ec)) return exact;
    if (host.native() != fs::path(name).native() && fs::exists(folder / name, ec)) return folder / name;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
        if (EqualNoCase(GameName(it->path().filename()), name)) return it->path();
    return {};
}

static fs::path FindUnder(const fs::path& root, const std::vector<std::string>& parts) {
    if (root.empty()) return {};
    fs::path current = root;
    for (const std::string& part : parts) {
        current = FindChild(current, part);
        if (current.empty()) return {};
    }
    std::error_code ec;
    return fs::exists(current, ec) ? current : fs::path();
}

std::vector<fs::path>& PackDirs() {
    static std::vector<fs::path> dirs;
    return dirs;
}

// The folders the game keeps its sound banks in, each an .sdt with its .map index: data/global's sound and speech, and
// each level's sound, speech and music (`folder` lower case).
static bool IsSoundBankFolder(const std::string& folder) {
    if (folder == "data/global/sound" || folder == "data/global/speech") return true;
    const std::string levels = "data/levels/";
    if (folder.compare(0, levels.size(), levels) != 0) return false;
    const size_t slash = folder.find('/', levels.size());
    if (slash == std::string::npos || slash == levels.size()) return false;
    const std::string kind = folder.substr(slash + 1);
    return kind == "sound" || kind == "speech" || kind == "music";
}

bool PackMayHold(const std::string& path) {
    std::string lower = path;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.size() > 5 && lower.compare(lower.size() - 5, 5, ".rsrc") == 0) lower.resize(lower.size() - 5);
    const size_t slash = lower.rfind('/');
    const std::string folder = slash == std::string::npos ? std::string() : lower.substr(0, slash);
    const std::string name = slash == std::string::npos ? lower : lower.substr(slash + 1);
    if (folder.find('.') != std::string::npos || name.empty() || name[0] == '.') return false;
    const size_t dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    const std::string kind = name.substr(dot);
    for (const char* allowed : {".tga", ".wct", ".ani", ".cur", ".sf2", ".ttf", ".bf4", ".mov"})
        if (kind == allowed) return true;
    return (kind == ".sdt" || kind == ".map") && IsSoundBankFolder(folder);
}

bool IsGameFile(const std::string& path) {
    const fs::path found = FindUnder(config().gameDir, SplitGamePath(path));
    std::error_code ec;
    return !found.empty() && fs::is_regular_file(found, ec);
}

// A game path's parts as a path from the game folder, '/' separated.
static std::string JoinParts(const std::vector<std::string>& parts) {
    std::string path;
    for (const std::string& part : parts) path += (path.empty() ? "" : "/") + part;
    return path;
}

fs::path FindHostPath(const std::string& gamePath) {
    const auto parts = SplitGamePath(gamePath);
    fs::path found = FindUnder(config().userDir, parts);
    if (!found.empty()) return found;
    // The chosen language (language_pack.h): its text, speech and pictures, at their places in the game folder.
    if (LanguagePackCovers(parts)) {
        found = FindUnder(LanguagePackFolder(), parts);
        if (!found.empty()) return found;
    }
    // From a pack only a file a pack may hold, so neither a folder nor another kind of file there (one put in
    // ".unpacked" by hand, or unpacked by older rules) takes a game file's place.
    if (PackMayHold(JoinParts(parts))) {
        for (const fs::path& pack : PackDirs()) {
            found = FindUnder(pack, parts);
            std::error_code ec;
            if (!found.empty() && fs::is_regular_file(found, ec)) return found;
        }
    }
    // A Gold ride (gold_rides.h) at its place in the game folder, before the base game's copy of it.
    found = GoldRideHostPath(parts);
    if (!found.empty()) return found;
    return FindUnder(config().gameDir, parts);
}

fs::path HostPathForWriting(const std::string& gamePath) {
    const auto parts = SplitGamePath(gamePath);
    fs::path current = config().userDir;
    std::error_code ec;
    for (size_t i = 0; i < parts.size(); ++i) {
        // Reuse an existing entry whatever its case, so "Save" and "save" stay one folder.
        fs::path existing = FindChild(current, parts[i]);
        current = existing.empty() ? current / HostName(parts[i]) : existing;
        if (i + 1 < parts.size()) fs::create_directories(current, ec);
    }
    if (parts.empty()) fs::create_directories(current, ec);
    return current;
}

// hfsdump (the project's disc extractor) writes "<name>.finfo" and "<name>.rsrc" next to
// each file; they are not part of the game's folder.
static bool IsDumpSideFile(const fs::path& path) {
    // Compared as paths: string() throws on Windows for an extension the ANSI code page cannot spell.
    const fs::path ext = path.extension();
    if (ext != fs::path(".finfo") && ext != fs::path(".rsrc")) return false;
    std::error_code ec;
    fs::path base = path;
    base.replace_extension();
    return fs::exists(base, ec);
}

// A file's last write time as the game's FILETIME, made as the Mac's glue made it: the local date and time of the write in
// seconds since 1904, in 100 ns ticks with the constant added that FileTimeToSystemTime takes off again (SystemTimeToFileTime,
// macdoze_misc.cpp). The Load and Save panels show it as the time the park was saved, and the game's directory searches
// compare it to find a player's newest park file. (A true Win32 FILETIME, UTC from 1601, showed as UTC plus 6 minutes 17
// seconds: the glue's constant is 377.0368 s short of 1601 to 1904.) From tpw-port PR #18 by samueleagle91.
uint64_t FileTimeOf(const fs::path& path) {
    struct stat info;
    if (::stat(path.c_str(), &info) != 0) return 0;
    const std::time_t written = info.st_mtime;
    std::tm local{};
    if (localtime_r(&written, &local) == nullptr) return 0;
    NS_MacDoze::_SYSTEMTIME time{};
    time.wYear = static_cast<uint16_t>(local.tm_year + 1900);
    time.wMonth = static_cast<uint16_t>(local.tm_mon + 1);
    time.wDay = static_cast<uint16_t>(local.tm_mday);
    time.wHour = static_cast<uint16_t>(local.tm_hour);
    time.wMinute = static_cast<uint16_t>(local.tm_min);
    time.wSecond = static_cast<uint16_t>(local.tm_sec);
    NS_MacDoze::tagFILETIME fileTime{};
    NS_MacDoze::SystemTimeToFileTime(&time, &fileTime);
    return (static_cast<uint64_t>(fileTime.dwHighDateTime) << 32) | fileTime.dwLowDateTime;
}

std::vector<DirEntry> ListGameFolder(const std::string& gamePath) {
    const auto parts = SplitGamePath(gamePath);
    const std::string inside = JoinParts(parts);
    // The user folder, the chosen language (language_pack.h), the asset packs (packs.h), the game folder; from the
    // language only the folders it covers (in data/Language its own folder alone, so the game takes it as its language),
    // from a pack only the files a pack may hold. The Gold rides (gold_rides.h) are listed with the game folder, as the
    // Gold installer's copies would be in it: a Gold copy of a base ride takes the base entry's place, a new ride is
    // sorted in among the base ones.
    enum class Root { kUser, kLanguage, kPack, kGame };
    std::vector<std::pair<fs::path, Root>> roots{{config().userDir, Root::kUser}};
    if (LanguagePackCovers(parts) || LanguagePackLists(parts, LanguagePackName()))
        roots.emplace_back(LanguagePackFolder(), Root::kLanguage);
    for (const fs::path& pack : PackDirs()) roots.emplace_back(pack, Root::kPack);
    roots.emplace_back(config().gameDir, Root::kGame);
    std::vector<DirEntry> entries;
    for (size_t r = 0; r < roots.size(); ++r) {
        const auto& [root, source] = roots[r];
        const bool pack = source == Root::kPack;
        const bool game = source == Root::kGame;
        const std::vector<DirEntry> gold = game ? GoldRideEntries(inside) : std::vector<DirEntry>();
        const fs::path folder = FindUnder(root, parts);
        std::error_code ec;
        const bool listed = !folder.empty() && fs::is_directory(folder, ec);
        if (!listed && gold.empty()) continue;
        std::vector<DirEntry> found;
        for (fs::directory_iterator it(folder, ec), end; listed && !ec && it != end; it.increment(ec)) {
            const std::string name = GameName(it->path().filename());
            if (name.empty() || name[0] == '.' || IsDumpSideFile(it->path())) continue;
            std::error_code kind;
            if (pack && (!it->is_regular_file(kind) || !PackMayHold(inside.empty() ? name : inside + "/" + name))) continue;
            if (source == Root::kLanguage && !LanguagePackLists(parts, name)) continue;
            const bool seen = std::any_of(entries.begin(), entries.end(),
                                          [&](const DirEntry& e) { return EqualNoCase(e.name, name); });
            if (seen) continue;
            std::error_code sizeError;
            const bool directory = it->is_directory(sizeError);
            const uint64_t size = directory ? 0 : it->file_size(sizeError);
            found.push_back({name, directory, sizeError ? 0 : size, FileTimeOf(it->path())});
        }
        for (const DirEntry& ride : gold) {
            const auto same = [&](const DirEntry& e) { return EqualNoCase(e.name, ride.name); };
            if (std::any_of(entries.begin(), entries.end(), same)) continue;   // the user folder or a pack has it
            const auto base = std::find_if(found.begin(), found.end(), same);
            if (base == found.end()) found.push_back(ride);
            else if (!ride.isDirectory) *base = ride;   // the Gold copy, with its size and time
        }
        // A fixed order, so runs are repeatable (the Mac listed a folder alphabetically).
        std::sort(found.begin(), found.end(), [](const DirEntry& a, const DirEntry& b) {
            return std::lexicographical_compare(a.name.begin(), a.name.end(), b.name.begin(), b.name.end(),
                                                [](char x, char y) {
                                                    return std::tolower(static_cast<unsigned char>(x)) <
                                                           std::tolower(static_cast<unsigned char>(y));
                                                });
        });
        entries.insert(entries.end(), found.begin(), found.end());
    }
    return entries;
}

std::string EditionOf(const fs::path& gameDir) {
    const fs::path language = FindUnder(gameDir, {"data", "Language"});
    return !language.empty() && !FindChild(language, "American").empty() ? "stp" : "tpw";
}

}  // namespace tpw

// ---- The application's folder (UProcess, UFolder) --------------------------------------
namespace SamsUtilities {

static void Assign(UString& out, const std::string& text) {
    out.~UString();
    new (&out) UString(text.c_str(), static_cast<uint32_t>(text.size()));
}

UFileItem::~UFileItem() {}

// The base class's GetItem finds nothing; the platform's folders override it.
UFileItem* UFolder::GetItem(const UString&, int32_t) { return nullptr; }

namespace {
// A folder of the game, named by its Mac path (with the trailing ':').
class GameFolder : public UFolder {
public:
    explicit GameFolder(std::string macPath) : mMacPath(std::move(macPath)) {}
    void GetPath(UString& path) override { Assign(path, mMacPath); }
    UFileItem* GetItem(const UString& name, int32_t type) override {
        const std::string child = mMacPath + name.mText;
        const fs::path host = tpw::FindHostPath(child);
        std::error_code ec;
        if (host.empty()) return nullptr;
        if (type == 0x666f6c64 /* 'fold' */ && !fs::is_directory(host, ec)) return nullptr;
        return new GameFolder(child + ":");
    }

private:
    std::string mMacPath;
};
}  // namespace

UProcess gThisProcess;

UFolder* UProcess::GetFolder() {
    static GameFolder folder(tpw::kProjectDirMac);
    return &folder;
}

}  // namespace SamsUtilities

// ---- Directory searches (macdoze) ------------------------------------------------------
namespace NS_MacDoze {

namespace {
// The state behind a FindFirstFile handle.
struct FindState {
    std::vector<tpw::DirEntry> entries;
    size_t next = 0;
};

// Windows wildcards as the game uses them: '*' any run of characters, '?' any one.
bool WildcardMatch(const char* pattern, const char* name) {
    if (*pattern == '\0') return *name == '\0';
    if (*pattern == '*') {
        for (const char* rest = name;; ++rest) {
            if (WildcardMatch(pattern + 1, rest)) return true;
            if (*rest == '\0') return false;
        }
    }
    if (*name == '\0') return false;
    if (*pattern != '?' &&
        std::tolower(static_cast<unsigned char>(*pattern)) != std::tolower(static_cast<unsigned char>(*name)))
        return false;
    return WildcardMatch(pattern + 1, name + 1);
}

void FillFindData(const tpw::DirEntry& entry, _WIN32_FIND_DATAA* data) {
    std::memset(data, 0, sizeof *data);
    data->dwFileAttributes = entry.isDirectory ? 0x10 : 0x80;   // FILE_ATTRIBUTE_DIRECTORY / NORMAL
    data->nFileSizeHigh = static_cast<uint32_t>(entry.size >> 32);
    data->nFileSizeLow = static_cast<uint32_t>(entry.size);
    for (uint32_t* time : {data->ftCreationTime, data->ftLastAccessTime, data->ftLastWriteTime}) {
        time[0] = static_cast<uint32_t>(entry.fileTime);
        time[1] = static_cast<uint32_t>(entry.fileTime >> 32);
    }
    std::strncpy(data->cFileName, entry.name.c_str(), sizeof data->cFileName - 1);
}

InternalHandle* const kInvalidHandle = reinterpret_cast<InternalHandle*>(static_cast<intptr_t>(-1));
}  // namespace

// The pattern's last component may hold wildcards; the folder before it is listed.
InternalHandle* FindFirstFile(const char* pattern, _WIN32_FIND_DATAA* data) {
    const std::string text(pattern);
    const size_t cut = text.find_last_of(":\\/");
    const std::string folder = cut == std::string::npos ? std::string() : text.substr(0, cut + 1);
    const std::string mask = cut == std::string::npos ? text : text.substr(cut + 1);
    auto* state = new FindState;
    for (const tpw::DirEntry& entry : tpw::ListGameFolder(folder))
        if (WildcardMatch(mask.c_str(), entry.name.c_str())) state->entries.push_back(entry);
    if (state->entries.empty()) {
        delete state;
        return kInvalidHandle;
    }
    FillFindData(state->entries[state->next++], data);
    return reinterpret_cast<InternalHandle*>(state);
}

// macdoze 0x10006310 (240 bytes): both times become seconds since the Mac epoch (1 January 1904: the FILETIME less
// 95616284229632000 ticks, divided by 10,000,000 with the signed 64-bit divide, which truncates towards zero) and those are
// compared: 1 when `a` is later, 0 when they are the same second, else -1. Times within one second are the same.
int32_t CompareFileTime(const tagFILETIME* a, const tagFILETIME* b) {
    const auto seconds = [](const tagFILETIME* time) {
        const uint64_t ticks = (static_cast<uint64_t>(time->dwHighDateTime) << 32) + time->dwLowDateTime;
        return static_cast<int64_t>(ticks - 95616284229632000ull) / 10000000;
    };
    const int64_t first = seconds(a);
    const int64_t second = seconds(b);
    if (first == second) return 0;
    return second < first ? 1 : -1;
}

// 0xffffffff when there is no such file or folder; FILE_ATTRIBUTE_DIRECTORY (0x10) for a
// folder, FILE_ATTRIBUTE_NORMAL (0x80) for a file. (The Mac shim also reported a locked
// file as read-only; nothing is locked here.)
uint32_t GetFileAttributes(const char* name) {
    const fs::path host = tpw::FindHostPath(name);
    if (host.empty()) return 0xffffffffu;
    std::error_code ec;
    return fs::is_directory(host, ec) ? 0x10 : 0x80;
}

// Nonzero when the folder was made. It goes to the user folder; an existing one (in
// either folder) is a failure, as it was for the Mac's FSpDirCreate.
int32_t CreateDirectory(const char* name, void*) {
    if (!tpw::FindHostPath(name).empty()) return 0;
    std::error_code ec;
    return fs::create_directory(tpw::HostPathForWriting(name), ec) ? 1 : 0;
}

int32_t FindNextFile(InternalHandle* handle, _WIN32_FIND_DATAA* data) {
    if (handle == nullptr || handle == kInvalidHandle) return 0;
    auto* state = reinterpret_cast<FindState*>(handle);
    if (state->next >= state->entries.size()) return 0;
    FillFindData(state->entries[state->next++], data);
    return 1;
}

int32_t FindClose(InternalHandle* handle) {
    if (handle == nullptr || handle == kInvalidHandle) return 0;
    delete reinterpret_cast<FindState*>(handle);
    return 1;
}

}  // namespace NS_MacDoze

// ---- File Manager and Resource Manager calls the game makes directly -------------------
namespace {
const int16_t kGameVolume = -1;          // vRefNum of the one volume
const int16_t kDebugOptionsFile = 100;   // resource file reference number
int16_t gCurrentResFile = 0;
char gDebugOptionsBytes[4];
char* gDebugOptionsPointer = gDebugOptionsBytes;

bool DebugOptionsWanted() {
    for (uint8_t value : tpw::config().debugOptions)
        if (value != 0xff) return true;
    return false;
}

std::string FromPascal(const unsigned char* text) {
    return std::string(reinterpret_cast<const char*>(text + 1), text[0]);
}
}  // namespace

// A full or partial Mac path to an FSSpec. Everything the game names lives on one volume.
// The "debug options" file exists when the launcher asked for an override.
// The host file of each name a spec was made for (a spec keeps only the name; the movie
// player hands one to QuickTime). Never destroyed.
static std::map<std::string, fs::path>& SpecPaths() {
    static auto* paths = new std::map<std::string, fs::path>;
    return *paths;
}

OSErr FSMakeFSSpec(int16_t, int32_t, const unsigned char* fileName, FSSpec* spec) {
    const std::string path = FromPascal(fileName);
    const auto parts = tpw::SplitGamePath(path);
    const bool debugOptions = parts.size() == 1 && parts[0] == "debug options";
    const fs::path host = debugOptions ? fs::path() : tpw::FindHostPath(path);
    if (debugOptions ? !DebugOptionsWanted() : host.empty()) return -43;   // fnfErr
    spec->vRefNum = kGameVolume;
    spec->parID = debugOptions ? kDebugOptionsFile : 2;
    const std::string name = parts.empty() ? std::string() : parts.back();
    SamsUtilities::UString::c2pcpy(spec->name, name.substr(0, 63).c_str());
    if (!debugOptions) SpecPaths()[name.substr(0, 63)] = host;
    return 0;
}

fs::path tpw::HostPathOfSpec(const FSSpec* spec) {
    const auto found = SpecPaths().find(FromPascal(spec->name));
    return found != SpecPaths().end() ? found->second : fs::path();
}

// The game's CD check wants its volume locked (bit 7 or 15 of the attributes), as a CD is.
// The game folder is treated as that read-only disc: nothing is ever written to it.
OSErr PBHGetVInfoSync(HVolumeParam* paramBlock) {
    if (paramBlock->ioVRefNum != kGameVolume) {
        paramBlock->ioResult = -35;   // nsvErr
        return -35;
    }
    paramBlock->ioVAtrb = static_cast<int16_t>(0x8080);
    paramBlock->ioResult = 0;
    return 0;
}

// The current volume and folder (InterfaceLib, bullfrog glue 0x1006b55c): the one volume, named
// like the game folder, and its root folder (dirID 2, as FSMakeFSSpec gives).
OSErr HGetVol(unsigned char* volName, int16_t* vRefNum, int32_t* dirID) {
    if (volName != nullptr) SamsUtilities::UString::c2pcpy(volName, "TPW");
    if (vRefNum != nullptr) *vRefNum = kGameVolume;
    if (dirID != nullptr) *dirID = 2;
    return 0;
}

// UpperString (glue 0x1006b574) and p2cstr (glue 0x1006b58c). LbFile_GetCurrentDrive passes the
// volume name's length byte as the pointer, so an address in the Mac's low memory (below the
// first page) is left alone.
static bool IsRealPointer(const unsigned char* text) { return reinterpret_cast<uintptr_t>(text) >= 4096; }

void UpperString(unsigned char* text, bool) {
    if (!IsRealPointer(text)) return;
    for (unsigned char* c = text + 1; c < text + 1 + text[0]; ++c) *c = static_cast<unsigned char>(std::toupper(*c));
}

char* p2cstr(unsigned char* text) {
    if (!IsRealPointer(text)) return reinterpret_cast<char*>(text);
    const size_t length = text[0];
    std::memmove(text, text + 1, length);
    text[length] = '\0';
    return reinterpret_cast<char*>(text);
}

int16_t FSpOpenResFile(const FSSpec* spec, int8_t) {
    if (spec->parID != kDebugOptionsFile) return -1;
    gCurrentResFile = kDebugOptionsFile;
    return kDebugOptionsFile;
}

int16_t CurResFile() { return gCurrentResFile; }

void UseResFile(int16_t refNum) { gCurrentResFile = refNum; }

void CloseResFile(int16_t refNum) {
    if (gCurrentResFile == refNum) gCurrentResFile = 0;
}

Handle GetNamedResource(uint32_t type, const unsigned char* name) {
    if (gCurrentResFile != kDebugOptionsFile || type != 0x444f5054 /* 'DOPT' */ ||
        FromPascal(name) != "debug options")
        return nullptr;
    std::memcpy(gDebugOptionsBytes, tpw::config().debugOptions, sizeof gDebugOptionsBytes);
    return &gDebugOptionsPointer;
}
