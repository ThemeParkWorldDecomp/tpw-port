// macdoze_system.cpp: the Win32 system calls of macdoze that the game makes outside the
// window and the file system: threads, sleeping, critical sections, the string helpers
// (_itoa, _strnicmp, _wcsnicmp), the clock's text, the disk and the temp folder.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include <sys/statvfs.h>

#include "platform.h"
#include "platform/macdoze.h"
#include "platform/macdoze_system.h"

namespace fs = std::filesystem;

namespace NS_MacDoze {

// ---- Threads and sleeping -------------------------------------------------------------

// macdoze 0x1000c478 made a cooperative UThread and answered its handle (0 when it could not),
// the thread object going to *id. The host runs the function on a thread of its own; the
// handle and the id are the same counter value, never 0 (the game compares the answer to 0).
// The handle is never waited on or closed by the game, so the thread is detached.
uint32_t _beginthreadex(void*, uint32_t, void* (*start)(void*), void* arg, uint32_t, uint32_t* id) {
    static std::atomic<uint32_t> counter{0};
    if (start == nullptr) return 0;
    try {
        std::thread([start, arg]() { start(arg); }).detach();
    } catch (const std::system_error&) {
        return 0;
    }
    const uint32_t handle = ++counter;
    if (id != nullptr) *id = handle;
    return handle;
}

// macdoze 0x1000c5b4 ran a UTimer and yielded the UThread until it ticked: 0 only yields,
// 0xffffffff (INFINITE) never returns.
void Sleep(uint32_t milliseconds) {
    if (milliseconds == 0) {
        std::this_thread::yield();
    } else if (milliseconds == 0xffffffffu) {
        for (;;) std::this_thread::sleep_for(std::chrono::hours(24));
    } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
}

// ---- Critical sections ----------------------------------------------------------------

// macdoze 0x1000ba90 and 0x1000bad4 are 4 byte bodies that do nothing. The mutex behind the
// word is made on the first EnterCriticalSection (macdoze_misc.cpp), so Initialize only has
// to leave the word empty (the game's objects come from operator new, which does not clear
// them) and Delete frees the mutex.
void InitializeCriticalSection(void** section) {
    if (section != nullptr) *section = nullptr;
}

void DeleteCriticalSection(void** section) {
    if (section == nullptr) return;
    delete static_cast<std::recursive_mutex*>(*section);
    *section = nullptr;
}

// macdoze 0x1000be2c set the +0x1c flag of the event object, yielded the UThread and answered
// 0. The events (CreateEvent, WaitForSingleObject, src/game/TPEmbed.cpp) have no host object
// behind them, so there is nothing to set.
int32_t SetEvent(InternalHandle*) { return 0; }

// ---- String helpers -------------------------------------------------------------------

// macdoze 0x10002f40: the digits of |value| in `radix` ('A' to 'Z' above 9, upper case), a
// '-' first for a negative value, written backwards and then reversed (_strrev).
char* _itoa(int32_t value, char* buffer, int32_t radix) {
    if (radix < 2 || radix > 36) {
        buffer[0] = '\0';
        return buffer;
    }
    int64_t rest = value;
    const bool negative = rest < 0;
    if (negative) rest = -rest;
    char* out = buffer;
    do {
        const int32_t digit = static_cast<int32_t>(rest % radix);
        *out++ = static_cast<char>(digit < 10 ? '0' + digit : '7' + digit);
        rest /= radix;
    } while (rest != 0);
    if (negative) *out++ = '-';
    *out = '\0';
    for (char *low = buffer, *high = out - 1; low < high; ++low, --high) std::swap(*low, *high);
    return buffer;
}

static int32_t LowerByte(char c) {
    const int32_t x = static_cast<unsigned char>(c);
    return x >= 'A' && x <= 'Z' ? x + 0x20 : x;
}

// macdoze 0x10003224: compares the lower-cased bytes, -1, 1 or 0; stops at the NUL or the count.
int32_t _strnicmp(const char* a, const char* b, int32_t count) {
    for (; count > 0; --count, ++a, ++b) {
        const int32_t x = LowerByte(*a);
        const int32_t y = LowerByte(*b);
        if (x != y) return x < y ? -1 : 1;
        if (x == 0) return 0;
    }
    return 0;
}

// The Latin-1 lower case of a wide character (macdoze's table covers the characters below 0x100).
static int32_t LowerWide(char16_t c) {
    if (c >= 'A' && c <= 'Z') return c + 0x20;
    if (c >= 0xc0 && c <= 0xde && c != 0xd7) return c + 0x20;
    return c;
}

// macdoze 0x100037c8: like _strnicmp on 16-bit characters.
int32_t _wcsnicmp(const char16_t* a, const char16_t* b, uint32_t count) {
    for (; count != 0; --count, ++a, ++b) {
        const int32_t x = LowerWide(*a);
        const int32_t y = LowerWide(*b);
        if (x != y) return x < y ? -1 : 1;
        if (x == 0) return 0;
    }
    return 0;
}

// ---- The date and time as text --------------------------------------------------------

namespace {
const char* const kDayNames[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* const kMonthNames[] = {"January", "February", "March",     "April",   "May",      "June",
                                   "July",    "August",   "September", "October", "November", "December"};

// The day of the week (0 Sunday) of a calendar date. The Mac's wDayOfWeek is 1 to 7
// (FileTimeToSystemTime), so the weekday is worked out from the date.
int32_t DayOfWeek(int32_t year, int32_t month, int32_t day) {
    static const int32_t offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 1 || month > 12) return 0;
    if (month < 3) --year;
    return (year + year / 4 - year / 100 + year / 400 + offsets[month - 1] + day) % 7;
}

_SYSTEMTIME NowLocal() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    _SYSTEMTIME time{};
    time.wYear = static_cast<uint16_t>(local.tm_year + 1900);
    time.wMonth = static_cast<uint16_t>(local.tm_mon + 1);
    time.wDayOfWeek = static_cast<uint16_t>(local.tm_wday);
    time.wDay = static_cast<uint16_t>(local.tm_mday);
    time.wHour = static_cast<uint16_t>(local.tm_hour);
    time.wMinute = static_cast<uint16_t>(local.tm_min);
    time.wSecond = static_cast<uint16_t>(local.tm_sec);
    return time;
}

void AppendNumber(std::string& out, int32_t value, int32_t minDigits) {
    std::string digits = std::to_string(value);
    while (static_cast<int32_t>(digits.size()) < minDigits) digits.insert(digits.begin(), '0');
    out += digits;
}

// Expands a Win32 picture: runs of the letters in `letters` are the fields, text in single quotes
// is copied, anything else is copied as it is.
template <typename Field>
std::string Expand(const char* picture, const char* letters, Field field) {
    std::string out;
    for (const char* p = picture; *p != '\0';) {
        if (*p == '\'') {
            ++p;
            while (*p != '\0') {
                if (*p == '\'') {
                    if (p[1] == '\'') out += '\'';   // '' inside quotes is one quote
                    else break;
                    p += 2;
                    continue;
                }
                out += *p++;
            }
            if (*p == '\'') ++p;
        } else if (std::strchr(letters, *p) != nullptr) {
            const char letter = *p;
            int32_t run = 0;
            while (*p == letter) ++run, ++p;
            field(out, letter, run);
        } else {
            out += *p++;
        }
    }
    return out;
}

// What the caller gets: the Win32 count (the text and its NUL), or the room it needs when size is 0.
// 0 when the buffer is too small.
int32_t Deliver(const std::string& text, char* out, int32_t size) {
    const int32_t needed = static_cast<int32_t>(text.size()) + 1;
    if (size == 0) return needed;
    if (out == nullptr || size < needed) return 0;
    std::memcpy(out, text.c_str(), needed);
    return needed;
}
}  // namespace

