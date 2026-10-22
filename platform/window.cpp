// window.cpp: the game's one window and its OpenGL context. The game makes the window with
// the Mac Window Manager (NewCWindow), asks indirectx for an OpenGL "DirectDraw
// implementation" on it (IdxMakeOpenGLImplementation, which owns the UOpenGL context), and
// moves and sizes it with macdoze's Win32 window calls.
//
// The window FITS THE SCREEN (platform/fit.h, unless --no-fit): the game draws at its own resolution into an
// offscreen framebuffer (platform/offscreen.cpp), and every presented frame is scaled into the window, which is as large
// as the screen allows with the game's aspect ratio and never has more pixels than the game. The game is told its
// own size, not the window's, and the mouse is mapped back through the letterbox. The game's full-screen box (procID 2)
// is a real full-screen window, the whole screen in a Space of its own (not with --windowed, --background or --no-fit).
#include <SDL3/SDL.h>
#if defined(__APPLE__)
#include <objc/message.h>
#include <objc/runtime.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "fit.h"
#include "handoff.h"
#include "offscreen.h"
#include "platform.h"
#include "platform/indirectx.h"
#include "platform/macdoze.h"

// The three OpenGL calls the frame dump needs (the game's own are in platform/opengl.h).
extern "C" {
void glPixelStorei(unsigned int name, int value);
void glReadBuffer(unsigned int mode);
void glReadPixels(int x, int y, int width, int height, unsigned int format, unsigned int type, void* pixels);
}

namespace {
constexpr unsigned int kGlPackAlignment = 0x0D05, kGlBack = 0x0405, kGlRgb = 0x1907, kGlUnsignedByte = 0x1401;
constexpr unsigned int kGlColorAttachment0 = 0x8CE0;   // GL_COLOR_ATTACHMENT0_EXT
tpw::Window gWindow;
bool gWindowMade = false;
bool gVideoStarted = false;

// The fit: the game's resolution (what the game is told, whatever the window is) and the screen the window is fitted to.
struct FitState {
    bool wanted = false;        // the window was fitted (no --no-fit); false again if the framebuffer could not be made
    int gameWide = 0;
    int gameHigh = 0;
    SDL_DisplayID display = 0;
    bool fullScreen = false;    // a real full-screen window (fit::RealFullScreen): the whole screen, a Space of its own
} gFit;

// The window's size in points, and the game's picture inside it (points from the top left).
tpw::fit::Rect LetterboxInPoints() {
    int wide = 0, high = 0;
    SDL_GetWindowSize(gWindow.sdl, &wide, &high);
    return tpw::fit::Letterbox(wide, high, gFit.gameWide, gFit.gameHigh);
}

bool StartVideo() {
    if (gVideoStarted) return true;
    if (tpw::config().background) {
        // Shown and raised without becoming the key window (the Cocoa orderFront, not makeKeyAndOrderFront).
        SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
        SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, "0");
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        tpw::Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    gVideoStarted = true;
    return true;
}

// --background: the window goes behind every other window of the screen once shown (Cocoa's orderBack, through
// the Objective-C runtime so the platform stays C++). SDL has no call for it.
static void SendWindowToBack() {
#if defined(__APPLE__)
    void* nsWindow = SDL_GetPointerProperty(SDL_GetWindowProperties(gWindow.sdl), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    if (nsWindow == nullptr) return;
    using Send = void (*)(void*, SEL, void*);
    reinterpret_cast<Send>(objc_msgSend)(nsWindow, sel_registerName("orderBack:"), nullptr);
    tpw::Log("window sent to the back (--background)");
#endif
}

// The frame about to be presented (with the cursor over it), saved as a picture (Config::dumpFramesDir) when it
// differs from the one before it (every Config::dumpEvery'th of those); at most
// Config::dumpLimit pictures (200 unless --dump-limit says otherwise). Each saved picture is
// logged with the milliseconds since start, so a clip can be cut at real speed.
void DumpFrame() {
    static int32_t count = 0, seen = 0;
    static std::vector<uint8_t> last;
    if (count >= std::max(tpw::config().dumpLimit, 0)) return;
    int width = 0, height = 0;
    if (gFit.wanted) {   // the picture the game made, whatever the window is
        width = gFit.gameWide;
        height = gFit.gameHigh;
    } else {
        SDL_GetWindowSizeInPixels(gWindow.sdl, &width, &height);
    }
    if (width <= 0 || height <= 0) return;
    const size_t pitch = static_cast<size_t>(width) * 3;
    std::vector<uint8_t> bottomUp(pitch * static_cast<size_t>(height)), topDown(bottomUp.size());
    glPixelStorei(kGlPackAlignment, 1);
    glReadBuffer(gFit.wanted ? kGlColorAttachment0 : kGlBack);   // (the present framebuffer is bound: see SwapBuffers)
    glReadPixels(0, 0, width, height, kGlRgb, kGlUnsignedByte, bottomUp.data());
    if (bottomUp == last) return;
    last = bottomUp;
    if (seen++ % std::max(tpw::config().dumpEvery, 1) != 0) return;
    for (int row = 0; row < height; ++row) {
        std::memcpy(&topDown[pitch * static_cast<size_t>(row)], &bottomUp[pitch * static_cast<size_t>(height - 1 - row)], pitch);
    }
    SDL_Surface* surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGB24, topDown.data(), static_cast<int>(pitch));
    if (surface == nullptr) return;
    char name[32];
    std::snprintf(name, sizeof name, "frame-%04d.bmp", count++);
    std::error_code ec;
    std::filesystem::create_directories(tpw::config().dumpFramesDir, ec);
    if (!SDL_SaveBMP(surface, (tpw::config().dumpFramesDir / name).string().c_str())) {
        tpw::Log("could not save %s: %s", name, SDL_GetError());
    } else {
        tpw::Log("dumped %s at %llu ms (pass %d)", name, static_cast<unsigned long long>(SDL_GetTicks()), tpw::CurrentPass());
    }
    SDL_DestroySurface(surface);
}

void SetPortRect(int32_t width, int32_t height) {
    gWindow.portRect.left = 0;
    gWindow.portRect.top = 0;
    gWindow.portRect.right = static_cast<int16_t>(width);
    gWindow.portRect.bottom = static_cast<int16_t>(height);
}
}  // namespace

