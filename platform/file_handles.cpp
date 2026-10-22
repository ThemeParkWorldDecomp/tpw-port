// file_handles.cpp: macdoze's Win32 file calls (CreateFile, ReadFile, ...), which are how
// the bullfrog file system reaches the disk. Paths go through the mapping in files.cpp:
// files are read from the user folder or the game folder and created or written only in
// the user folder (a game file opened for writing is copied there first).
//
// Where the Mac shim's behaviour differs from Win32 it is the shim that is followed, since
// that is what the game ran on (macdoze shared: CreateFile 0x10003bcc, ReadFile 0x10003eb4,
// SetFilePointer 0x10004644, GetFileSize 0x10004578).
#include <cstdio>
#include <cstring>
#include <system_error>
#include <unordered_set>
#include <vector>

#include "handoff.h"
#include "platform.h"
#include "platform/macdoze.h"

namespace fs = std::filesystem;

namespace NS_MacDoze {

namespace {
struct Handle {
    enum Kind { kFile, kMapping } kind;
    explicit Handle(Kind k) : kind(k) {}
    virtual ~Handle() {}
};

struct FileHandle : Handle {
    FileHandle() : Handle(kFile) {}
    ~FileHandle() override {
        if (file != nullptr) std::fclose(file);
    }
    std::FILE* file = nullptr;
    int32_t reference = 0;
};

// A whole-file, read-only mapping: the file's bytes in memory.
struct MappingHandle : Handle {
    MappingHandle() : Handle(kMapping) {}
    std::vector<uint8_t> bytes;
};

// Every live handle, so a stale or foreign pointer handed to these calls is refused
// instead of dereferenced. Never destroyed: the game's global objects close their files
// from their destructors while the program exits (an archive a resource still holds).
std::unordered_set<Handle*>& Live() {
    static auto* handles = new std::unordered_set<Handle*>;
    return *handles;
}

InternalHandle* const kInvalid = reinterpret_cast<InternalHandle*>(static_cast<intptr_t>(-1));

template<class T> T* Find(InternalHandle* handle, Handle::Kind kind) {
    auto* candidate = reinterpret_cast<Handle*>(handle);
    if (Live().count(candidate) == 0 || candidate->kind != kind) return nullptr;
    return static_cast<T*>(candidate);
}

uint32_t SizeOf(std::FILE* file) {
    const long position = std::ftell(file);
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, position, SEEK_SET);
    return static_cast<uint32_t>(size);
}

int32_t gNextReference = 1;
}  // namespace

// access: 0x80000000 read, 0x40000000 write. creation: 1 a new file (fails if it exists),
// 2 a new file replacing any old one, 3 an existing file; any other value opens an
// existing file and creates nothing, as in the shim. Returns the handle, or -1.
InternalHandle* CreateFile(const char* name, uint32_t access, uint32_t, void*, uint32_t creation, uint32_t,
                           InternalHandle*) {
    const bool writing = (access & 0x40000000u) != 0;
    fs::path existing = tpw::FindHostPath(name);
    std::error_code ec;
    if (!existing.empty() && fs::is_directory(existing, ec)) return kInvalid;
    if (creation == 1 && !existing.empty()) return kInvalid;
    if (creation == 3 && existing.empty()) return kInvalid;
    std::FILE* file = nullptr;
    if (creation == 1 || creation == 2) {
        file = std::fopen(tpw::HostPathForWriting(name).string().c_str(), "w+b");
    } else if (existing.empty()) {
        return kInvalid;
    } else if (!writing) {
        file = std::fopen(existing.string().c_str(), "rb");
    } else {
        // The installed game is never written to: a game file opened for writing is first
        // copied to the user folder, which lookups prefer from then on.
        const fs::path target = tpw::HostPathForWriting(name);
        if (!fs::exists(target, ec)) fs::copy_file(existing, target, ec);
        file = std::fopen(target.string().c_str(), "r+b");
    }
    if (file == nullptr) return kInvalid;
    auto* handle = new FileHandle;
    handle->file = file;
    handle->reference = gNextReference++;
    Live().insert(handle);
    return reinterpret_cast<InternalHandle*>(handle);
}

