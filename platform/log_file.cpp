// log_file.cpp: the launcher's log file (log_file.h).
#include "log_file.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <system_error>
#include <vector>

#include <fcntl.h>
#ifdef _WIN32
// Windows: no <sys/utsname.h> or sysconf; the system and the hardware come from the Win32 calls (OsLine, HardwareLine),
// and the file from the C runtime's _wopen, _write and _close (<io.h>). Only this file includes <windows.h>: none of the
// game's headers come in here.
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
#include <sys/utsname.h>
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace tpw {

namespace {

std::string gPath;

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

// "2026-10-22T19:46:29-03:00 (ADT)"
std::string IsoLocalTime(std::time_t when) {
    std::tm local{};
    localtime_r(&when, &local);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%S", &local);
#ifdef _WIN32
    // Windows: struct tm has no tm_gmtoff or tm_zone. The offset is the local time read as UTC (_mkgmtime, which
    // changes its argument, so a copy) less the time itself. The zone's name is left out: Windows gives its long name in
    // the system's language and code page ("Atlantic Daylight Time"), not an abbreviation.
    std::tm asUtc = local;
    const long offset = static_cast<long>(_mkgmtime(&asUtc) - when);
    const long minutes = (offset < 0 ? -offset : offset) / 60;
    char zone[64];
    std::snprintf(zone, sizeof zone, "%c%02ld:%02ld", offset < 0 ? '-' : '+', minutes / 60, minutes % 60);
    return std::string(stamp) + zone;
#else
    const long offset = local.tm_gmtoff;
    const long minutes = (offset < 0 ? -offset : offset) / 60;
    char zone[64];
    std::snprintf(zone, sizeof zone, "%c%02ld:%02ld", offset < 0 ? '-' : '+', minutes / 60, minutes % 60);
    std::string text = std::string(stamp) + zone;
    if (local.tm_zone != nullptr && local.tm_zone[0] != '\0') text += std::string(" (") + local.tm_zone + ")";
    return text;
#endif
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

// launcher-YYYYMMDD-HHMMSS.log, or with a -N before .log: the part sorted on.
bool IsLogName(const std::string& name) {
    return name.size() > 13 && name.compare(0, 9, "launcher-") == 0 && name.compare(name.size() - 4, 4, ".log") == 0;
}

void WriteAll(int fd, const char* data, size_t length) {
    while (length > 0) {
#ifdef _WIN32
        // Windows: _write takes and answers an int count (the header is a few hundred bytes) and has no EINTR.
        const int wrote = _write(fd, data, static_cast<unsigned>(length));
#else
        const ssize_t wrote = write(fd, data, length);
        if (wrote < 0 && errno == EINTR) continue;
#endif
        if (wrote <= 0) return;
        data += wrote;
        length -= static_cast<size_t>(wrote);
    }
}

}  // namespace

std::string LogFileName(std::time_t when) {
    std::tm local{};
    localtime_r(&when, &local);
    char name[48];
    std::strftime(name, sizeof name, "launcher-%Y%m%d-%H%M%S.log", &local);
    return name;
}

std::string LogHeaderLines(int argc, const char* const* argv, const std::string& path) {
    return "date: " + IsoLocalTime(std::time(nullptr)) + "\n" + OsLine() + HardwareLine() + "args: " +
           CommandLine(argc, argv) + "\n" + "log: " + path + "\n";
}

size_t RemoveOldLogs(const std::filesystem::path& folder, size_t keep) {
    std::error_code ec;
    std::vector<std::string> stems;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        std::error_code typeError;
        if (IsLogName(name) && it->is_regular_file(typeError)) stems.push_back(name.substr(0, name.size() - 4));
    }
    // Newest first. The stem without ".log", so "...-194629-2" (the second run that second) sorts after "...-194629".
    std::sort(stems.begin(), stems.end(), [](const std::string& a, const std::string& b) { return a > b; });
    size_t removed = 0;
    for (size_t i = keep; i < stems.size(); ++i) {
        std::error_code removeError;
        if (std::filesystem::remove(folder / (stems[i] + ".log"), removeError)) ++removed;
    }
    return removed;
}

bool OpenLogFile(const std::filesystem::path& folder, const std::string& firstLine, int argc, const char* const* argv,
                 std::string& err) {
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (!std::filesystem::is_directory(folder, ec)) {
        err = "cannot make the folder \"" + folder.string() + "\"" + (ec ? ": " + ec.message() : "");
        return false;
    }
    const std::string name = LogFileName(std::time(nullptr));
    const std::string stem = name.substr(0, name.size() - 4);
    int fd = -1;
    std::filesystem::path path;
    for (int n = 1; n < 100 && fd < 0; ++n) {
        path = folder / (n == 1 ? name : stem + "-" + std::to_string(n) + ".log");
#ifdef _WIN32
        // Windows: the path's c_str() is UTF-16, so _wopen. _O_NOINHERIT is O_CLOEXEC's counterpart. _O_BINARY keeps
        // every line ending in \n alone, as on the Mac: text mode would write \r\n, and the file would differ by
        // system (Notepad and the other editors read \n). Read and write for the user is all a mode means there.
        fd = _wopen(path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_APPEND | _O_NOINHERIT | _O_BINARY,
                    _S_IREAD | _S_IWRITE);
#else
        fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_APPEND | O_CLOEXEC, 0644);
#endif
        if (fd < 0 && errno != EEXIST) break;
    }
    if (fd < 0) {
        err = "cannot make \"" + path.string() + "\": " + std::strerror(errno);
        return false;
    }
    CloseLogFile();
    gPath = path.string();
    const std::string header = firstLine + "\n" + LogHeaderLines(argc, argv, gPath);
    WriteAll(fd, header.data(), header.size());
    gLogFileFd.store(fd);
    RemoveOldLogs(folder, kLogFilesKept);
    return true;
}

const std::string& LogFilePath() { return gPath; }

void CloseLogFile() {
    const int fd = gLogFileFd.exchange(-1);
#ifdef _WIN32
    if (fd >= 0) _close(fd);   // Windows: the C runtime's name
#else
    if (fd >= 0) close(fd);
#endif
    gPath.clear();
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