// macdoze 0x100064cc is an 8 byte body that answers 0 and writes nothing, so the caller
// (src/bullfrog/TbTimeStamp.cpp) copies whatever its buffer held. The host fills the text in
// instead: dddd ddd (weekday), MMMM MMM MM M (month), dd d (day), yyyy yy (year), in English.
int32_t GetDateFormatA(uint32_t, uint32_t, const _SYSTEMTIME* time, const char* picture, char* out, int32_t size) {
    const _SYSTEMTIME now = time != nullptr ? *time : NowLocal();
    const int32_t weekday = DayOfWeek(now.wYear, now.wMonth, now.wDay);
    const int32_t month = now.wMonth >= 1 && now.wMonth <= 12 ? now.wMonth - 1 : 0;
    const std::string text = Expand(picture != nullptr ? picture : "dddd, MMMM d, yyyy", "dMy",
                                    [&](std::string& result, char letter, int32_t run) {
        switch (letter) {
            case 'd':
                if (run >= 4) result += kDayNames[weekday];
                else if (run == 3) result += std::string(kDayNames[weekday]).substr(0, 3);
                else AppendNumber(result, now.wDay, run);
                break;
            case 'M':
                if (run >= 4) result += kMonthNames[month];
                else if (run == 3) result += std::string(kMonthNames[month]).substr(0, 3);
                else AppendNumber(result, now.wMonth, run);
                break;
            default:   // 'y'
                if (run >= 3) AppendNumber(result, now.wYear, 4);
                else AppendNumber(result, now.wYear % 100, run);
                break;
        }
    });
    return Deliver(text, out, size);
}

