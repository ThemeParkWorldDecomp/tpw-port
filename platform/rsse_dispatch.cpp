// rsse_dispatch.cpp: the C library imports that ProcessInstruction (0x100d0b30) and FLY_ForceExtractPeep reach through glue
// (include/rsse.h, namespace processinstruction_callee) so that their test libraries can record them. In the launcher they are
// the library calls themselves. The Mac's MSL counts time from 1900, not 1970; only TimeField reads it, and only through
// LocalTime, so the fields come out the same.
#include <cstring>
#include <ctime>

#include "platform/samsutils.h"
#include "rsse.h"

namespace processinstruction_callee {

// labs on the Mac is a negate without a check, so the most negative long answers itself.
int32_t Labs(int32_t value) {
    return value < 0 ? static_cast<int32_t>(0u - static_cast<uint32_t>(value)) : value;
}

char* StrCpy(char* to, const char* from) { return std::strcpy(to, from); }

char* StrCat(char* to, const char* from) { return std::strcat(to, from); }

int32_t StrCmp(const char* a, const char* b) { return std::strcmp(a, b); }

int32_t Time(int32_t* out) {
    const int32_t now = static_cast<int32_t>(SamsUtilities::UTimer::GetWallSeconds());
    if (out != nullptr) *out = now;
    return now;
}

// The struct tm the caller reads: nine longs, tm_sec first (the host's tm has more fields and wider ones).
const int32_t* LocalTime(const int32_t* when) {
    static int32_t fields[9];
    const std::time_t t = when != nullptr ? static_cast<std::time_t>(*when) : 0;
    const std::tm* tm = std::localtime(&t);
    if (tm == nullptr) return nullptr;
    const int f[9] = {tm->tm_sec, tm->tm_min, tm->tm_hour, tm->tm_mday, tm->tm_mon,
                      tm->tm_year, tm->tm_wday, tm->tm_yday, tm->tm_isdst};
    for (int i = 0; i < 9; ++i) fields[i] = f[i];
    return fields;
}

}  // namespace processinstruction_callee
