// log_file.cpp: the launcher's log file (log_file.h).
#include "log_file.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <fcntl.h>
#ifdef _WIN32
// Windows: no <sys/utsname.h>, sysconf or flock; the system and the hardware come from the Win32 calls (OsLine,
// HardwareLine), the file from CreateFileW (its lock from LockFileEx) and the C runtime's _open_osfhandle, _write and _close
// (<io.h>). Only this file includes <windows.h>: none of the game's headers come in here.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace tpw {

namespace {

namespace fs = std::filesystem;

std::string gPath;
int gRun = 0;
char gRunTag[32] = "";
#ifdef _WIN32
// The file's second handle, for its lock, its reads and its cut (the lines go through gLogFileFd's, which may only append).
HANDLE gLockHandle = INVALID_HANDLE_VALUE;
#endif

// How much of the file a launch reads at most: a file longer than that (a run that wrote and wrote) loses its beginning.
constexpr size_t kReadMostBytes = 64000000;

#if defined(__APPLE__)
// A sysctl text value ("" when there is none).
std::string SysctlText(const char* name) {
    size_t size = 0;
    if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0) return "";
    std::string value(size, '\0');
    if (sysctlbyname(name, value.data(), &size, nullptr, 0) != 0) return "";
    value.resize(std::strlen(value.c_str()));
    return value;
}

// A sysctl number (`fallback` when there is none); the kernel answers 4 or 8 bytes.
int64_t SysctlNumber(const char* name, int64_t fallback) {
    int64_t wide = 0;
    size_t size = sizeof wide;
    if (sysctlbyname(name, &wide, &size, nullptr, 0) != 0) return fallback;
    if (size == sizeof(int32_t)) {
        int32_t narrow = 0;
        std::memcpy(&narrow, &wide, sizeof narrow);
        return narrow;
    }
    return size == sizeof wide ? wide : fallback;
}
#endif

std::string Or(const std::string& text, const char* fallback) { return text.empty() ? fallback : text; }

// "16 GB", "7.5 GB"
std::string Gigabytes(uint64_t bytes) {
    char text[32];
    const double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    if (gb == static_cast<double>(static_cast<uint64_t>(gb))) std::snprintf(text, sizeof text, "%.0f GB", gb);
    else std::snprintf(text, sizeof text, "%.1f GB", gb);
    return text;
}

// "-03:00", the local time's offset from UTC.
std::string ZoneOffset(const std::tm& local, std::time_t when) {
#ifdef _WIN32
    // Windows: struct tm has no tm_gmtoff or tm_zone. The offset is the local time read as UTC (_mkgmtime, which
    // changes its argument, so a copy) less the time itself.
    std::tm asUtc = local;
    const long offset = static_cast<long>(_mkgmtime(&asUtc) - when);
#else
    (void)when;
    const long offset = local.tm_gmtoff;
#endif
    const long minutes = (offset < 0 ? -offset : offset) / 60;
    char zone[64];
    std::snprintf(zone, sizeof zone, "%c%02ld:%02ld", offset < 0 ? '-' : '+', minutes / 60, minutes % 60);
    return zone;
}

// "2026-10-22T19:46:29-03:00 (ADT)"
std::string IsoLocalTime(std::time_t when) {
    std::tm local{};
    localtime_r(&when, &local);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%S", &local);
    std::string text = std::string(stamp) + ZoneOffset(local, when);
#ifndef _WIN32
    // The zone's name is left out on Windows: it gives its long name in the system's language and code page ("Atlantic
    // Daylight Time"), not an abbreviation.
    if (local.tm_zone != nullptr && local.tm_zone[0] != '\0') text += std::string(" (") + local.tm_zone + ")";
#endif
    return text;
}

// "2026-10-28 14:03:22 -03:00", a run header's date.
std::string HeaderTime(std::time_t when) {
    std::tm local{};
    localtime_r(&when, &local);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &local);
    return std::string(stamp) + " " + ZoneOffset(local, when);
}