static void RefitWindow();   // below, with the display choice it needs
static void DisableFit(const char* why);

namespace tpw {

Window* MainWindow() { return gWindowMade ? &gWindow : nullptr; }

std::string WindowTitle(const std::string& edition, const unsigned char* title) {
    if (edition == "stp") return "SimTheme Park";
    return std::string(reinterpret_cast<const char*>(title + 1), title[0]);
}

void WindowOrigin(int32_t& x, int32_t& y) {
    x = y = 0;
    if (gWindowMade) {
        int wx = 0, wy = 0;
        SDL_GetWindowPosition(gWindow.sdl, &wx, &wy);
        x = wx;
        y = wy;
    }
}

void WindowSizes(int& pointsWide, int& pointsHigh, int& pixelsWide, int& pixelsHigh) {
    pointsWide = pointsHigh = pixelsWide = pixelsHigh = 0;
    if (!gWindowMade) return;
    if (gFit.wanted) {   // the surface the game draws into: its own resolution, one point to the pixel
        pointsWide = pixelsWide = gFit.gameWide;
        pointsHigh = pixelsHigh = gFit.gameHigh;
        return;
    }
    SDL_GetWindowSize(gWindow.sdl, &pointsWide, &pointsHigh);
    SDL_GetWindowSizeInPixels(gWindow.sdl, &pixelsWide, &pixelsHigh);
}

bool FitActive() { return gWindowMade && gFit.wanted; }

int LargestGameSize() {
    if (!StartVideo()) return 0;
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window* probe = SDL_CreateWindow("", 1, 1, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (probe == nullptr) return 0;
    int largest = 0;
    if (SDL_GLContext context = SDL_GL_CreateContext(probe)) {
        largest = offscreen::LargestSize();
        SDL_GL_MakeCurrent(probe, nullptr);
        SDL_GL_DestroyContext(context);
    }
    SDL_DestroyWindow(probe);
    return largest;
}

// The game's resolution, as the game sets it (SetDisplayMode, MoveWindow). Without the fit the window is resized to it,
// as before; with it the window is re-fitted and the framebuffer made again at the new size.
void SetGameSize(int wide, int high) {
    if (!gWindowMade || wide <= 0 || high <= 0) return;
    if (!gFit.wanted) {
        SDL_SetWindowSize(gWindow.sdl, wide, high);
        SDL_SyncWindow(gWindow.sdl);
        return;
    }
    if (wide == gFit.gameWide && high == gFit.gameHigh) return;
    gFit.gameWide = wide;
    gFit.gameHigh = high;
    SetPortRect(wide, high);
    if (offscreen::Ready() && !offscreen::Create(wide, high)) {
        DisableFit("could not make the framebuffer for the new resolution");
        return;
    }
    RefitWindow();
}

void GameSize(int& wide, int& high) {
    wide = high = 0;
    if (!gWindowMade) return;
    if (gFit.wanted) {
        wide = gFit.gameWide;
        high = gFit.gameHigh;
    } else {
        SDL_GetWindowSize(gWindow.sdl, &wide, &high);
    }
}

void MouseToGame(double x, double y, double& gameX, double& gameY) {
    gameX = x;
    gameY = y;
    if (!gWindowMade || !gFit.wanted) return;
    fit::WindowToGame(LetterboxInPoints(), gFit.gameWide, gFit.gameHigh, x, y, gameX, gameY);
    gameX = fit::ClampToGame(gameX, gFit.gameWide);
    gameY = fit::ClampToGame(gameY, gFit.gameHigh);
}

bool CursorPlacement(CursorPlace& place) {
    if (!gWindowMade) return false;
    float mouseX = 0.0f, mouseY = 0.0f;
    SDL_GetMouseState(&mouseX, &mouseY);
    int pointsWide = 0, pointsHigh = 0, pixelsWide = 0, pixelsHigh = 0;
    SDL_GetWindowSize(gWindow.sdl, &pointsWide, &pointsHigh);
    SDL_GetWindowSizeInPixels(gWindow.sdl, &pixelsWide, &pixelsHigh);
    if (pointsWide <= 0 || pointsHigh <= 0 || pixelsWide <= 0 || pixelsHigh <= 0) return false;
    float scriptedX = 0.0f, scriptedY = 0.0f;
    bool scriptedDown = false;
    if (ScriptedPointer(scriptedX, scriptedY, scriptedDown)) {   // a scripted click: its game pixel is the cursor
        place.x = scriptedX;
        place.y = scriptedY;
        place.scaleX = place.scaleY = 1.0f;
        if (gFit.wanted) {
            place.surfaceWide = gFit.gameWide;
            place.surfaceHigh = gFit.gameHigh;
        } else {   // without the fit a game pixel is a point of the window
            place.scaleX = static_cast<float>(pixelsWide) / static_cast<float>(pointsWide);
            place.scaleY = static_cast<float>(pixelsHigh) / static_cast<float>(pointsHigh);
            place.surfaceWide = pixelsWide;
            place.surfaceHigh = pixelsHigh;
        }
        return true;
    }
    if (mouseX < 0.0f || mouseY < 0.0f || mouseX >= static_cast<float>(pointsWide) ||
        mouseY >= static_cast<float>(pointsHigh)) {
        if (config().dumpFramesDir.empty()) return false;
        mouseX = static_cast<float>(pointsWide) * 0.5f;
        mouseY = static_cast<float>(pointsHigh) * 0.5f;
    }
    if (gFit.wanted) {   // game pixels in a surface of the game's size
        double gameX = 0.0, gameY = 0.0;
        MouseToGame(mouseX, mouseY, gameX, gameY);
        place.x = static_cast<float>(gameX);
        place.y = static_cast<float>(gameY);
        place.scaleX = place.scaleY = 1.0f;
        place.surfaceWide = gFit.gameWide;
        place.surfaceHigh = gFit.gameHigh;
        return true;
    }
    place.x = mouseX;
    place.y = mouseY;
    place.scaleX = static_cast<float>(pixelsWide) / static_cast<float>(pointsWide);
    place.scaleY = static_cast<float>(pixelsHigh) / static_cast<float>(pointsHigh);
    place.surfaceWide = pixelsWide;
    place.surfaceHigh = pixelsHigh;
    return true;
}

// The frame: with the fit, the game's framebuffer is copied (the cursor is drawn on the copy, so the game's own picture
// never carries it, and the dump shows what the player sees), the copy is saved if frames are being dumped, then scaled
// into the window; the game's framebuffer is bound again for its next frame.
void SwapBuffers() {
    if (!gWindowMade || gWindow.glContext == nullptr) return;
    if (gFit.wanted && offscreen::Ready()) {
        offscreen::CopyGameToPresent();
        offscreen::BindPresent();
        DrawCursorOverlay();
        if (!config().dumpFramesDir.empty()) DumpFrame();
        int pixelsWide = 0, pixelsHigh = 0;
        SDL_GetWindowSizeInPixels(gWindow.sdl, &pixelsWide, &pixelsHigh);
        const fit::PixelBox box = fit::RoundBox(fit::Letterbox(pixelsWide, pixelsHigh, gFit.gameWide, gFit.gameHigh));
        offscreen::ScaleToWindow(box, pixelsWide, pixelsHigh);
        // The swap presents nothing while a framebuffer object is bound (macOS, 2026-10-13); the game's is rebound after.
        offscreen::BindDefault();
        offscreen::DrainErrors("presenting a frame");
        SDL_GL_SwapWindow(gWindow.sdl);
        offscreen::BindGame();
        return;
    }
    DrawCursorOverlay();
    if (!config().dumpFramesDir.empty()) DumpFrame();
    SDL_GL_SwapWindow(gWindow.sdl);
}

}  // namespace tpw

// The screen --display names (an SDL display index, or a case-blind part of the display's name, such as "builtin" for
// the MacBook's own screen), or 0 when none was asked for or none matches. The screens are listed in the log once, so
// the names can be seen without guessing.
static SDL_DisplayID ChooseDisplay() {
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    if (displays == nullptr) return 0;
    auto lower = [](std::string text) {
        for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    };
    const std::string wanted = lower(tpw::config().display);
    SDL_DisplayID chosen = 0;
    for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetDisplayName(displays[i]);
        const std::string shown = name != nullptr ? name : "";
        tpw::Log("display %d: \"%s\"%s", i, shown.c_str(), displays[i] == SDL_GetPrimaryDisplay() ? " (main)" : "");
        if (wanted.empty() || chosen != 0) continue;
        const bool byIndex = !wanted.empty() && std::all_of(wanted.begin(), wanted.end(), ::isdigit) &&
                             std::atoi(wanted.c_str()) == i;
        const std::string lowered = lower(shown);
        const bool byName = !byIndex && lowered.find(wanted) != std::string::npos;
        // "builtin" also matches "Built-in Retina Display" (the hyphen).
        const bool builtin = !byIndex && !byName && wanted == "builtin" && lowered.find("built-in") != std::string::npos;
        if (byIndex || byName || builtin) chosen = displays[i];
    }
    if (!wanted.empty() && chosen == 0) {
        tpw::Log("--display \"%s\": no such screen; using SDL's choice", tpw::config().display.c_str());
    }
    SDL_free(displays);
    return chosen;
}

