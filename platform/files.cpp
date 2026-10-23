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

#include "platform.h"
#include "platform/macdoze.h"

namespace fs = std::filesystem;

namespace tpw {

const char kProjectDirMac[] = "TPW:Theme Park Data:";
const char kRemoteDirMac[] = "Theme Park:Theme Park Data:";

static bool EqualNoCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
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
    for (char c : path) {
        if (c == ':' || c == '\\' || c == '/') flush();
        else part.push_back(c);
    }
    flush();
    return parts;
}

// `name` inside `folder`, matched ignoring case: the exact name if it exists, else the
// first entry that differs only in case. Empty if there is none.
static fs::path FindChild(const fs::path& folder, const std::string& name) {
    std::error_code ec;
    const fs::path exact = folder / name;
    if (fs::exists(exact, ec)) return exact;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
        if (EqualNoCase(it->path().filename().string(), name)) return it->path();
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

fs::path FindHostPath(const std::string& gamePath) {
    const auto parts = SplitGamePath(gamePath);
    fs::path found = FindUnder(config().userDir, parts);
    if (found.empty()) found = FindUnder(config().gameDir, parts);
    return found;
}

fs::path HostPathForWriting(const std::string& gamePath) {
    const auto parts = SplitGamePath(gamePath);
    fs::path current = config().userDir;
    std::error_code ec;
    for (size_t i = 0; i < parts.size(); ++i) {
        // Reuse an existing entry whatever its case, so "Save" and "save" stay one folder.
        fs::path existing = FindChild(current, parts[i]);
        current = existing.empty() ? current / parts[i] : existing;
        if (i + 1 < parts.size()) fs::create_directories(current, ec);
    }
    if (parts.empty()) fs::create_directories(current, ec);
    return current;
}

// hfsdump (the project's disc extractor) writes "<name>.finfo" and "<name>.rsrc" next to
// each file; they are not part of the game's folder.
static bool IsDumpSideFile(const fs::path& path) {
    const std::string ext = path.extension().string();
    if (ext != ".finfo" && ext != ".rsrc") return false;
    std::error_code ec;
    fs::path base = path;
    base.replace_extension();
    return fs::exists(base, ec);
}

// A file's last write time as the game's FILETIME, made as the Mac's glue made it: the local date and time of the write in
// seconds since 1904, in 100 ns ticks with the constant added that FileTimeToSystemTime takes off again (SystemTimeToFileTime,
// macdoze_misc.cpp). The Load and Save panels show it as the time the park was saved, and the game's directory searches
// compare it to find a player's newest park file. (A true Win32 FILETIME, UTC from 1601, showed as UTC plus 6 minutes 17
// seconds: the glue's constant is 377.0368 s short of 1601 to 1904.)
static uint64_t FileTimeOf(const fs::path& path) {
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
    std::vector<DirEntry> entries;
    for (const fs::path& root : {config().userDir, config().gameDir}) {
        const fs::path folder = FindUnder(root, parts);
        std::error_code ec;
        if (folder.empty() || !fs::is_directory(folder, ec)) continue;
        std::vector<DirEntry> found;
        for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = it->path().filename().string();
            if (name.empty() || name[0] == '.' || IsDumpSideFile(it->path())) continue;
            const bool seen = std::any_of(entries.begin(), entries.end(),
                                          [&](const DirEntry& e) { return EqualNoCase(e.name, name); });
            if (seen) continue;
            std::error_code sizeError;
            const bool directory = it->is_directory(sizeError);
            const uint64_t size = directory ? 0 : it->file_size(sizeError);
            found.push_back({name, directory, sizeError ? 0 : size, FileTimeOf(it->path())});
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