// A file's last change as a header's date ("" when it cannot be read).
std::string WrittenTime(const fs::path& file) {
    std::error_code ec;
    const fs::file_time_type time = fs::last_write_time(file, ec);
    if (ec) return std::string();
    const auto system = std::chrono::system_clock::now() +
                        std::chrono::duration_cast<std::chrono::system_clock::duration>(time - fs::file_time_type::clock::now());
    return HeaderTime(std::chrono::system_clock::to_time_t(system));
}

// The command line, each argument quoted when it has a space or is empty.
std::string CommandLine(int argc, const char* const* argv) {
    std::string line;
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i] != nullptr ? argv[i] : "";
        if (i > 0) line += ' ';
        if (arg.empty() || arg.find_first_of(" \t\"") != std::string::npos) {
            line += '"';
            for (char c : arg) {
                if (c == '"' || c == '\\') line += '\\';
                line += c;
            }
            line += '"';
        } else {
            line += arg;
        }
    }
    return line;
}

std::string OsLine() {
#if defined(__APPLE__)
    return "os: macOS " + Or(SysctlText("kern.osproductversion"), "(version unknown)") + " (" +
           Or(SysctlText("kern.osversion"), "build unknown") + ")\n";
#elif defined(_WIN32)
    // Windows: no uname. RtlGetVersion (ntdll.dll, in every process, so it is looked up and not linked) answers the real
    // version; GetVersionExW answers 6.2 to a program without a compatibility manifest. Windows 11 is 10.0, build 22000
    // and later.
    using RtlGetVersionCall = LONG(WINAPI*)(OSVERSIONINFOW*);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto rtlGetVersion =
        ntdll != nullptr ? reinterpret_cast<RtlGetVersionCall>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")))
                         : nullptr;
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof version;
    if (rtlGetVersion == nullptr || rtlGetVersion(&version) != 0) return "os: Windows (version unknown)\n";
    return "os: Windows " + std::to_string(version.dwMajorVersion) + "." + std::to_string(version.dwMinorVersion) +
           " (build " + std::to_string(version.dwBuildNumber) + ")\n";
#else
    utsname name{};
    std::string kernel = uname(&name) == 0 ? std::string(name.sysname) + " " + name.release : "unknown";
    return std::string("os: ") + SDL_GetPlatform() + " (" + kernel + ")\n";
#endif
}

std::string HardwareLine() {
#ifdef _WIN32
    // Windows: no uname or sysconf. The machine is the processor's own (GetNativeSystemInfo), the cores the logical
    // processors of the launcher's processor group (GetSystemInfo: at most 64), the memory the physical memory
    // (GlobalMemoryStatusEx). The model and the processor's name are not looked for.
    SYSTEM_INFO native{};
    GetNativeSystemInfo(&native);
    const std::string model = "unknown model";
    const char* machine = native.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64   ? "x64"
                          : native.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? "arm64"
                          : native.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL ? "x86"
                                                                                          : "";
    const std::string arch = Or(machine, "unknown arch");
    const std::string cpu = "unknown cpu";
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const int64_t cores = static_cast<int64_t>(info.dwNumberOfProcessors);
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof status;
    const int64_t memory = GlobalMemoryStatusEx(&status) ? static_cast<int64_t>(status.ullTotalPhys) : 0;
#else
    utsname name{};
    const std::string unameMachine = uname(&name) == 0 ? name.machine : "";
#if defined(__APPLE__)
    const std::string model = Or(SysctlText("hw.model"), "unknown model");
    std::string arch = SysctlText("hw.machine");
    if (arch.empty()) arch = Or(unameMachine, "unknown arch");
    // An Intel build on Apple silicon: the kernel then answers x86_64 for the machine.
    if (SysctlNumber("sysctl.proc_translated", 0) == 1) arch += " (under Rosetta)";
    const std::string cpu = Or(SysctlText("machdep.cpu.brand_string"), "unknown cpu");
    const int64_t cores = SysctlNumber("hw.ncpu", 0);
    const int64_t memory = SysctlNumber("hw.memsize", 0);
#else
    const std::string model = "unknown model";
    const std::string arch = Or(unameMachine, "unknown arch");
    const std::string cpu = "unknown cpu";
    const int64_t cores = sysconf(_SC_NPROCESSORS_ONLN);
    const int64_t pages = sysconf(_SC_PHYS_PAGES), pageSize = sysconf(_SC_PAGESIZE);
    const int64_t memory = pages > 0 && pageSize > 0 ? pages * pageSize : 0;
#endif
#endif
    return "hardware: " + model + ", " + arch + ", " + cpu + ", " + (cores > 0 ? std::to_string(cores) : "?") +
           " cores, " + (memory > 0 ? Gigabytes(static_cast<uint64_t>(memory)) : "? GB") + " RAM\n";
}