uint32_t tpw::ChosenDisplay() {
    return ChooseDisplay();
}

// Without the fit: moves the window to the chosen screen (if one was asked for) and logs where it ended up.
static void PlaceOnChosenDisplay(SDL_Window* window, SDL_DisplayID chosen) {
    if (chosen != 0) {
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(chosen), SDL_WINDOWPOS_CENTERED_DISPLAY(chosen));
    }
    const SDL_DisplayID now = SDL_GetDisplayForWindow(window);
    const char* nowName = now != 0 ? SDL_GetDisplayName(now) : nullptr;
    tpw::Log("window on display \"%s\"", nowName != nullptr ? nowName : "?");
}

// The fit could not be kept (no framebuffer objects): back to the old window, as large as the game in points (out of
// full screen first: without the fit the game draws into the window directly, at the window's size).
static void DisableFit(const char* why) {
    tpw::Log("fit: %s; the window is the game's size", why);
    tpw::offscreen::Destroy();
    if (gFit.fullScreen) {
        SDL_SetWindowFullscreen(gWindow.sdl, false);
        gFit.fullScreen = false;
    }
    gFit.wanted = false;
    SDL_SetWindowSize(gWindow.sdl, gFit.gameWide, gFit.gameHigh);
    SDL_SyncWindow(gWindow.sdl);
}

