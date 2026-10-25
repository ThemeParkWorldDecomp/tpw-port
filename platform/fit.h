// fit.h: the arithmetic of "fit the window to the screen, render at the game's resolution". Header only and free of
// SDL and OpenGL so platform/tests can check it. The game draws into an offscreen framebuffer of exactly its own
// resolution (platform/offscreen.cpp); the window is made as large as fits the screen, with the game's aspect
// ratio, and the framebuffer is scaled into it with black bars where the aspect ratios differ (a letterbox).
#pragma once
#include <algorithm>
#include <cmath>

namespace tpw::fit {

// A rectangle in the unit of the caller (window points or window pixels).
struct Rect {
    double left = 0.0;
    double top = 0.0;
    double width = 0.0;
    double height = 0.0;
};

// The window to create, in points (the content area, without the title bar), and how much it scales the
// game: scale is window pixels per game pixel (1.0: no scaling at all; above 1 the picture is enlarged).
struct WindowFit {
    int pointsWide = 0;
    int pointsHigh = 0;
    double scale = 1.0;
};

// What the screen leaves for the window's content: the display's usable bounds (which exclude the menu bar and
// the dock) less the window's own frame (title bar and borders), all in points. density is the pixels per
// point of the screen (2 on a Retina display). The result is the largest window of the game's aspect ratio that
// fits: it fills the usable height (or width) of the screen whatever the game's resolution, so the game's
// picture is scaled up or down to it (the maintainer, 2026-10-13: the window fills the MacBook's height, with space left
// and right; the game still renders at its own resolution and is enlarged into the window).
inline WindowFit ComputeWindowFit(double usableWide, double usableHigh, double frameWide, double frameHigh,
                                  double density, int gameWide, int gameHigh) {
    WindowFit fit;
    if (gameWide <= 0 || gameHigh <= 0) return fit;
    if (!(density > 0.0)) density = 1.0;
    const double maxWide = gameWide / density;
    const double maxHigh = gameHigh / density;
    const double availWide = std::max(usableWide - frameWide, 1.0);
    const double availHigh = std::max(usableHigh - frameHigh, 1.0);
    const double factor = std::min(availWide / maxWide, availHigh / maxHigh);
    fit.pointsWide = std::max(static_cast<int>(std::floor(maxWide * factor + 1e-9)), 1);
    fit.pointsHigh = std::max(static_cast<int>(std::floor(maxHigh * factor + 1e-9)), 1);
    fit.scale = fit.pointsWide * density / gameWide;
    return fit;
}

// Whether the game's window becomes a real full-screen window (a Space of its own, the menu bar and the dock hidden, as
// the green button or Cmd+Ctrl+F make it): only the game's full-screen box (procID 2; --windowed asks for a document
// window instead), only through the fit (without it the game draws into the window directly, at the window's size), and
// never with --background (a full-screen Space cannot sit behind the other windows). The window is then the whole
// screen and the game's picture is letterboxed into it.
inline bool RealFullScreen(bool fullScreenBox, bool fitted, bool background) {
    return fullScreenBox && fitted && !background;
}

// Whether a key press is the game's full-screen toggle, Alt+Enter (the Windows convention; Linux too): an Enter key
// with an Alt key held and no other modifier, a first press, not a repeat. Never on a Mac, where Cmd+Ctrl+F and the
// green button do it and Option+Return stays the game's. The press is the host's: it never reaches the game.
inline bool FullScreenToggleKey(bool apple, bool enterKey, bool altHeld, bool otherModifierHeld, bool repeat) {
    return !apple && enterKey && altHeld && !otherModifierHeld && !repeat;
}

// Whether the toggle can act at all: only through the fit (the game draws into its offscreen framebuffer and the
// window shows it scaled, whatever the window's size) and never with --background (as RealFullScreen). The window need
// not be the game's full-screen box: --windowed's document window goes full screen the same way.
inline bool CanToggleFullScreen(bool fitted, bool background) {
    return fitted && !background;
}

// The largest rectangle of the game's aspect ratio inside a window of the given size, centred in it.
inline Rect Letterbox(double windowWide, double windowHigh, int gameWide, int gameHigh) {
    Rect box;
    if (gameWide <= 0 || gameHigh <= 0 || windowWide <= 0.0 || windowHigh <= 0.0) return box;
    const double scale = std::min(windowWide / gameWide, windowHigh / gameHigh);
    box.width = gameWide * scale;
    box.height = gameHigh * scale;
    box.left = (windowWide - box.width) * 0.5;
    box.top = (windowHigh - box.height) * 0.5;
    return box;
}

// A pixel of a letterbox on the screen: the rectangle rounded to whole pixels, edges first, so the box neither
// leaves a gap nor overlaps itself.
struct PixelBox {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};
inline PixelBox RoundBox(const Rect& box) {
    PixelBox out;
    out.left = static_cast<int>(std::lround(box.left));
    out.top = static_cast<int>(std::lround(box.top));
    out.right = static_cast<int>(std::lround(box.left + box.width));
    out.bottom = static_cast<int>(std::lround(box.top + box.height));
    return out;
}

// A window position (points, from the window's top left) as a game pixel, not clamped: outside the box it is
// negative or past the game's size.
inline void WindowToGame(const Rect& box, int gameWide, int gameHigh, double x, double y, double& gameX, double& gameY) {
    gameX = box.width > 0.0 ? (x - box.left) * gameWide / box.width : 0.0;
    gameY = box.height > 0.0 ? (y - box.top) * gameHigh / box.height : 0.0;
}

// The other way: a game pixel as a window position (what a cursor warp needs).
inline void GameToWindow(const Rect& box, int gameWide, int gameHigh, double gameX, double gameY, double& x, double& y) {
    x = gameWide > 0 ? box.left + gameX * box.width / gameWide : 0.0;
    y = gameHigh > 0 ? box.top + gameY * box.height / gameHigh : 0.0;
}

// A game coordinate held inside the game's pixels, 0 to size-1 (so that whole pixels from it are in range): the bars
// never give the game an out-of-range position.
inline double ClampToGame(double value, int size) {
    if (size <= 0) return 0.0;
    if (!(value > 0.0)) return 0.0;   // also NaN
    return std::min(value, static_cast<double>(size - 1));
}

// The top left of a window of the given size (its content area, with a frame above it of frameHigh points)
// centred in the usable bounds of a display.
inline void CentreInUsable(double usableLeft, double usableTop, double usableWide, double usableHigh, double wide,
                           double high, double frameLeft, double frameHigh, int& left, int& top) {
    left = static_cast<int>(std::lround(usableLeft + (usableWide - wide - frameLeft) * 0.5 + frameLeft));
    top = static_cast<int>(std::lround(usableTop + (usableHigh - high - frameHigh) * 0.5 + frameHigh));
}

}  // namespace tpw::fit