// The value after `option` on the command line ("" when it is not there).
std::string ArgValue(int argc, const char* const* argv, const char* option) {
    for (int i = 1; i + 1 < argc; ++i)
        if (argv[i] != nullptr && argv[i + 1] != nullptr && std::strcmp(argv[i], option) == 0) return argv[i + 1];
    return std::string();
}

// "[... 1234 bytes of this run's earlier lines were cut here: ...]", the line a cut run keeps in their place.
std::string CutLine(size_t bytes, const char* what, size_t keep) {
    std::string limit = keep >= 1000000 && keep % 1000000 == 0 ? std::to_string(keep / 1000000) + " MB"
                                                                : std::to_string(keep) + " bytes";
    return "[... " + std::to_string(bytes) + " bytes of " + what + "'s earlier lines were cut here: the log file keeps its newest " +
           limit + " ...]\n";
}

// Where the run header line at or after `from` starts (a line's start), or npos.
size_t HeaderAtOrAfter(const std::string& text, size_t from) {
    size_t at = from;
    if (at > 0 && text[at - 1] != '\n') {   // inside a line: the next line's start
        const size_t newline = text.find('\n', at);
        if (newline == std::string::npos) return std::string::npos;
        at = newline + 1;
    }
    while (at < text.size()) {
        const size_t end = text.find('\n', at);
        if (RunNumberOf(text.substr(at, end == std::string::npos ? std::string::npos : end - at)) > 0) return at;
        const size_t next = text.find(std::string("\n") + kRunHeaderStart, at);
        if (next == std::string::npos) return std::string::npos;
        at = next + 1;
    }
    return std::string::npos;
}

// Where the last run header line before `before` starts, or npos.
size_t HeaderBefore(const std::string& text, size_t before) {
    size_t found = std::string::npos;
    for (size_t at = HeaderAtOrAfter(text, 0); at != std::string::npos && at < before; at = HeaderAtOrAfter(text, at + 1))
        found = at;
    return found;
}

// The line of `text` at `at` with its newline.
std::string LineAt(const std::string& text, size_t at) {
    const size_t end = text.find('\n', at);
    return end == std::string::npos ? text.substr(at) + "\n" : text.substr(at, end + 1 - at);
}

// The file's lock: taken (exclusive), held by another process (busy), or not to be had on this system or file (none).
enum class Lock { kExclusive, kBusy, kNone };

#ifdef _WIN32
// The lock is on one byte far past any end the file reaches: a Windows lock keeps every other handle from writing the
// bytes it covers (and from reading them, when it is exclusive), and the lines must go on being written.
OVERLAPPED LockByte() {
    OVERLAPPED at{};
    at.OffsetHigh = 0x40000000;   // 2^62
    return at;
}

Lock TryExclusive(HANDLE file) {
    OVERLAPPED at = LockByte();
    if (LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &at)) return Lock::kExclusive;
    return GetLastError() == ERROR_LOCK_VIOLATION ? Lock::kBusy : Lock::kNone;
}