// The pixels per point of a screen. SDL's "content scale" is 1 on the maintainer's Retina screens (2026-10-13: both the LG
// UltraFine and the built-in one say 1.00) while the current display mode says 2.00, so the larger of the two counts.
static float DisplayDensity(SDL_DisplayID display) {
    float density = SDL_GetDisplayContentScale(display);
    if (const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display)) density = std::max(density, mode->pixel_density);
    return density > 0.0f ? density : 1.0f;
}

// A real full-screen window: its size, which is the whole screen's (the screen's full bounds, menu bar and dock
// included), as the window itself reports it.
static void LogFullScreen() {
    int pointsWide = 0, pointsHigh = 0, pixelsWide = 0, pixelsHigh = 0;
    SDL_GetWindowSize(gWindow.sdl, &pointsWide, &pointsHigh);
    SDL_GetWindowSizeInPixels(gWindow.sdl, &pixelsWide, &pixelsHigh);
    const SDL_DisplayID now = SDL_GetDisplayForWindow(gWindow.sdl);
    const char* name = now != 0 ? SDL_GetDisplayName(now) : nullptr;
    tpw::Log("fullscreen: %dx%d points (%dx%d pixels) on \"%s\"", pointsWide, pointsHigh, pixelsWide, pixelsHigh,
             name != nullptr ? name : "?");
}