// macdoze 0x1000646c, likewise an 8 byte body that writes nothing. The host fills in h hh (12 hour),
// H HH (24 hour), m mm, s ss and t tt (AM/PM; t is the first letter).
int32_t GetTimeFormatA(uint32_t, uint32_t, const _SYSTEMTIME* time, const char* picture, char* out, int32_t size) {
    const _SYSTEMTIME now = time != nullptr ? *time : NowLocal();
    const int32_t twelve = now.wHour % 12 == 0 ? 12 : now.wHour % 12;
    const std::string text = Expand(picture != nullptr ? picture : "h:mm:ss tt", "hHmst",
                                    [&](std::string& result, char letter, int32_t run) {
        switch (letter) {
            case 'h': AppendNumber(result, twelve, run); break;
            case 'H': AppendNumber(result, now.wHour, run); break;
            case 'm': AppendNumber(result, now.wMinute, run); break;
            case 's': AppendNumber(result, now.wSecond, run); break;
            default:   // 't'
                result += now.wHour < 12 ? "AM" : "PM";
                if (run == 1) result.resize(result.size() - 1);
                break;
        }
    });
    return Deliver(text, out, size);
}

// ---- Disk and temp folder -------------------------------------------------------------

// macdoze 0x10005068 asked the File Manager (PBGetVInfoSync) about the volume: one sector
// per cluster, the allocation block size as the bytes per sector, and the free and total
// blocks. The game works out the free bytes in 32 bits (LbFile_GetFreeDriveSpace), so the
// free count is cut to what fits. Answers whether the volume could be asked.
int32_t GetDiskFreeSpace(const char*, uint32_t* sectorsPerCluster, uint32_t* bytesPerSector, uint32_t* freeClusters,
                         uint32_t* totalClusters) {
    struct statvfs info;
    std::error_code ec;
    const fs::path where = tpw::config().userDir.empty() ? fs::path("/") : tpw::config().userDir;
    if (statvfs(where.c_str(), &info) != 0 && statvfs("/", &info) != 0) return 0;
    const uint64_t blockSize = info.f_frsize != 0 ? info.f_frsize : 512;
    const uint64_t free = static_cast<uint64_t>(info.f_bavail);
    const uint64_t total = static_cast<uint64_t>(info.f_blocks);
    const uint64_t most = 0x7fffffffull / blockSize;
    if (sectorsPerCluster != nullptr) *sectorsPerCluster = 1;
    if (bytesPerSector != nullptr) *bytesPerSector = static_cast<uint32_t>(blockSize);
    if (freeClusters != nullptr) *freeClusters = static_cast<uint32_t>(free < most ? free : most);
    if (totalClusters != nullptr) *totalClusters = static_cast<uint32_t>(total < 0xffffffffull ? total : 0xffffffffull);
    return 1;
}

// macdoze 0x1000515c answered the length of the temp folder's path and copied it only when
// it fits (length < size). The game appends a file name straight on, so the path ends in '/'.
uint32_t GetTempPath(uint32_t size, char* path) {
    std::error_code ec;
    std::string folder = fs::temp_directory_path(ec).string();
    if (ec || folder.empty()) folder = "/tmp";
    if (folder.back() != '/') folder += '/';
    const uint32_t length = static_cast<uint32_t>(folder.size());
    if (length < size && path != nullptr) std::memcpy(path, folder.c_str(), length + 1);
    return length;
}

}  // namespace NS_MacDoze