// The shared lock, waited for (a launch that holds the exclusive one is cutting the file). From the exclusive one: that
// is let go first (Windows does not change a lock's kind).
void HoldShared(HANDLE file, bool fromExclusive) {
    if (fromExclusive) {
        OVERLAPPED at = LockByte();
        UnlockFileEx(file, 0, 1, 0, &at);
    }
    OVERLAPPED at = LockByte();
    (void)LockFileEx(file, 0, 0, 1, 0, &at);
}

// The file's last `most` bytes at most. False when it cannot be read.
bool ReadEnd(HANDLE file, size_t most, std::string& text, uint64_t& from) {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) return false;
    const uint64_t length = static_cast<uint64_t>(size.QuadPart);
    from = length > most ? length - most : 0;
    LARGE_INTEGER start{};
    start.QuadPart = static_cast<LONGLONG>(from);
    if (!SetFilePointerEx(file, start, nullptr, FILE_BEGIN)) return false;
    text.assign(static_cast<size_t>(length - from), '\0');
    size_t got = 0;
    while (got < text.size()) {
        DWORD read = 0;
        const DWORD ask = static_cast<DWORD>(std::min<size_t>(text.size() - got, 1 << 20));
        if (!ReadFile(file, &text[got], ask, &read, nullptr)) return false;
        if (read == 0) break;
        got += read;
    }
    text.resize(got);
    return true;
}

// The file becomes `text`: written over its beginning, then cut at its end. False when that failed.
bool Rewrite(HANDLE file, const fs::path&, const std::string& text) {
    LARGE_INTEGER start{};
    if (!SetFilePointerEx(file, start, nullptr, FILE_BEGIN)) return false;
    size_t done = 0;
    while (done < text.size()) {
        DWORD wrote = 0;
        const DWORD ask = static_cast<DWORD>(std::min<size_t>(text.size() - done, 1 << 20));
        if (!WriteFile(file, text.data() + done, ask, &wrote, nullptr) || wrote == 0) return false;
        done += wrote;
    }
    return SetEndOfFile(file) != 0;
}

std::string SystemError(DWORD code) { return std::error_code(static_cast<int>(code), std::system_category()).message(); }
#else
Lock TryExclusive(int fd) {
    for (;;) {
        if (flock(fd, LOCK_EX | LOCK_NB) == 0) return Lock::kExclusive;
        if (errno == EINTR) continue;
        return errno == EWOULDBLOCK ? Lock::kBusy : Lock::kNone;
    }
}

// The shared lock, waited for (a launch that holds the exclusive one is cutting the file). flock changes the exclusive
// lock into it.
void HoldShared(int fd, bool) {
    while (flock(fd, LOCK_SH) != 0 && errno == EINTR) {
    }
}

bool ReadEnd(int fd, size_t most, std::string& text, uint64_t& from) {
    struct stat info {};
    if (fstat(fd, &info) != 0) return false;
    const uint64_t length = static_cast<uint64_t>(info.st_size);
    from = length > most ? length - most : 0;
    text.assign(static_cast<size_t>(length - from), '\0');
    size_t got = 0;
    while (got < text.size()) {
        const ssize_t read = pread(fd, &text[got], text.size() - got, static_cast<off_t>(from + got));
        if (read < 0 && errno == EINTR) continue;
        if (read < 0) return false;
        if (read == 0) break;
        got += static_cast<size_t>(read);
    }
    text.resize(got);
    return true;
}

// The file becomes `text`: written over its beginning through a descriptor of its own (the lines' appends wherever it
// is told to write), then cut at its end. False when that failed.
bool Rewrite(int, const fs::path& path, const std::string& text) {
    const int fd = open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) return false;
    size_t done = 0;
    bool ok = true;
    while (ok && done < text.size()) {
        const ssize_t wrote = pwrite(fd, text.data() + done, text.size() - done, static_cast<off_t>(done));
        if (wrote < 0 && errno == EINTR) continue;
        if (wrote <= 0) ok = false;
        else done += static_cast<size_t>(wrote);
    }
    ok = ok && ftruncate(fd, static_cast<off_t>(text.size())) == 0;
    return close(fd) == 0 && ok;
}
#endif

}  // namespace

