// gfx.h: the graphics backend behind the game's window (platform/window.cpp): what makes the device on the window, keeps
// the picture the game draws (at its own resolution, platform/fit.h) and shows it. Two backends: OpenGL (platform/gl_backend.cpp
// with platform/offscreen.cpp; every system) and, on Linux, the port's own Vulkan renderer (platform/vulkan/). The game
// itself draws with OpenGL 1.1 calls (include/platform/opengl.h) whichever backend is in use: on Linux those calls are the
// platform layer's own (platform/gl_entry.cpp) and go to the Vulkan renderer or to the system's OpenGL.
//
// The frame, as window.cpp drives it: the game draws into its target; at the swap BeginPresent copies the game's picture
// into the present target (which the OpenGL calls then draw into: the cursor overlay goes on the copy, so the game's own
// picture never carries it), ReadPresent gives the frame for the dump, and EndPresent scales the present target into the
// window's box and shows it, after which the game's target is drawn into again.
#pragma once
#include <cstdint>
#include <vector>

#include "fit.h"

struct SDL_Window;

namespace tpw::gfx {

// The next level of a mip chain of RGBA8 pixels: each pixel the mean of the 2 x 2 under it (a row or column that has no
// pair is used alone), which is GLU's box filter for the powers of two the game's pages are. The one filter of both
// renderers (gluBuild2DMipmaps on the system's OpenGL, platform/gl_entry.cpp, and the Vulkan renderer's own).
std::vector<uint8_t> HalfSizeRGBA(const std::vector<uint8_t>& level, int32_t width, int32_t height, int32_t& outWide, int32_t& outHigh);

enum class Api { OpenGL, Vulkan };

// The backend the launcher asks for (--renderer; the default is Vulkan on Linux and OpenGL elsewhere), before the window
// and before Probe. One this build has not is OpenGL, and the log says so.
void Request(Api api);
Api Requested();
// The backend in use, as Probe settled it: a Vulkan that is not there falls back to OpenGL, and the log says so.
Api Chosen();
const char* Name(Api api);
// Whether this build has the backend at all (Vulkan: Linux).
bool Available(Api api);

// Before the game's window exists: tries the chosen backend on a hidden window, falls back when it fails, and answers the
// largest width or height a frame can have (0 when it cannot be asked). Video must be started. Called once; later calls
// answer what the first found.
int Probe();

// What the game's window is made with: SDL_WINDOW_OPENGL or SDL_WINDOW_VULKAN, after the backend's own attributes are set.
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

// The OpenGL backend made a context current: on Linux platform/gl_entry.cpp takes the system's OpenGL calls then.
void OnGLContextMade();

}  // namespace tpw::gfx
