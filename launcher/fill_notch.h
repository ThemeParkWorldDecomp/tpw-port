// fill_notch.h: the launcher's Settings switch "Fill Around Camera" (launcher.ini fill_notch, launcher/ui/launcher_ui.h
// kFillNotchKey; a Mac's only). On a MacBook with a camera notch the game's full screen is a Space of its own, which macOS
// keeps below the notch, with a black band above the game. With the switch on, SDL makes full screen without a Space
// (SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES "0"), a borderless window over the whole screen, the strips beside the notch
// included; the notch then hides a strip at the top centre. Found by djlongy (tpw-port issue #131).
//
// SDL's Cocoa video reads the hint once, when video starts (Cocoa_VideoInit), not at each switch to full screen, so it is
// set before the game's video starts. The window's Play starts the game as a process of its own (launcher/game_app.h),
// which reads launcher.ini afresh, so a change in Settings counts from the next Play.
//
// Below the notch a MacBook's screen is 16:10, the shape the game would take; the whole panel is taller (2056 x 1329
// points), so a game starting in full screen with the switch on takes the panel's own shape (StartShape) and fills it.
#pragma once

#include <SDL3/SDL.h>

#include <numeric>
#include <string>

#ifdef TPW_PORT_FIX
#include "UI_Base.h"
#endif

namespace tpw::launcher {

// Whether launcher.ini's value turns the switch on: only "1" (absent, "0" or anything else: off, the full screen as it
// has always been).
inline bool FillNotchSetting(const std::string& value) { return value == "1"; }

// On: asks SDL for full screen without a Space. Off: leaves SDL's default (Spaces) alone. True when the hint was set.
inline bool UseFillNotch(bool on) { return on && SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0"); }

#ifdef TPW_PORT_FIX
// The shape a game starts at on a screen of `screenWide` x `screenHigh`: with the switch in effect (`fillNotch`) and the game
// starting in full screen, the screen's own, in lowest terms (2056 x 1329 points: 2056:1329), as full screen then covers
// the whole panel; otherwise, and on a screen narrower than 4:3 or wider than 16:9, the nearest of the three the UI was
// laid out for (UI_NearestShape: a MacBook's is 16:10), as always. True when it is the screen's own.
inline bool StartShape(bool fillNotch, bool fullScreen, int screenWide, int screenHigh, int& wide, int& high) {
    if (fillNotch && fullScreen && screenWide > 0 && screenHigh > 0 && screenWide * 3 >= screenHigh * 4 &&
        screenWide * 9 <= screenHigh * 16) {
        const int divisor = std::gcd(screenWide, screenHigh);
        wide = screenWide / divisor;
        high = screenHigh / divisor;
        return true;
    }
    const UISizeShape& nearest = kUISizeShapes[UI_NearestShape(screenWide, screenHigh)];
    wide = nearest.mWide;
    high = nearest.mHigh;
    return false;
}
#endif

}  // namespace tpw::launcher
