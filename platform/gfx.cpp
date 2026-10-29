// gfx.cpp: the calls of gfx.h, handed to the backend, and the mip filter (HalfSizeRGBA). The one backend is OpenGL
// (platform/gl_backend.cpp), in every build; another renderer would be chosen here and its calls handed to it in the
// same way.
#include "gfx.h"

#include <algorithm>

namespace tpw::gfx {

std::vector<uint8_t> HalfSizeRGBA(const std::vector<uint8_t>& level, int32_t width, int32_t height, int32_t& outWide,
                                  int32_t& outHigh) {
    outWide = std::max(width / 2, 1);
    outHigh = std::max(height / 2, 1);
    std::vector<uint8_t> out(static_cast<size_t>(outWide) * static_cast<size_t>(outHigh) * 4);
    for (int32_t y = 0; y < outHigh; ++y) {
        for (int32_t x = 0; x < outWide; ++x) {
            const int32_t x0 = std::min(x * 2, width - 1), x1 = std::min(x * 2 + 1, width - 1);
            const int32_t y0 = std::min(y * 2, height - 1), y1 = std::min(y * 2 + 1, height - 1);
            for (int channel = 0; channel < 4; ++channel) {
                const uint32_t sum = level[(static_cast<size_t>(y0) * width + x0) * 4 + channel] +
                                     level[(static_cast<size_t>(y0) * width + x1) * 4 + channel] +
                                     level[(static_cast<size_t>(y1) * width + x0) * 4 + channel] +
                                     level[(static_cast<size_t>(y1) * width + x1) * 4 + channel];
                out[(static_cast<size_t>(y) * outWide + x) * 4 + channel] = static_cast<uint8_t>((sum + 2) / 4);
            }
        }
    }
    return out;
}

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