std::string RunHeaderLine(int run, std::time_t when, const std::string& firstLine, const std::string& system,
                          const std::string& edition, const std::string& program, long process) {
    std::string line = kRunHeaderStart + std::to_string(run) + " | " + HeaderTime(when) + " | " + firstLine;
    for (const std::string* part : {&system, &edition, &program})
        if (!part->empty()) line += " | " + *part;
    if (process > 0) line += " | process " + std::to_string(process);
    return line + " ====";
}

int RunNumberOf(const std::string& line) {
    const size_t start = std::strlen(kRunHeaderStart);
    if (line.compare(0, start, kRunHeaderStart) != 0) return 0;
    int number = 0;
    size_t at = start;
    for (; at < line.size() && at < start + 9 && line[at] >= '0' && line[at] <= '9'; ++at) number = number * 10 + (line[at] - '0');
    if (at == start || (at < line.size() && line[at] != ' ')) return 0;
    return number;
}

int HighestRunNumber(const std::string& text) {
    int highest = 0;
    for (size_t at = HeaderAtOrAfter(text, 0); at != std::string::npos; at = HeaderAtOrAfter(text, at + 1)) {
        const size_t end = text.find('\n', at);
        highest = std::max(highest, RunNumberOf(text.substr(at, end == std::string::npos ? std::string::npos : end - at)));
    }
    return highest;
}

std::string TrimLog(const std::string& text, size_t keep, size_t* cut) {
    if (cut != nullptr) *cut = 0;
    if (text.size() <= keep) return text;
    const size_t start = text.size() - keep;
    // At a run header: the runs before it go whole.
    const size_t header = HeaderAtOrAfter(text, start);
    if (header != std::string::npos) {
        if (cut != nullptr) *cut = header;
        return text.substr(header);
    }
    // The newest run alone is longer: it keeps its header line, a line saying what was cut, and its end from a line's
    // start. The room is reckoned with the longest such line, so the text never passes `keep`.
    const size_t own = HeaderBefore(text, start);
    const std::string headerLine = own != std::string::npos ? LineAt(text, own) : std::string();
    const size_t prefixMost = headerLine.size() + CutLine(static_cast<size_t>(-1), "this run", keep).size();
    const bool room = keep > prefixMost;   // no room for the two lines (the tests' tiny files): a line's start, and nothing more
    size_t from = text.size() - (room ? keep - prefixMost : keep);
    if (from > 0 && text[from - 1] != '\n') {
        const size_t newline = text.find('\n', from);
        from = newline == std::string::npos ? text.size() : newline + 1;
    }
    if (!room) {
        if (cut != nullptr) *cut = from;
        return text.substr(from);
    }
    const size_t runStart = own != std::string::npos ? own + headerLine.size() : 0;
    if (cut != nullptr) *cut = from - headerLine.size();
    return headerLine + CutLine(from - runStart, "this run", keep) + text.substr(from);
}

bool IsOldRunLogName(const std::string& name) {
    // launcher-YYYYMMDD-HHMMSS.log: 28 characters; with -N (1 or 2 digits) 30 or 31.
    const auto digits = [&](size_t at, size_t count) {
        for (size_t i = at; i < at + count; ++i)
            if (i >= name.size() || name[i] < '0' || name[i] > '9') return false;
        return true;
    };
    if (name.size() < 28 || name.compare(0, 9, "launcher-") != 0 || !digits(9, 8) || name[17] != '-' || !digits(18, 6))
        return false;
    if (name.size() == 28) return name.compare(24, 4, ".log") == 0;
    if (name[24] != '-' || name.compare(name.size() - 4, 4, ".log") != 0) return false;
    const size_t count = name.size() - 29;   // the N
    return (count == 1 || count == 2) && digits(25, count) && name[25] != '0';
}