// With the fit: sizes the window for the game's resolution and the screen it is on, and centres it there. The window's
// own frame (title bar) is taken off the screen's usable bounds (which already leave out the menu bar and the dock), and
// its pixel density is asked of the window itself once it is on the screen. Called when the window is made and whenever
// the game's resolution changes; logs what it did. A real full-screen window (or one about to be, while hidden) keeps its
// size, the whole screen: only the framebuffer (SetGameSize) and the letterbox (every frame) follow the new resolution.
static void RefitWindow() {
    SDL_Window* window = gWindow.sdl;
    if (gFit.fullScreen && (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0) {
        tpw::Log("fit: game %dx%d letterboxed into the full screen", gFit.gameWide, gFit.gameHigh);
        if (gWindow.visible) LogFullScreen();
        return;
    }
    const SDL_DisplayID display = gFit.display != 0 ? gFit.display : SDL_GetPrimaryDisplay();
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    SDL_SyncWindow(window);
    SDL_Rect usable = {0, 0, gFit.gameWide, gFit.gameHigh};
    if (!SDL_GetDisplayUsableBounds(display, &usable) || usable.w <= 0 || usable.h <= 0) {
        tpw::Log("fit: no usable bounds for the screen (%s); assuming the game's size", SDL_GetError());
        usable = {0, 0, gFit.gameWide, gFit.gameHigh};
    }
    int top = 0, left = 0, bottom = 0, right = 0;
    if (!gWindow.fullScreenBox && !SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right)) {
        top = 32;   // a title bar's worth when the system will not say
        left = bottom = right = 0;
    }
    // (the window's own density, once it is on the screen; the screen's if that has not caught up)
    const float density = std::max(SDL_GetWindowPixelDensity(window), DisplayDensity(display));
    const tpw::fit::WindowFit fit = tpw::fit::ComputeWindowFit(usable.w, usable.h, left + right, top + bottom, density,
                                                               gFit.gameWide, gFit.gameHigh);
    SDL_SetWindowSize(window, fit.pointsWide, fit.pointsHigh);
    int x = 0, y = 0;
    tpw::fit::CentreInUsable(usable.x, usable.y, usable.w, usable.h, fit.pointsWide, fit.pointsHigh, left, top, x, y);
    SDL_SetWindowPosition(window, x, y);
    SDL_SyncWindow(window);
    int pixelsWide = 0, pixelsHigh = 0;
    SDL_GetWindowSizeInPixels(window, &pixelsWide, &pixelsHigh);
    const char* name = SDL_GetDisplayName(display);
    tpw::Log("fit: game %dx%d in a %dx%d-point window (%dx%d pixels, scale %.2f) on \"%s\"", gFit.gameWide, gFit.gameHigh,
             fit.pointsWide, fit.pointsHigh, pixelsWide, pixelsHigh,
             gFit.gameWide > 0 ? static_cast<double>(pixelsWide) / gFit.gameWide : 0.0, name != nullptr ? name : "?");
}