// Closes a file or a mapping. NULL, -1 and unknown handles fail (the game closes its
// "no mapping" value as a matter of course).
int32_t CloseHandle(InternalHandle* handle) {
    auto* candidate = reinterpret_cast<Handle*>(handle);
    if (Live().erase(candidate) == 0) return 0;
    delete candidate;
    return 1;
}

// As the shim: the count read is reported, and the call succeeds only when all of it was
// there (the Mac's FSRead returns an end-of-file error for a short read).
int32_t ReadFile(InternalHandle* handle, void* buffer, uint32_t count, uint32_t* done, void*) {
    auto* file = Find<FileHandle>(handle, Handle::kFile);
    if (file == nullptr) return 0;
    const uint32_t read = static_cast<uint32_t>(std::fread(buffer, 1, count, file->file));
    tpw::CountStartupRead(read);   // the launcher's bar (handoff.h)
    if (done != nullptr) *done = read;
    return read == count ? 1 : 0;
}

int32_t WriteFile(InternalHandle* handle, const void* buffer, uint32_t count, uint32_t* done, void*) {
    auto* file = Find<FileHandle>(handle, Handle::kFile);
    if (file == nullptr) return 0;
    const uint32_t written = static_cast<uint32_t>(std::fwrite(buffer, 1, count, file->file));
    if (done != nullptr) *done = written;
    return written == count ? 1 : 0;
}

// method 0 from the start, 1 from the current position, 2 from the end. The new position,
// or 0xffffffff: unlike Win32, the Mac refuses to position past the end of the file (the
// mark then stays at the end) or before its start.
uint32_t SetFilePointer(InternalHandle* handle, int32_t distance, int32_t*, uint32_t method) {
    auto* file = Find<FileHandle>(handle, Handle::kFile);
    if (file == nullptr) return 0xffffffffu;
    const int64_t size = SizeOf(file->file);
    const int64_t base = method == 0 ? 0 : method == 1 ? std::ftell(file->file) : size;
    const int64_t target = base + distance;
    if (target < 0) return 0xffffffffu;
    if (target > size) {
        std::fseek(file->file, 0, SEEK_END);
        return 0xffffffffu;
    }
    std::fseek(file->file, static_cast<long>(target), SEEK_SET);
    return static_cast<uint32_t>(target);
}

uint32_t GetFileSize(InternalHandle* handle, uint32_t* sizeHigh) {
    auto* file = Find<FileHandle>(handle, Handle::kFile);
    if (file == nullptr) return 0xffffffffu;
    if (sizeHigh != nullptr) *sizeHigh = 0;
    return SizeOf(file->file);
}

int32_t FlushFileBuffers(InternalHandle* handle) {
    auto* file = Find<FileHandle>(handle, Handle::kFile);
    return file != nullptr && std::fflush(file->file) == 0 ? 1 : 0;
}

// The Mac file reference number behind a handle (the sound and movie code hands it to the
// Toolbox); here a number that identifies the open file, 0 for a bad handle.
int32_t GetMacFileReference(InternalHandle* handle) {
    auto* file = Find<FileHandle>(handle, Handle::kFile);
    return file != nullptr ? file->reference : 0;
}

// The bullfrog library maps whole files read-only (protection 2) to read archives without
// copying. The mapping here is the file's contents read once.
InternalHandle* CreateFileMapping(InternalHandle* handle, void*, uint32_t, uint32_t, uint32_t, const char*) {
    auto* file = Find<FileHandle>(handle, Handle::kFile);
    if (file == nullptr) return nullptr;
    auto* mapping = new MappingHandle;
    const long position = std::ftell(file->file);
    mapping->bytes.resize(SizeOf(file->file));
    std::fseek(file->file, 0, SEEK_SET);
    const size_t read = mapping->bytes.empty() ? 0 : std::fread(mapping->bytes.data(), 1, mapping->bytes.size(), file->file);
    std::fseek(file->file, position, SEEK_SET);
    tpw::CountStartupRead(read);
    if (read != mapping->bytes.size()) {
        delete mapping;
        return nullptr;
    }
    Live().insert(mapping);
    return reinterpret_cast<InternalHandle*>(mapping);
}