std::vector<fs::path> OldLogFiles(const fs::path& logsFolder) {
    struct Found {
        fs::path path;
        fs::file_time_type time;
    };
    std::vector<Found> found;
    const auto add = [&](const fs::path& path) {
        std::error_code typeError, timeError;
        if (!fs::is_regular_file(fs::symlink_status(path, typeError))) return;   // a file, not a link to one
        fs::file_time_type time = fs::last_write_time(path, timeError);
        if (timeError) time = fs::file_time_type::min();
        found.push_back({path, time});
    };
    std::error_code ec;
    for (fs::directory_iterator it(logsFolder, ec), end; !ec && it != end; it.increment(ec))
        if (IsOldRunLogName(it->path().filename().string())) add(it->path());
    // Windows' launcher.log and game.log, beside launcher.ini in the folder the logs folder is in.
    if (logsFolder.has_parent_path() && logsFolder.has_filename())
        for (const char* name : {"launcher.log", "game.log"}) add(logsFolder.parent_path() / name);
    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) {
        if (a.time != b.time) return a.time < b.time;
        return a.path.filename().string() < b.path.filename().string();
    });
    std::vector<fs::path> paths;
    for (const Found& file : found) paths.push_back(file.path);
    return paths;
}

std::string FoldedOldLog(const fs::path& file, int run, size_t keep, const ScrubIdentity* identity) {
    std::error_code ec;
    const uintmax_t size = fs::file_size(file, ec);
    std::string text;
    size_t cut = 0;
    if (!ec) {
        std::ifstream in(file, std::ios::binary);
        const uintmax_t from = size > keep ? size - keep : 0;
        in.seekg(static_cast<std::streamoff>(from));
        text.assign(static_cast<size_t>(size - from), '\0');
        in.read(&text[0], static_cast<std::streamsize>(text.size()));
        text.resize(static_cast<size_t>(std::max<std::streamsize>(in.gcount(), 0)));
        if (from > 0) {   // from a line's start
            const size_t newline = text.find('\n');
            const size_t skip = newline == std::string::npos ? text.size() : newline + 1;
            text.erase(0, skip);
            cut = static_cast<size_t>(from) + skip;
        }
    }
    text = Scrub(identity, text);
    const std::string name = file.filename().string();
    std::string when, what;
    if (name == "launcher.log" || name == "game.log") {
        what = name == "game.log" ? "what the game's own process printed on Windows" : "what the launcher printed on Windows";
    } else if (cut == 0) {
        // The old file's header: its first line (the launcher's name, version and build) and its date: line.
        const size_t newline = text.find('\n');
        const std::string first = text.substr(0, newline);
        if (!first.empty() && first.size() < 200) what = first;
        const size_t date = text.find("\ndate: ");
        if (date != std::string::npos) when = text.substr(date + 7, text.find('\n', date + 1) - (date + 7));
    }
    if (when.empty()) {
        const std::string written = WrittenTime(file);
        if (!written.empty()) when = "last written " + written;
    }
    std::string header = kRunHeaderStart + std::to_string(run);
    if (!when.empty()) header += " | " + when;
    if (!what.empty()) header += " | " + what;
    header += " | from the old file " + name + " ====\n";
    if (cut > 0) header += CutLine(cut, "this file", keep);
    if (!text.empty() && text.back() != '\n') text += '\n';
    return header + text;
}

std::string LogHeaderLines(int argc, const char* const* argv, const std::string& path) {
    return "date: " + IsoLocalTime(std::time(nullptr)) + "\n" + OsLine() + HardwareLine() + "args: " +
           CommandLine(argc, argv) + "\n" + "log: " + path + "\n";
}

bool OpenLogFile(const fs::path& folder, const std::string& firstLine, int argc, const char* const* argv, std::string& err) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (!fs::is_directory(folder, ec)) {
        err = "cannot make the folder \"" + folder.string() + "\"" + (ec ? ": " + ec.message() : "");
        return false;
    }
    const fs::path path = folder / kLogFileName;