// ---- Window Manager --------------------------------------------------------------------
// procID 2 is the plain box the game makes for full screen (at 0, 0, the size of the game's
// resolution); 0 is a document window. The window starts hidden; DDraw_Init shows it.
GrafPort* NewCWindow(void*, const Rect* bounds, const unsigned char* title, bool visible, int16_t procID, GrafPort*,
                     bool, int32_t) {
    if (gWindowMade) {
        tpw::Log("NewCWindow: the game asked for a second window; only one is supported");
        return nullptr;
    }
    if (!StartVideo()) return nullptr;
    const int32_t width = bounds->right - bounds->left;
    const int32_t height = bounds->bottom - bounds->top;
    const std::string edition = tpw::config().edition.empty() ? tpw::EditionOf(tpw::config().gameDir) : tpw::config().edition;
    const std::string name = tpw::WindowTitle(edition, title);

    // A legacy (compatibility) context: the engine draws with OpenGL 1.1 calls.
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN;
    if (procID == 2) flags |= SDL_WINDOW_BORDERLESS;
    const SDL_DisplayID chosen = ChooseDisplay();
    gFit = FitState();
    gFit.wanted = tpw::config().fit;
    int createWide = width, createHigh = height;
    if (gFit.wanted) {
        // The game's pixels, not the window's, are the game's resolution: ask for a window of the right shape for the
        // screen (RefitWindow below corrects it with what the window itself reports).
        gFit.gameWide = width;
        gFit.gameHigh = height;
        gFit.display = chosen != 0 ? chosen : SDL_GetPrimaryDisplay();
        flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE;
        SDL_Rect usable = {0, 0, width, height};
        SDL_GetDisplayUsableBounds(gFit.display, &usable);
        const tpw::fit::WindowFit guess =
            tpw::fit::ComputeWindowFit(usable.w, usable.h, 0, procID == 2 ? 0 : 32, DisplayDensity(gFit.display), width, height);
        if (guess.pointsWide > 0 && guess.pointsHigh > 0) {
            createWide = guess.pointsWide;
            createHigh = guess.pointsHigh;
        }
    }
    gWindow.sdl = SDL_CreateWindow(name.c_str(), createWide, createHigh, flags);
    if (gWindow.sdl == nullptr) {
        tpw::Log("SDL_CreateWindow failed: %s", SDL_GetError());
        gFit = FitState();
        return nullptr;
    }
    gWindow.fullScreenBox = procID == 2;
    if (gFit.wanted) {
        RefitWindow();
        PlaceOnChosenDisplay(gWindow.sdl, 0);
        // The game's full-screen box is a real full-screen window (SDL's desktop full screen, a Space of its own on the
        // chosen screen, which macOS makes when the window is shown). The fitted window above is its size outside full
        // screen (Cmd+Ctrl+F or the green button leave it).
        if (tpw::fit::RealFullScreen(gWindow.fullScreenBox, true, tpw::config().background)) {
            gFit.fullScreen = SDL_SetWindowFullscreenMode(gWindow.sdl, nullptr) && SDL_SetWindowFullscreen(gWindow.sdl, true);
            if (!gFit.fullScreen) tpw::Log("fullscreen: not available (%s); the fitted window stays", SDL_GetError());
        }
    } else {
        if (procID != 2) SDL_SetWindowPosition(gWindow.sdl, bounds->left, bounds->top);
        PlaceOnChosenDisplay(gWindow.sdl, chosen);
    }
    std::memset(gWindow.mHeader, 0, sizeof gWindow.mHeader);
    SetPortRect(width, height);
    gWindow.visible = false;
    gWindowMade = true;
    if (visible) NS_MacDoze::ShowWindow(&gWindow, 5);
    return &gWindow;
}

// The application's frontmost window: the game's one window once it is shown. As on the
// Mac, this does not depend on the application being the active one (the game hears about
// that through its suspend and resume events and pauses itself). The engine presents a
// frame only while this is its window, and the first frame it presents, the splash, is
// drawn before a new window has been given the keyboard.
GrafPort* FrontWindow() {
    return gWindowMade && gWindow.visible ? &gWindow : nullptr;
}

// Lets the system run: the engine calls it inside its waits (the splash screen), where no
// events are being processed, so the window would otherwise stop responding.
void SystemTask() {
    if (gVideoStarted) SDL_PumpEvents();
}

