// registry.cpp: the Windows registry calls of macdoze (Reg*A). The Mac library kept the registry
// as a tree of folders and files in the Preferences folder: a key is a folder, a value a file
// holding its type and data. The host keeps the same shape under <userDir>/registry, one
// folder per root key (HKEY_CURRENT_USER and the others).
//
// A key handle is a small number the table below maps to a folder; the roots (0x80000000 to
// 0x80000006, TbRegistryKey's `i | 0x80000000`) map to their own folders and are never closed.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "platform.h"
#include "platform/macdoze.h"
#include "path_text.h"

namespace fs = std::filesystem;

namespace NS_MacDoze {

namespace {
constexpr int32_t kSuccess = 0;
constexpr int32_t kFileNotFound = 2;
constexpr int32_t kAccessDenied = 5;
constexpr int32_t kMoreData = 0xea;        // the Mac's answer when the caller's buffer is too small
constexpr int32_t kNoMoreItems = 0x103;
constexpr int32_t kBadKey = 0x3f2;         // the Mac's answer when the registry could not be reached
constexpr HKEY kFirstHandle = 0x100;
constexpr HKEY kFirstRoot = 0x80000000u;
constexpr HKEY kLastRoot = 0x80000006u;

const char* const kRootFolders[] = {"HKEY_CLASSES_ROOT",     "HKEY_CURRENT_USER",   "HKEY_LOCAL_MACHINE", "HKEY_USERS",
                                    "HKEY_PERFORMANCE_DATA", "HKEY_CURRENT_CONFIG", "HKEY_DYN_DATA"};

std::mutex gLock;
std::unordered_map<HKEY, fs::path> gKeys;
HKEY gNextHandle = kFirstHandle;

// A name as a file name: letters, digits, space, '_' and '-' stay, every other byte is %XX. '.' is
// encoded, so a value file ("<name>.val") never has the name of a key's folder.
std::string Encode(const std::string& name) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : name) {
        if (std::isalnum(c) || c == ' ' || c == '_' || c == '-') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string Decode(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() &&std::isxdigit(static_cast<unsigned char>(text[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
            out += static_cast<char>(std::stoi(text.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += text[i];
        }
    }
    return out;
}

bool EqualNoCase(const std::string& a, const std::string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

// The entry of `folder` named `file` (already encoded), whatever its case (the registry ignores
// case). Empty when there is none.
fs::path FindEntry(const fs::path& folder, const std::string& file) {
    std::error_code ec;
    const fs::path exact = folder / file;
    if (fs::exists(exact, ec)) return exact;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
        if (EqualNoCase(tpw::PathText(it->path().filename()), file)) return it->path();
    return {};
}

// The folder of a key handle; empty when the handle is not one. A root's folder is made when first used.
fs::path FolderOf(HKEY key) {
    if (key >= kFirstRoot && key <= kLastRoot) {
        const fs::path folder = tpw::config().userDir / "registry" / kRootFolders[key - kFirstRoot];
        std::error_code ec;
        fs::create_directories(folder, ec);
        return ec ? fs::path() : folder;
    }
    const auto found = gKeys.find(key);
    return found != gKeys.end() ? found->second : fs::path();
}

// "a\b/c" as its parts; the Mac split a key path at both slashes.
std::vector<std::string> SplitKeyPath(const char* path) {
    std::vector<std::string> parts;
    std::string part;
    for (const char* p = path != nullptr ? path : ""; ; ++p) {
        if (*p == '\\' || *p == '/' || *p == '\0') {
            if (!part.empty()) parts.push_back(part);
            part.clear();
            if (*p == '\0') break;
        } else {
            part += *p;
        }
    }
    return parts;
}

HKEY NewHandle(const fs::path& folder) {
    const HKEY handle = gNextHandle++;
    gKeys[handle] = folder;
    return handle;
}

// Follows `path` below `start`, making the folders when `create`. Empty when a part is missing (and
// not created) or the disk refuses. *created says whether any folder was made.
fs::path Descend(const fs::path& start, const char* path, bool create, bool* created) {
    fs::path current = start;
    std::error_code ec;
    for (const std::string& part : SplitKeyPath(path)) {
        const std::string encoded = Encode(part);
        fs::path next = FindEntry(current, encoded);
        if (next.empty() || !fs::is_directory(next, ec)) {
            if (!create) return {};
            next = current / encoded;
            if (!fs::create_directory(next, ec) && ec) return {};
            if (created != nullptr) *created = true;
        }
        current = next;
    }
    return current;
}

fs::path ValueFile(const fs::path& folder, const char* name) {
    return FindEntry(folder, Encode(name != nullptr ? name : "") + ".val");
}

// A value file: the 4 byte type, then the data.
bool ReadValue(const fs::path& file, uint32_t* type, std::vector<char>* data) {
    std::ifstream in(file, std::ios::binary);
    uint32_t kind = 0;
    if (!in.read(reinterpret_cast<char*>(&kind), 4)) return false;
    if (type != nullptr) *type = kind;
    if (data != nullptr) data->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

// The names in a folder, sorted: the sub-keys (folders) or the values (files ending in ".val").
std::vector<std::string> ListNames(const fs::path& folder, bool keys) {
    std::vector<std::string> names;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::string file = tpw::PathText(it->path().filename());
        std::error_code kindError;
        const bool isFolder = it->is_directory(kindError);
        if (keys) {
            if (isFolder) names.push_back(Decode(file));
        } else if (!isFolder && file.size() >= 4 && file.compare(file.size() - 4, 4, ".val") == 0) {
            names.push_back(Decode(file.substr(0, file.size() - 4)));
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

// Copies a name out the way RegEnum* does: *size is the room (with the NUL) and becomes the
// length (without it); the Win32 answer for a name that does not fit is ERROR_MORE_DATA.
int32_t GiveName(const std::string& name, char* out, uint32_t* size) {
    if (out == nullptr || size == nullptr) return kBadKey;
    if (*size < name.size() + 1) return kMoreData;
    std::memcpy(out, name.c_str(), name.size() + 1);
    *size = static_cast<uint32_t>(name.size());
    return kSuccess;
}
}  // namespace

// macdoze 0x1000735c: RegOpenKeyExA with full access.
int32_t RegOpenKeyA(HKEY key, const char* subKey, HKEY* result) {
    return RegOpenKeyExA(key, subKey, 0, 0xf003f, result);
}

// macdoze 0x100073b0 made (or opened) a folder per part of the path and answered the new folder as
// the handle, 0x3f2 when it could not. The host opens only keys that exist (the Windows rule, which
// TbRegistryKey::GetItemType relies on to tell a key from a value); RegCreateKeyExA makes them.
int32_t RegOpenKeyExA(HKEY key, const char* subKey, uint32_t, uint32_t, HKEY* result) {
    std::lock_guard<std::mutex> lock(gLock);
    const fs::path start = FolderOf(key);
    if (start.empty() || result == nullptr) return kBadKey;
    const fs::path folder = Descend(start, subKey, false, nullptr);
    if (folder.empty()) return kFileNotFound;
    *result = NewHandle(folder);
    return kSuccess;
}

// macdoze 0x100078dc was RegOpenKeyExA; the host also reports whether the key was new
// (REG_CREATED_NEW_KEY 1, REG_OPENED_EXISTING_KEY 2) when the caller asks.
int32_t RegCreateKeyExA(HKEY key, const char* subKey, uint32_t, char*, uint32_t, uint32_t, void*, HKEY* result,
                        uint32_t* disposition) {
    std::lock_guard<std::mutex> lock(gLock);
    const fs::path start = FolderOf(key);
    if (start.empty() || result == nullptr) return kBadKey;
    bool created = false;
    const fs::path folder = Descend(start, subKey, true, &created);
    if (folder.empty()) return kBadKey;
    *result = NewHandle(folder);
    if (disposition != nullptr) *disposition = created ? 1 : 2;
    return kSuccess;
}

// macdoze 0x1000792c freed the handle unless it was a root, and answered 0.
int32_t RegCloseKey(HKEY key) {
    std::lock_guard<std::mutex> lock(gLock);
    gKeys.erase(key);
    return kSuccess;
}

// macdoze 0x100075a8 wrote a file holding the type and the data, 0 or 0x3f2.
int32_t RegSetValueExA(HKEY key, const char* name, uint32_t, uint32_t type, const void* data, uint32_t size) {
    std::lock_guard<std::mutex> lock(gLock);
    const fs::path folder = FolderOf(key);
    if (folder.empty()) return kBadKey;
    fs::path file = ValueFile(folder, name);
    if (file.empty()) file = folder / (Encode(name != nullptr ? name : "") + ".val");
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(&type), 4);
    if (data != nullptr && size != 0) out.write(static_cast<const char*>(data), size);
    return out ? kSuccess : kBadKey;
}

// macdoze 0x10007710: when *size is smaller than the data it sets *size and answers 0xea; otherwise it
// sets *size and reads the data, answering 0 (0x3f2 when the file could not be read). The caller may leave
// the type, the data or the size out; with no buffer the host answers the size alone (TbRegistryKey's
// GetValueSize passes a size it never set). A value that does not exist answers ERROR_FILE_NOT_FOUND.
int32_t RegQueryValueExA(HKEY key, const char* name, uint32_t*, uint32_t* type, void* data, uint32_t* size) {
    std::lock_guard<std::mutex> lock(gLock);
    const fs::path folder = FolderOf(key);
    if (folder.empty()) return kBadKey;
    const fs::path file = ValueFile(folder, name);
    if (file.empty()) return kFileNotFound;
    uint32_t kind = 0;
    std::vector<char> bytes;
    if (!ReadValue(file, &kind, &bytes)) return kBadKey;
    if (type != nullptr) *type = kind;
    if (size == nullptr) return kSuccess;
    const uint32_t have = static_cast<uint32_t>(bytes.size());
    if (data == nullptr) {
        *size = have;
        return kSuccess;
    }
    if (*size < have) {
        *size = have;
        return kMoreData;
    }
    std::memcpy(data, bytes.data(), have);
    *size = have;
    return kSuccess;
}

// macdoze 0x100079d8, 0x10007a00 and 0x10007a28 answered 0 and wrote nothing, which leaves
// TbRegistryKey::ListItems working from counts it never got. The host lists the key's folder.
int32_t RegEnumValueA(HKEY key, uint32_t index, char* name, uint32_t* nameSize, uint32_t*, uint32_t* type, void* data,
                      uint32_t* dataSize) {
    std::lock_guard<std::mutex> lock(gLock);
    const fs::path folder = FolderOf(key);
    if (folder.empty()) return kBadKey;
    const std::vector<std::string> names = ListNames(folder, false);
    if (index >= names.size()) return kNoMoreItems;
    const int32_t answer = GiveName(names[index], name, nameSize);
    if (answer != kSuccess) return answer;
    const fs::path file = ValueFile(folder, names[index].c_str());
    uint32_t kind = 0;
    std::vector<char> bytes;
    if (file.empty() || !ReadValue(file, &kind, &bytes)) return kBadKey;
    if (type != nullptr) *type = kind;
    if (data != nullptr && dataSize != nullptr) {
        const uint32_t have = static_cast<uint32_t>(bytes.size());
        if (*dataSize < have) {
            *dataSize = have;
            return kMoreData;
        }
        std::memcpy(data, bytes.data(), have);
        *dataSize = have;
    } else if (dataSize != nullptr) {
        *dataSize = static_cast<uint32_t>(bytes.size());
    }
    return kSuccess;
}

int32_t RegEnumKeyExA(HKEY key, uint32_t index, char* name, uint32_t* nameSize, uint32_t*, char* classText,
                      uint32_t* classSize, tagFILETIME* lastWrite) {
    std::lock_guard<std::mutex> lock(gLock);
    const fs::path folder = FolderOf(key);
    if (folder.empty()) return kBadKey;
    const std::vector<std::string> names = ListNames(folder, true);
    if (index >= names.size()) return kNoMoreItems;
    const int32_t answer = GiveName(names[index], name, nameSize);
    if (answer != kSuccess) return answer;
    if (classText != nullptr && classSize != nullptr && *classSize != 0) classText[0] = '\0';
    if (classSize != nullptr) *classSize = 0;
    if (lastWrite != nullptr) std::memset(lastWrite, 0, sizeof *lastWrite);
    return kSuccess;
}

int32_t RegQueryInfoKeyA(HKEY key, char* classText, uint32_t* classSize, uint32_t*, uint32_t* subKeys,
                         uint32_t* maxSubKeyLen, uint32_t* maxClassLen, uint32_t* values, uint32_t* maxValueNameLen,
                         uint32_t* maxValueLen, uint32_t* securityLen, tagFILETIME* lastWrite) {
    std::lock_guard<std::mutex> lock(gLock);
    const fs::path folder = FolderOf(key);
    if (folder.empty()) return kBadKey;
    const std::vector<std::string> keys = ListNames(folder, true);
    const std::vector<std::string> names = ListNames(folder, false);
    uint32_t longestKey = 0, longestName = 0, longestValue = 0;
    for (const std::string& name : keys) longestKey = std::max<uint32_t>(longestKey, name.size());
    for (const std::string& name : names) {
        longestName = std::max<uint32_t>(longestName, name.size());
        std::vector<char> bytes;
        const fs::path file = ValueFile(folder, name.c_str());
        if (!file.empty() && ReadValue(file, nullptr, &bytes)) longestValue = std::max<uint32_t>(longestValue, bytes.size());
    }
    if (classText != nullptr && classSize != nullptr && *classSize != 0) classText[0] = '\0';
    if (classSize != nullptr) *classSize = 0;
    if (subKeys != nullptr) *subKeys = static_cast<uint32_t>(keys.size());
    if (maxSubKeyLen != nullptr) *maxSubKeyLen = longestKey;
    if (maxClassLen != nullptr) *maxClassLen = 0;
    if (values != nullptr) *values = static_cast<uint32_t>(names.size());
    if (maxValueNameLen != nullptr) *maxValueNameLen = longestName;
    if (maxValueLen != nullptr) *maxValueLen = longestValue;
    if (securityLen != nullptr) *securityLen = 0;
    if (lastWrite != nullptr) std::memset(lastWrite, 0, sizeof *lastWrite);
    return kSuccess;
}

// macdoze 0x10007a54 and 0x10007a80 answered 0 and removed nothing. The host removes the value file, or
// the key's folder (which, as on Windows, must have no sub-keys).
int32_t RegDeleteValueA(HKEY key, const char* name) {
    std::lock_guard<std::mutex> lock(gLock);
    const fs::path folder = FolderOf(key);
    if (folder.empty()) return kBadKey;
    const fs::path file = ValueFile(folder, name);
    std::error_code ec;
    if (file.empty() || !fs::remove(file, ec)) return kFileNotFound;
    return kSuccess;
}

int32_t RegDeleteKeyA(HKEY key, const char* subKey) {
    std::lock_guard<std::mutex> lock(gLock);
    const fs::path start = FolderOf(key);
    if (start.empty()) return kBadKey;
    const fs::path folder = Descend(start, subKey, false, nullptr);
    if (folder.empty() || folder == start) return kFileNotFound;
    if (!ListNames(folder, true).empty()) return kAccessDenied;
    std::error_code ec;
    fs::remove_all(folder, ec);
    return ec ? kAccessDenied : kSuccess;
}

}  // namespace NS_MacDoze
