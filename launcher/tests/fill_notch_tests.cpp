// fill_notch_tests.cpp: the Settings switch "Fill Around Camera" (launcher/fill_notch.h, tpw-port issue #131). SDL's
// SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES is set to "0" only when launcher.ini's fill_notch is "1"; absent, "0" or any other
// value leaves SDL's default (no hint, full screen in a Space of its own, below the notch). With it on, a game that starts
// in full screen takes the screen's own shape (StartShape); otherwise the nearest of 4:3, 16:10 and 16:9, as always.
#include <SDL3/SDL.h>

#include <cstdio>
#include <string>

#include "fill_notch.h"

namespace {

int gFailures = 0;

void Check(bool ok, const std::string& what) {
    if (ok) return;
    ++gFailures;
    std::printf("FAILED: %s\n", what.c_str());
}

// The hint as SDL holds it after UseFillNotch(FillNotchSetting(value)) from no hint at all; null when it is not set.
std::string HintAfter(const std::string& value, bool& set, bool& returned) {
    SDL_ResetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES);
    returned = tpw::launcher::UseFillNotch(tpw::launcher::FillNotchSetting(value));
    const char* hint = SDL_GetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES);
    set = hint != nullptr;
    return hint != nullptr ? hint : "";
}

}  // namespace

int main() {
    Check(tpw::launcher::FillNotchSetting("1"), "fill_notch=1: on");
    for (const char* off : {"", "0", "2", "yes", "on", " 1"})
        Check(!tpw::launcher::FillNotchSetting(off), std::string("fill_notch=") + off + ": off");

    bool set = false, returned = false;
    const std::string on = HintAfter("1", set, returned);
    Check(set && on == "0" && returned, "fill_notch=1: SDL's full-screen Spaces hint is 0 (full screen without a Space)");
    Check(!SDL_GetHintBoolean(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, true), "fill_notch=1: SDL reads Spaces as off");
    for (const char* off : {"", "0", "yes"}) {
        HintAfter(off, set, returned);
        Check(!set && !returned, std::string("fill_notch=") + off + ": the hint is not set");
        Check(SDL_GetHintBoolean(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, true),
              std::string("fill_notch=") + off + ": SDL reads Spaces as on, its default");
    }

#ifdef TPW_PORT_FIX
    struct Shape {
        bool fillNotch;
        bool fullScreen;
        int screenWide, screenHigh;
        bool own;
        int wide, high;
        const char* what;
    };
    const Shape shapes[] = {
        {true, true, 2056, 1329, true, 2056, 1329, "a 16-inch MacBook's whole panel in points: its own shape, 2056:1329"},
        {true, true, 4112, 2658, true, 2056, 1329, "in pixels: the same, in lowest terms"},
        {true, true, 1512, 982, true, 756, 491, "a 14-inch MacBook's: 756:491"},
        {true, true, 2560, 1600, true, 8, 5, "a 16:10 screen: 8:5"},
        {true, true, 1920, 1080, true, 16, 9, "a 16:9 screen: 16:9"},
        {true, true, 1600, 1200, true, 4, 3, "a 4:3 screen: 4:3"},
        {true, true, 3440, 1440, false, 16, 9, "21:9, wider than the UI's layouts: the nearest, 16:9"},
        {true, true, 1280, 1024, false, 4, 3, "5:4, narrower: the nearest, 4:3"},
        {true, true, 1080, 1920, false, 4, 3, "upright: the nearest, 4:3"},
        {true, true, 0, 0, false, 4, 3, "no screen: not its own"},
        {true, false, 2056, 1329, false, 16, 10, "a window: the nearest, 16:10"},
        {false, true, 2056, 1329, false, 16, 10, "the switch off: the nearest, 16:10, as always"},
        {false, true, 1512, 982, false, 16, 10, "the switch off, a 14-inch MacBook: 16:10"},
        {false, true, 1920, 1080, false, 16, 9, "the switch off, 16:9: 16:9"},
        {false, true, 1280, 1024, false, 4, 3, "the switch off, 5:4: 4:3"},
        {false, false, 2056, 1329, false, 16, 10, "the switch off, a window: 16:10"},
    };
    for (const Shape& shape : shapes) {
        int wide = -1, high = -1;
        const bool own = tpw::launcher::StartShape(shape.fillNotch, shape.fullScreen, shape.screenWide, shape.screenHigh, wide, high);
        Check(own == shape.own && wide == shape.wide && high == shape.high,
              std::string(shape.what) + " (got " + std::to_string(wide) + ":" + std::to_string(high) + ")");
    }
#endif

    if (gFailures == 0) std::printf("fill_notch: all checks passed\n");
    return gFailures == 0 ? 0 : 1;
}