// ---- macdoze: Win32 window calls ---------------------------------------------------------
namespace NS_MacDoze {

int32_t ShowWindow(GrafPort* window, int32_t command) {
    if (window != &gWindow || !gWindowMade) return 0;
    const bool wasVisible = gWindow.visible;
    if (command == 0) {   // SW_HIDE
        SDL_HideWindow(gWindow.sdl);
        gWindow.visible = false;
    } else {
        SDL_ShowWindow(gWindow.sdl);
        tpw::OnGameWindowShown();   // the launcher's window closes (platform/handoff.h)
        if (tpw::config().background) SendWindowToBack();
        else SDL_RaiseWindow(gWindow.sdl);
        gWindow.visible = true;
        if (gFit.fullScreen && !wasVisible) {
            // SDL enters the full screen asked for while the window was hidden as it is shown; macOS animates the move
            // to the new Space, so the size is read once that is over (SDL_SyncWindow waits at most 2.5 seconds).
            if (!SDL_SyncWindow(gWindow.sdl)) tpw::Log("fullscreen: the move to full screen had not finished in time");
            LogFullScreen();
        }
    }
    return wasVisible ? 1 : 0;
}

int32_t UpdateWindow(GrafPort*) { return 1; }

int32_t GetClientRect(GrafPort* window, SamsUtilities::URectangle::LongRect* rect) {
    if (window != &gWindow || !gWindowMade) return 0;
    int w = 0, h = 0;
    tpw::GameSize(w, h);   // the game's resolution: with the fit the window is another size
    *rect = {0, 0, w, h};
    return 1;
}

// The window's frame is the host's business: the rectangle reported is the content's, so
// the engine's "grow the window until the client area is the display size" sum works out.
int32_t GetWindowRect(GrafPort* window, SamsUtilities::URectangle::LongRect* rect) {
    if (window != &gWindow || !gWindowMade) return 0;
    int x = 0, y = 0, w = 0, h = 0;
    SDL_GetWindowPosition(gWindow.sdl, &x, &y);
    tpw::GameSize(w, h);
    *rect = {x, y, x + w, y + h};
    return 1;
}

// With the fit the size is the game's resolution (the client area it wants, since GetWindowRect and GetClientRect
// report no frame) and the window stays where it is, re-fitted if that resolution is a new one.
int32_t MoveWindow(GrafPort* window, int32_t x, int32_t y, int32_t width, int32_t height, int32_t) {
    if (window != &gWindow || !gWindowMade) return 0;
    if (gFit.wanted) {
        tpw::SetGameSize(width, height);
        return 1;
    }
    SDL_SetWindowSize(gWindow.sdl, width, height);
    SDL_SetWindowPosition(gWindow.sdl, x, y);
    SDL_SyncWindow(gWindow.sdl);
    SetPortRect(width, height);
    return 1;
}

// macdoze 0x10000f74: SizeWindow (flag 1 clear), MoveWindow (flag 2 clear), then the order (flags 0x94 clear): 1 sends
// the window to the back, 0 to -2 (HWND_TOP, TOPMOST, NOTOPMOST) brings it to the front, any other window is swapped
// with it (no host equivalent: the game has one window). Flag 0x80 hides it. Always answers 1. The sizing and the
// position follow MoveWindow's rules (the fit re-fits to the new resolution and leaves the window where it is).
int32_t SetWindowPos(GrafPort* window, GrafPort* after, int32_t x, int32_t y, int32_t width, int32_t height,
                     uint32_t flags) {
    if (window != &gWindow || !gWindowMade) return 0;
    if ((flags & 1) == 0) {
        if (gFit.wanted) {
            tpw::SetGameSize(width, height);
        } else {
            SDL_SetWindowSize(gWindow.sdl, width, height);
            SetPortRect(width, height);
        }
    }
    if ((flags & 2) == 0 && !gFit.wanted) {
        SDL_SetWindowPosition(gWindow.sdl, x, y);
        SDL_SyncWindow(gWindow.sdl);
    }
    if ((flags & 0x94) == 0) {
        const intptr_t order = reinterpret_cast<intptr_t>(after);
        if (order == 1) {
            SendWindowToBack();
        } else if (order <= 0 && order > -3) {
            if (!tpw::config().background) SDL_RaiseWindow(gWindow.sdl);
        }
    }
    if ((flags & 0x80) != 0) {
        SDL_HideWindow(gWindow.sdl);
        gWindow.visible = false;
    }
    return 1;
}

int32_t ScreenToClient(GrafPort*, tagPOINT* point) {
    int32_t x, y;
    tpw::WindowOrigin(x, y);
    point->x -= x;
    point->y -= y;
    return 1;
}

}  // namespace NS_MacDoze