// A view from `offsetLow` on (`bytes` 0: to the end). NULL when the range is not inside
// the file.
void* MapViewOfFileEx(InternalHandle* handle, uint32_t, uint32_t, uint32_t offsetLow, uint32_t bytes, void*) {
    auto* mapping = Find<MappingHandle>(handle, Handle::kMapping);
    if (mapping == nullptr || offsetLow > mapping->bytes.size() || bytes > mapping->bytes.size() - offsetLow)
        return nullptr;
    return mapping->bytes.data() + offsetLow;
}

// Views are pointers into the mapping, which lives until its handle is closed.
int32_t UnmapViewOfFile(void*) { return 1; }

// Only the allocation granularity is read (the bullfrog library aligns partial views to
// it): Win32's 64 KB.
void GetSystemInfo(_SYSTEM_INFO* info) {
    std::memset(info, 0, sizeof *info);
    info->dwPageSize = 0x1000;
    info->dwAllocationGranularity = 0x10000;
}

int32_t DeleteFile(const char* name) {
    std::error_code ec;
    const fs::path user = tpw::HostPathForWriting(name);
    return fs::is_regular_file(user, ec) && fs::remove(user, ec) ? 1 : 0;
}

int32_t RemoveDirectory(const char* name) {
    std::error_code ec;
    const fs::path user = tpw::HostPathForWriting(name);
    return fs::is_directory(user, ec) && fs::remove(user, ec) ? 1 : 0;
}

// Renames within the user folder; a file that only the game folder has cannot be moved.
int32_t MoveFile(const char* from, const char* to) {
    std::error_code ec;
    const fs::path source = tpw::HostPathForWriting(from);
    if (!fs::exists(source, ec) || !tpw::FindHostPath(to).empty()) return 0;
    fs::rename(source, tpw::HostPathForWriting(to), ec);
    return ec ? 0 : 1;
}

int32_t CopyFile(const char* from, const char* to, int32_t failIfExists) {
    std::error_code ec;
    const fs::path source = tpw::FindHostPath(from);
    if (source.empty() || fs::is_directory(source, ec)) return 0;
    if (failIfExists != 0 && !tpw::FindHostPath(to).empty()) return 0;
    fs::copy_file(source, tpw::HostPathForWriting(to), fs::copy_options::overwrite_existing, ec);
    return ec ? 0 : 1;
}

// Nothing here is locked or hidden, so there is nothing to set; the call succeeds for
// anything that exists.
int32_t SetFileAttributes(const char* name, uint32_t) { return tpw::FindHostPath(name).empty() ? 0 : 1; }

}  // namespace NS_MacDoze

namespace tpw {

// A read at an absolute offset of the file with Mac reference number `reference`, as the File Manager's PBRead with
// fsFromStart: the file mark is left after the bytes read (nothing is restored), as the Mac's was. The seek and the read are
// one step against other users of the same file (the stdio lock is held across them). -1 when no open file has the reference.
int32_t ReadFileReference(int32_t reference, uint32_t offset, void* buffer, uint32_t count) {
    for (NS_MacDoze::Handle* candidate : NS_MacDoze::Live()) {
        if (candidate->kind != NS_MacDoze::Handle::kFile) continue;
        auto* file = static_cast<NS_MacDoze::FileHandle*>(candidate);
        // the player keeps the number as a 16-bit Mac reference, so a number past 0x7fff is compared as one
        if (file->reference != reference && static_cast<int16_t>(file->reference) != reference) continue;
        flockfile(file->file);
        int32_t read = -1;
        if (std::fseek(file->file, static_cast<long>(offset), SEEK_SET) == 0) {
            read = count == 0 ? 0 : static_cast<int32_t>(std::fread(buffer, 1, count, file->file));
        } else {
            read = 0;
        }
        funlockfile(file->file);
        if (read > 0) tpw::CountStartupRead(static_cast<uint64_t>(read));   // any thread (the sound's too)
        return read;
    }
    return -1;
}

std::string GamePathForWriting(const char* gamePath) { return HostPathForWriting(gamePath).string(); }

std::string GamePathForReading(const char* gamePath) {
    const fs::path host = FindHostPath(gamePath);
    return host.empty() ? std::string(gamePath) : host.string();
}

}  // namespace tpw
