// gfx.h: the graphics backend behind the game's window (platform/window.cpp): what makes the device on the window,
// keeps the picture the game draws (at its own resolution, platform/fit.h) and shows it. The backend is OpenGL
// (platform/gl_backend.cpp with platform/offscreen.cpp): the context SDL makes on the window, the offscreen
// framebuffers when the window is fitted, and the swap. The game itself draws with OpenGL 1.1 calls
// (include/platform/opengl.h). The window asks this interface for all of it rather than calling OpenGL itself, so
// that another renderer can stand behind the interface in OpenGL's place.
//
// The frame, as window.cpp drives it: the game draws into its target; at the swap BeginPresent copies the game's picture
// into the present target (which the OpenGL calls then draw into: the cursor overlay goes on the copy, so the game's own
// picture never carries it), ReadPresent gives the frame for the dump, and EndPresent scales the present target into the
// window's box and shows it, after which the game's target is drawn into again.
#pragma once
#include <cstdint>

#include "fit.h"

struct SDL_Window;

namespace tpw::gfx {

// Before the game's window exists: tries the backend on a hidden window and answers the largest width or height a frame
// can have (0 when it cannot be asked). Video must be started. Called once; later calls answer what the first found.
int Probe();

// What the game's window is made with (OpenGL: SDL_WINDOW_OPENGL), after the backend's own attributes are set.
uint32_t WindowFlags();

// Makes the device (context) on the window. With `fit` the game draws into a target of gameWide x gameHigh pixels, else
// into one of the window's own size (OpenGL: the window's buffer itself). False when it cannot be made.
bool Create(SDL_Window* window, bool fit, int gameWide, int gameHigh);
void Destroy();
bool Created();
// The game's resolution changed (SetGameSize): the targets follow it, with the fit kept as it is. False when they cannot
// be made at that size (the fit is then dropped by window.cpp). Without the fit: the window was resized to the game's size.
bool Resize(int gameWide, int gameHigh);
// The fit put on or off at run time (DisableFit): with it the game's own size, without it the window's.
bool Fit(bool fit, int gameWide, int gameHigh);
// Whether the game draws into a target of its own size (the fit is on and its targets exist).
bool FitReady();

// The swap waits for this many of the screen's refreshes (platform/window.cpp MatchSwapIntervalToRefresh). False when the
// backend refused it (OpenGL: the driver); the caller then asks for 1.
bool SetSwapInterval(int interval);

// The frame (see above). ReadPresent: the present target as RGB bytes, bottom row first, width x height x 3 (the target's
// size: GameSize with the fit, the window's pixels without); false when it cannot be read.
void BeginPresent();
bool ReadPresent(uint8_t* rgb, int width, int height);
// box: where the picture goes in the window (window pixels from the top left); the rest is black.
void EndPresent(const fit::PixelBox& box, int windowWide, int windowHigh);

// A line for the log once the device exists: the API, the device and its driver.
const char* Description();

}  // namespace tpw::gfx