// ---- The cursor --------------------------------------------------------------------------
namespace SamsUtilities {

void UCursor::SetPosition(const UPointMac& position) {
    if (!gWindowMade) return;
    int32_t x, y;
    tpw::WindowOrigin(x, y);
    double windowX = position.x - x, windowY = position.y - y;
    if (gFit.wanted) {   // the game's pixel to the window point that shows it
        tpw::fit::GameToWindow(LetterboxInPoints(), gFit.gameWide, gFit.gameHigh, windowX, windowY, windowX, windowY);
    }
    // Only a focused window moves the pointer: an unfocused one (--background, or simply behind another) would
    // pull the mouse away from whatever the person is doing.
    if ((SDL_GetWindowFlags(gWindow.sdl) & SDL_WINDOW_INPUT_FOCUS) == 0) return;
    SDL_WarpMouseInWindow(gWindow.sdl, static_cast<float>(windowX), static_cast<float>(windowY));
}

// ---- UOpenGL -----------------------------------------------------------------------------
namespace {
// The context object the game holds. The host picks the pixel format, so the AGL attributes
// the game adds are only noted.
struct OpenGLContext : UOpenGL {
    int32_t attributeCount = 0;
};
OpenGLContext gOpenGLContext;
UOpenGL* gCurrentOpenGL = nullptr;
}  // namespace

UOpenGL* UOpenGL::GetCurrentInstance() { return gCurrentOpenGL; }

void UOpenGL::Swap() { tpw::SwapBuffers(); }

void UOpenGL::AddAttribute(int32_t, int32_t) { ++static_cast<OpenGLContext*>(this)->attributeCount; }

// How many textures of size x size the card holds in the given format. The original
// measures it by creating textures until the driver reports them non-resident, in steps,
// with a ceiling; a current card holds more than the game can use, so the ceiling the
// texture cache was written for is reported: the video memory divided by the texture's
// size at 2 bytes a texel (the cache's 16-bit formats) or 4 for GL_RGBA.
int32_t UOpenGL::GetMaxTextures(int32_t, int32_t size, int32_t format) {
    const uint32_t bytesPerTexel = format == 0x1908 ? 4 : 2;
    const uint32_t textureBytes = static_cast<uint32_t>(size) * static_cast<uint32_t>(size) * bytesPerTexel;
    return textureBytes != 0 ? static_cast<int32_t>(mVideoMemory / textureBytes) : 0;
}

}  // namespace SamsUtilities

// ---- indirectx: the OpenGL implementation --------------------------------------------------
namespace {
class OpenGLImplementation : public IdxDirectDraw {
public:
    SamsUtilities::UOpenGL* DisplayGetContext() override { return &SamsUtilities::gOpenGLContext; }
};
}  // namespace

IdxDirectDraw* IdxDirectDraw::mImplementation = nullptr;

IdxDirectDraw::~IdxDirectDraw() {}

// Makes the OpenGL context on the window. NULL when it fails.
IdxDirectDraw* IdxMakeOpenGLImplementation(SamsUtilities::DeviceKind, GrafPort* window, void*) {
    if (window != &gWindow || !gWindowMade) return nullptr;
    if (gWindow.glContext == nullptr) {
        gWindow.glContext = SDL_GL_CreateContext(gWindow.sdl);
        if (gWindow.glContext == nullptr) {
            tpw::Log("SDL_GL_CreateContext failed: %s", SDL_GetError());
            return nullptr;
        }
        SDL_GL_MakeCurrent(gWindow.sdl, static_cast<SDL_GLContext>(gWindow.glContext));
        SDL_GL_SetSwapInterval(1);
        // The game's picture goes into a framebuffer of its own size from here on (see SwapBuffers).
        if (gFit.wanted && !tpw::offscreen::Create(gFit.gameWide, gFit.gameHigh)) DisableFit("no offscreen framebuffer");
    }
    auto& context = SamsUtilities::gOpenGLContext;
    std::memset(static_cast<SamsUtilities::UOpenGL*>(&context), 0, sizeof(SamsUtilities::UOpenGL));
    // The Mac asked the driver; the game only checks for "8 MB or less" (a first run then
    // drops to low detail). 64 MB keeps the texture cache's own counts in the range the
    // game was written for.
    context.mVideoMemory = 64u * 1024 * 1024;
    SamsUtilities::gCurrentOpenGL = &context;
    static OpenGLImplementation implementation;
    IdxDirectDraw::mImplementation = &implementation;
    return &implementation;
}
