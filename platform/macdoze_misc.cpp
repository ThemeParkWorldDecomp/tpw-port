// macdoze_misc.cpp: the Win32 calls of macdoze that need no window: memory, counters,
// strings, code page conversions.
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>

#include "platform.h"
#include "platform/arena.h"
#include "platform/ltms.h"
#include "platform/macdoze.h"

namespace NS_MacDoze {

void ZeroMemory(void* block, uint32_t size) { std::memset(block, 0, size); }
// The Mac shim moved the bytes with BlockMoveData, which allows the two to overlap.
void CopyMemory(void* dest, const void* source, uint32_t size) { std::memmove(dest, source, size); }

// GlobalAlloc: a fixed block, so the handle is the pointer. 0x40 (GMEM_ZEROINIT) clears it.
// The blocks come from the arena: this is the allocator the game gives the engine, and
// the engine reads its file images into these blocks (include/Ptr32.h).
InternalHandle* GlobalAlloc(uint32_t flags, uint32_t size) {
    void* block = (flags & 0x40) != 0 ? tpw::ArenaAllocZeroed(size != 0 ? size : 1) : tpw::ArenaAlloc(size != 0 ? size : 1);
    return static_cast<InternalHandle*>(block);
}

InternalHandle* GlobalFree(InternalHandle* block) {
    tpw::ArenaFree(block);
    return nullptr;   // NULL: freed
}

// The machine the engine sizes its procedural textures for (tb_init.cpp: 16-pixel tiles
// under 48 MB, 32 under 64 MB). Reported as a 1 GB machine with half of it free, the same
// machine Gestalt describes.
void GlobalMemoryStatus(_MEMORYSTATUS* status) {
    status->dwLength = sizeof *status;
    status->dwMemoryLoad = 50;
    status->dwTotalPhys = 0x40000000;
    status->dwAvailPhys = 0x20000000;
    status->dwTotalPageFile = 0x40000000;
    status->dwAvailPageFile = 0x20000000;
    status->dwTotalVirtual = 0x7fffffff;
    status->dwAvailVirtual = 0x40000000;
}

// Critical sections: the word the game passes holds a recursive mutex, made the first time it
// is entered (the game makes a handful and never frees them). The Mac shim's were
// cooperative-thread locks (macdoze 0x1000bb14, 0x1000bb74).
void EnterCriticalSection(void** handle) {
    if (*handle == nullptr) *handle = new std::recursive_mutex;
    static_cast<std::recursive_mutex*>(*handle)->lock();
}

void LeaveCriticalSection(void** handle) {
    if (*handle != nullptr) static_cast<std::recursive_mutex*>(*handle)->unlock();
}

// The new value. The game runs on one thread, and so did the Mac shim (cooperative
// threads), so these are plain arithmetic.
int32_t InterlockedIncrement(int32_t* value) { return ++*value; }
int32_t InterlockedDecrement(int32_t* value) { return --*value; }

// Whether a byte starts a double-byte character in the system code page. The host side
// of that code page is single-byte (see the conversions below), so never.
int32_t IsDBCSLeadByte(uint8_t) { return 0; }

char* _strdup(const char* text) {
    const size_t size = std::strlen(text) + 1;
    char* copy = static_cast<char*>(std::malloc(size));
    if (copy != nullptr) std::memcpy(copy, text, size);
    return copy;
}

static int32_t Lower(int32_t c) { return c >= 'A' && c <= 'Z' ? c + 0x20 : c; }

int32_t _stricmp(const char* a, const char* b) {
    for (;; ++a, ++b) {
        const int32_t x = Lower(static_cast<unsigned char>(*a));
        const int32_t y = Lower(static_cast<unsigned char>(*b));
        if (x != y) return x < y ? -1 : 1;
        if (x == 0) return 0;
    }
}

int32_t _wcsicmp(const char16_t* a, const char16_t* b) {
    for (;; ++a, ++b) {
        const int32_t x = Lower(*a);
        const int32_t y = Lower(*b);
        if (x != y) return x < y ? -1 : 1;
        if (x == 0) return 0;
    }
}

char16_t* _itow(int32_t value, char16_t* text, int32_t radix) {
    char16_t digits[40];
    int32_t count = 0;
    // As in Win32: only base 10 shows a sign; other bases print the 32-bit pattern.
    const bool negative = radix == 10 && value < 0;
    uint32_t rest = negative ? 0u - static_cast<uint32_t>(value) : static_cast<uint32_t>(value);
    if (radix < 2 || radix > 36) radix = 10;
    do {
        const uint32_t digit = rest % static_cast<uint32_t>(radix);
        digits[count++] = static_cast<char16_t>(digit < 10 ? u'0' + digit : u'a' + digit - 10);
        rest /= static_cast<uint32_t>(radix);
    } while (rest != 0);
    char16_t* out = text;
    if (negative) *out++ = u'-';
    while (count > 0) *out++ = digits[--count];
    *out = 0;
    return text;
}

// Code page conversions. The game converts names it shows or uses as file names; the host
// side of the "system code page" is Latin-1 here: characters up to 0xff map one to one,
// anything else becomes the default character ('?').
// TODO(platform): the Mac converted through the system script (Mac OS Roman); file names
// with accents need the real table once saved games carry such names.
int32_t WideCharToMultiByte(uint32_t, uint32_t, const char16_t* text, int32_t length, char* out, int32_t size,
                            const char* defaultChar, int32_t* usedDefault) {
    if (length < 0) {   // NUL-terminated: the NUL is converted too
        length = 0;
        while (text[length] != 0) ++length;
        ++length;
    }
    if (usedDefault != nullptr) *usedDefault = 0;
    if (size == 0) return length;
    int32_t written = 0;
    for (; written < length && written < size; ++written) {
        const char16_t c = text[written];
        if (c <= 0xff) {
            out[written] = static_cast<char>(c);
        } else {
            out[written] = defaultChar != nullptr ? *defaultChar : '?';
            if (usedDefault != nullptr) *usedDefault = 1;
        }
    }
    return written < length ? 0 : written;   // 0: the buffer was too small
}

int32_t MultiByteToWideChar(uint32_t, uint32_t, const char* text, int32_t length, char16_t* out, int32_t size) {
    if (length < 0) length = static_cast<int32_t>(std::strlen(text)) + 1;
    if (size == 0) return length;
    int32_t written = 0;
    for (; written < length && written < size; ++written) out[written] = static_cast<unsigned char>(text[written]);
    return written < length ? 0 : written;
}

// macdoze 0x10005fe4 (240 bytes, unit w125). The FILETIME is taken as seconds since the Mac's epoch (1 January 1904): the ticks less
// 95616284229632000, divided by 10,000,000 with the signed 64-bit divide (truncating toward zero; the constant is the one CompareFileTime
// uses). The Mac's LongSecondsToDate then makes the calendar date of those seconds (proleptic Gregorian, no time zone, no daylight saving)
// and the original copies its year, month, day of week, day, hour, minute and second; the milliseconds are always 0. The day of the week is
// the Mac's (1 Sunday to 7 Saturday: 1 January 1904 was a Friday), copied unchanged. Always 1.
// Assumption (not checked against the Toolbox): seconds before 1904 (a negative count) are floored to a calendar date the same way.
int32_t FileTimeToSystemTime(const tagFILETIME* fileTime, _SYSTEMTIME* time) {
    const uint64_t ticks = (static_cast<uint64_t>(fileTime->dwHighDateTime) << 32) + fileTime->dwLowDateTime;
    const int64_t seconds = static_cast<int64_t>(ticks - 95616284229632000ull) / 10000000;
    int64_t days = seconds / 86400;
    int64_t rest = seconds % 86400;
    if (rest < 0) {
        rest += 86400;
        --days;
    }
    // Days since 1 January 1904 to a civil date (the days-from-civil algorithm, shifted to 1 March of year 0).
    const int64_t z = days + 695361;   // 695361: days from 1 March 0000 to 1 January 1904 (719468 to 1970, less the 24107 days back to 1904)
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t dayOfEra = z - era * 146097;
    const int64_t yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    const int64_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const int64_t monthIndex = (5 * dayOfYear + 2) / 153;
    const int64_t day = dayOfYear - (153 * monthIndex + 2) / 5 + 1;
    const int64_t month = monthIndex < 10 ? monthIndex + 3 : monthIndex - 9;
    const int64_t year = yearOfEra + era * 400 + (month <= 2 ? 1 : 0);
    int64_t weekday = (days + 5) % 7;
    if (weekday < 0) weekday += 7;
    time->wYear = static_cast<uint16_t>(year);
    time->wMonth = static_cast<uint16_t>(month);
    time->wDayOfWeek = static_cast<uint16_t>(weekday + 1);
    time->wDay = static_cast<uint16_t>(day);
    time->wHour = static_cast<uint16_t>(rest / 3600);
    time->wMinute = static_cast<uint16_t>(rest % 3600 / 60);
    time->wSecond = static_cast<uint16_t>(rest % 60);
    time->wMilliseconds = 0;
    return 1;
}

// macdoze 0x10005f08 (144 bytes). GetDateTime (the Mac's local clock in seconds since 1904) and LongSecondsToDate make the calendar date; the
// original copies its year, month, day of week, day, hour, minute and second, and the milliseconds are always 0. The day of the week is
// the Mac's (1 Sunday to 7 Saturday), as in FileTimeToSystemTime. The host's local clock replaces GetDateTime.
void GetLocalTime(_SYSTEMTIME* time) {
    const std::time_t now = std::time(nullptr);
    std::tm local;
    localtime_r(&now, &local);
    time->wYear = static_cast<uint16_t>(local.tm_year + 1900);
    time->wMonth = static_cast<uint16_t>(local.tm_mon + 1);
    time->wDayOfWeek = static_cast<uint16_t>(local.tm_wday + 1);
    time->wDay = static_cast<uint16_t>(local.tm_mday);
    time->wHour = static_cast<uint16_t>(local.tm_hour);
    time->wMinute = static_cast<uint16_t>(local.tm_min);
    time->wSecond = static_cast<uint16_t>(local.tm_sec);
    time->wMilliseconds = 0;
}

// macdoze 0x10006144 (208 bytes). The inverse of FileTimeToSystemTime: the Mac's LongDateToSeconds makes seconds since 1904 of the year,
// month, day, hour, minute and second (the day of the week and the milliseconds are not read), which become ticks of 100 ns (the 64-bit
// multiply by 10,000,000) and get the same 95616284229632000 added that FileTimeToSystemTime takes off. Always 1. Assumption (not checked
// against the Toolbox): a month outside 1 to 12 carries into the year.
int32_t SystemTimeToFileTime(const _SYSTEMTIME* time, tagFILETIME* fileTime) {
    int64_t year = time->wYear;
    int64_t month = static_cast<int64_t>(time->wMonth) - 1;
    year += month >= 0 ? month / 12 : -((11 - month) / 12);
    month = ((month % 12) + 12) % 12 + 1;
    // Days from 1 March 0000 to the civil date (the days-from-civil algorithm), less the 695361 days to 1 January 1904.
    if (month <= 2) --year;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const int64_t yearOfEra = year - era * 400;
    const int64_t dayOfYear = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + time->wDay - 1;
    const int64_t dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    const int64_t days = era * 146097 + dayOfEra - 695361;
    const int64_t seconds = days * 86400 + time->wHour * 3600 + time->wMinute * 60 + time->wSecond;
    const uint64_t ticks = static_cast<uint64_t>(seconds) * 10000000ull + 95616284229632000ull;
    fileTime->dwLowDateTime = static_cast<uint32_t>(ticks);
    fileTime->dwHighDateTime = static_cast<uint32_t>(ticks >> 32);
    return 1;
}

}  // namespace NS_MacDoze