#ifdef _WIN32
    // Two handles, each shared with every other process's (and a delete), neither inherited by a process this one
    // starts (no security attributes). The lines' may only append (FILE_APPEND_DATA without FILE_WRITE_DATA), so the
    // system writes each write at the file's end, as O_APPEND does, whichever process wrote last; the C runtime's
    // descriptor on it has no _O_APPEND, which would move to the end and then write, two steps another process could
    // come between. _O_BINARY keeps every line ending in \n alone, as on the Mac (text mode would write \r\n). The other
    // handle reads, cuts and holds the lock.
    const std::wstring wide = path.wstring();
    const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    const HANDLE lock = CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE, share, nullptr, OPEN_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock == INVALID_HANDLE_VALUE) {
        err = "cannot open \"" + path.string() + "\": " + SystemError(GetLastError());
        return false;
    }
    const HANDLE append = CreateFileW(wide.c_str(), FILE_APPEND_DATA | SYNCHRONIZE, share, nullptr, OPEN_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    const DWORD appendError = GetLastError();
    const int fd = append != INVALID_HANDLE_VALUE ? _open_osfhandle(reinterpret_cast<intptr_t>(append), _O_WRONLY | _O_BINARY) : -1;
    if (fd < 0) {
        err = "cannot open \"" + path.string() + "\" for appending: " +
              (append != INVALID_HANDLE_VALUE ? std::string("no C runtime descriptor for it") : SystemError(appendError));
        if (append != INVALID_HANDLE_VALUE) CloseHandle(append);
        CloseHandle(lock);
        return false;
    }
    const long process = static_cast<long>(GetCurrentProcessId());
#else
    // One descriptor for all: O_APPEND puts each write at the file's end, whichever process wrote last; the lock is
    // flock's, on it, and goes with it.
    const int fd = open(path.c_str(), O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0) {
        err = "cannot open \"" + path.string() + "\": " + std::strerror(errno);
        return false;
    }
    const int lock = fd;
    const long process = static_cast<long>(getpid());
#endif
    CloseLogFile();
    EnsureScrubIdentity();   // before the first byte: the header holds the folders and the command line
    // The game's own process (--from-launcher, launcher/game_app.h) writes beside the launcher that started it and
    // never cuts; a launcher cuts and folds only under the exclusive lock, which it waits for never.
    const bool game = !ArgValue(argc, argv, "--from-launcher").empty();
    const Lock got = game ? Lock::kBusy : TryExclusive(lock);
    if (got == Lock::kBusy) HoldShared(lock, false);
    std::string text;
    uint64_t from = 0;
    const bool read = ReadEnd(lock, kReadMostBytes, text, from);
    size_t dropped = 0;
    if (read && from > 0) {   // a file past kReadMostBytes: from a line's start
        const size_t newline = text.find('\n');
        dropped = newline == std::string::npos ? text.size() : newline + 1;
        text.erase(0, dropped);
    }
    int highest = HighestRunNumber(text);
    std::string notes;
    if (!read) notes += "log: the file could not be read, so it was not cut this launch\n";
    if (!game && read) {
        const std::vector<fs::path> old = OldLogFiles(folder);
        if (got == Lock::kBusy) {
            if (text.size() > kLogKeepBytes || !old.empty() || dropped > 0)
                notes += "log: not cut this launch: another launcher or the game's own process has the file open (the next "
                         "launch cuts it)\n";
        } else {
            std::string all = text;
            if (!all.empty() && all.back() != '\n') all += '\n';
            for (const fs::path& file : old) all += FoldedOldLog(file, ++highest, kLogKeepBytes, gScrubIdentity.load());
            size_t cut = 0;
            std::string kept = TrimLog(all, kLogKeepBytes, &cut);
            if (cut > 0 || dropped > 0 || !old.empty()) {
                if (Rewrite(lock, path, kept)) {
                    // An old file that cannot be removed (an older launcher still has it open) is said so: the next launch
                    // puts it in again.
                    std::string removed, stayed;
                    for (const fs::path& file : old) {
                        std::string& names = fs::remove(file, ec) ? removed : stayed;
                        names += (names.empty() ? "" : ", ") + file.filename().string();
                    }
                    if (!removed.empty())
                        notes += "log: the old log files were put in this one, oldest first, and removed: " + removed + "\n";
                    if (!stayed.empty())
                        notes += "log: these old log files were put in this one but could not be removed (the next launch puts "
                                 "them in again): " + stayed + "\n";
                    if (cut + dropped > 0)
                        notes += "log: cut " + std::to_string(cut + dropped) + " bytes of the oldest lines: the file keeps its newest " +
                                 std::to_string(kLogKeepBytes / 1000000) + " MB\n";
                    if (got == Lock::kNone) notes += "log: the file could not be locked here, so it was cut without its lock\n";
                    text = std::move(kept);
                } else {
                    highest = HighestRunNumber(text);   // the old files stay, for the next launch
                    notes += "log: the file could not be cut this launch\n";
                }
            }
        }
    }
    if (got == Lock::kExclusive) HoldShared(lock, true);
    gRun = highest + 1;
    std::snprintf(gRunTag, sizeof gRunTag, " [run %d]", gRun);
    gPath = path.string();
    const std::string play = ArgValue(argc, argv, "--play");
    const std::string edition = play == "us" ? "Sim Theme Park" : play == "eur" ? "Theme Park World" : "";
    std::string system = OsLine();
    system = system.substr(4, system.size() - 5);   // "os: " and the newline off
    std::string header = text.empty() || text.back() == '\n' ? "" : "\n";   // a line cut short (a full disk) is ended
    header += RunHeaderLine(gRun, std::time(nullptr), firstLine, system, edition,
                            game ? "the game's own process (from the launcher's Play)" : "the launcher", process) +
              "\n" + LogHeaderLines(argc, argv, gPath) + notes;
    WriteScrubbed(fd, header.data(), header.size());
    gLogFileFd.store(fd);
#ifdef _WIN32
    gLockHandle = lock;
#else
    (void)lock;
#endif
    return true;
}

