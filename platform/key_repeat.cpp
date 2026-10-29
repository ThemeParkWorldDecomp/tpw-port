// key_repeat.cpp: how soon and how often a held key repeats, from the system's keyboard settings. The Mac's Event
// Manager sent the autoKey events itself, after the Keyboard control panel's "Delay Until Repeat" and at its "Key
// Repeat Rate"; the run loop (gamework.cpp) makes them the same way, with the times the host's own settings give.
#include <algorithm>
#include <cmath>

#include "platform.h"

#if defined(__APPLE__)
#include <objc/message.h>
#include <objc/runtime.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace tpw {

namespace {
// Seconds from the system, in milliseconds: a value that is not a number keeps the fallback, a negative one is 0, and
// none is longer than an hour (macOS gives a very long delay when key repeat is Off: then a held key never repeats).
uint32_t Milliseconds(double seconds, uint32_t fallback) {
    if (!std::isfinite(seconds)) return seconds > 0 ? 3600000u : fallback;
    if (seconds < 0) return 0;
    if (seconds > 3600) return 3600000u;
    return static_cast<uint32_t>(std::lround(seconds * 1000.0));
}
}  // namespace

KeyRepeatTiming WindowsKeyRepeat(uint32_t delaySetting, uint32_t speedSetting) {
    // SPI_GETKEYBOARDDELAY is 0 to 3: about 250 ms to 1 second. SPI_GETKEYBOARDSPEED is 0 to 31: about 2.5 to 30
    // repeats a second, in even steps.
    KeyRepeatTiming timing;
    timing.delayMs = 250u * (std::min(delaySetting, 3u) + 1u);
    const double perSecond = 2.5 + static_cast<double>(std::min(speedSetting, 31u)) * 27.5 / 31.0;
    timing.intervalMs = static_cast<uint32_t>(std::lround(1000.0 / perSecond));
    return timing;
}

KeyRepeatTiming SystemKeyRepeat() {
    KeyRepeatTiming timing;
#if defined(__APPLE__)
    // [NSEvent keyRepeatDelay] and [NSEvent keyRepeatInterval]: the Keyboard settings' "Delay until repeat" and "Key
    // repeat rate", in seconds.
    Class event = objc_getClass("NSEvent");
    if (event != nullptr) {
        using Seconds = double (*)(Class, SEL);
        const Seconds send = reinterpret_cast<Seconds>(objc_msgSend);
        timing.delayMs = Milliseconds(send(event, sel_registerName("keyRepeatDelay")), timing.delayMs);
        timing.intervalMs = Milliseconds(send(event, sel_registerName("keyRepeatInterval")), timing.intervalMs);
    }
#elif defined(_WIN32)
    UINT delay = 0, speed = 0;
    if (SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &delay, 0) &&
        SystemParametersInfoW(SPI_GETKEYBOARDSPEED, 0, &speed, 0))
        timing = WindowsKeyRepeat(delay, speed);
#endif
    // Linux and the rest: the fallback (half a second, then about 30 a second, the usual desktop defaults).
    if (timing.intervalMs == 0) timing.intervalMs = 1;
    return timing;
}

}  // namespace tpw
