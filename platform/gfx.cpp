// gfx.cpp: the calls of gfx.h, handed to the backend. The one backend is OpenGL (platform/gl_backend.cpp), in every
// build; another renderer would be chosen here and its calls handed to it in the same way.
#include "gfx.h"

namespace tpw::gfx {

// The OpenGL backend's calls (gl_backend.cpp).
namespace gl {
bool Probe(int& largest);
uint32_t WindowFlags();
bool Create(SDL_Window* window, bool fit, int wide, int high);
void Destroy();
bool Created();
bool Fit(bool fit, int wide, int high);
bool FitReady();
bool SetSwapInterval(int interval);
void BeginPresent();
bool ReadPresent(uint8_t* rgb, int width, int height);
void EndPresent(const fit::PixelBox& box, int windowWide, int windowHigh);
const char* Description();
}  // namespace gl

int Probe() {
    int largest = 0;
    gl::Probe(largest);
    return largest;
}

// The backend a call goes to: OpenGL's, the only one.
#define TPW_GFX_CALL(call) gl::call

uint32_t WindowFlags() { return TPW_GFX_CALL(WindowFlags()); }
bool Create(SDL_Window* window, bool fit, int gameWide, int gameHigh) {
    return TPW_GFX_CALL(Create(window, fit, gameWide, gameHigh));
}
void Destroy() { TPW_GFX_CALL(Destroy()); }
bool Created() { return TPW_GFX_CALL(Created()); }
bool Resize(int gameWide, int gameHigh) { return TPW_GFX_CALL(Fit(FitReady(), gameWide, gameHigh)); }
bool Fit(bool fit, int gameWide, int gameHigh) { return TPW_GFX_CALL(Fit(fit, gameWide, gameHigh)); }
bool FitReady() { return TPW_GFX_CALL(FitReady()); }
bool SetSwapInterval(int interval) { return TPW_GFX_CALL(SetSwapInterval(interval)); }
void BeginPresent() { TPW_GFX_CALL(BeginPresent()); }
bool ReadPresent(uint8_t* rgb, int width, int height) { return TPW_GFX_CALL(ReadPresent(rgb, width, height)); }
void EndPresent(const fit::PixelBox& box, int windowWide, int windowHigh) { TPW_GFX_CALL(EndPresent(box, windowWide, windowHigh)); }
const char* Description() { return TPW_GFX_CALL(Description()); }

}  // namespace tpw::gfx