const std::string& LogFilePath() { return gPath; }

int LogRunNumber() { return gRun; }

const char* LogRunTag() { return gRunTag; }

void CloseLogFile() {
    const int fd = gLogFileFd.exchange(-1);
#ifdef _WIN32
    if (fd >= 0) _close(fd);   // Windows: the C runtime's name; it closes the handle under it
    if (gLockHandle != INVALID_HANDLE_VALUE) CloseHandle(gLockHandle);   // and the lock with it
    gLockHandle = INVALID_HANDLE_VALUE;
#else
    if (fd >= 0) close(fd);   // and flock's lock with it
#endif
    gPath.clear();
    gRun = 0;
    gRunTag[0] = '\0';
}

void LogDisplayOnce() {
    static std::atomic<bool> said{false};
    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0 || said.exchange(true)) return;
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    if (displays == nullptr || count == 0) {
        LogFileF("display: none found (%s)", SDL_GetError());
        SDL_free(displays);
        return;
    }
    // Every screen: "Built-in Retina Display" 1512x982 points, scale 2.00, 120 Hz (primary); ...
    std::string line = "display: " + std::to_string(count) + (count == 1 ? " screen: " : " screens: ");
    const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetDisplayName(displays[i]);
        const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(displays[i]);
        char text[256];
        if (mode != nullptr)
            std::snprintf(text, sizeof text, "%s\"%s\" %dx%d points, scale %.2f, %.0f Hz%s", i > 0 ? "; " : "",
                          name != nullptr ? name : "?", mode->w, mode->h, mode->pixel_density, mode->refresh_rate,
                          displays[i] == primary ? " (primary)" : "");
        else
            std::snprintf(text, sizeof text, "%s\"%s\" (no mode: %s)", i > 0 ? "; " : "", name != nullptr ? name : "?",
                          SDL_GetError());
        line += text;
    }
    SDL_free(displays);
    LogToFile("", line.c_str());
}

}  // namespace tpw
