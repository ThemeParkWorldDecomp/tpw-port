// gfx.cpp: the choice of backend behind gfx.h, the calls handed to it, and the mip filter (HalfSizeRGBA). The OpenGL
// backend (platform/gl_backend.cpp) is every build's; the Vulkan renderer (platform/vulkan/) is the Linux build's
// (TPW_HAVE_VULKAN, CMakeLists.txt), where it is the default and OpenGL the fallback when no Vulkan device can draw to
// the window. A Mac and Windows have OpenGL only, and their calls go to it as before.
#include "gfx.h"

#include <algorithm>

#include "platform.h"
#ifdef TPW_HAVE_VULKAN
#include "vulkan/renderer.h"
#endif

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

namespace {
#ifdef TPW_HAVE_VULKAN
Api gRequested = Api::Vulkan;
#else
Api gRequested = Api::OpenGL;
#endif
Api gChosen = gRequested;
bool gProbed = false;
int gLargest = 0;
}  // namespace

void Request(Api api) {
    if (!Available(api)) {
        tpw::Log("renderer: %s is not in this build; %s", Name(api), Name(Api::OpenGL));
        api = Api::OpenGL;
    }
    gRequested = api;
    gChosen = api;
    gProbed = false;
}

Api Requested() { return gRequested; }

Api Chosen() { return gChosen; }

const char* Name(Api api) { return api == Api::Vulkan ? "Vulkan" : "OpenGL"; }

bool Available(Api api) {
#ifdef TPW_HAVE_VULKAN
    (void)api;
    return true;
#else
    return api == Api::OpenGL;
#endif
}

int Probe() {
    if (gProbed) return gLargest;
    gProbed = true;
    gLargest = 0;
#ifdef TPW_HAVE_VULKAN
    if (gRequested == Api::Vulkan) {
        if (vkr::Probe(gLargest)) {
            gChosen = Api::Vulkan;
            return gLargest;
        }
        tpw::Log("renderer: Vulkan cannot draw here; OpenGL instead");
        gChosen = Api::OpenGL;
    }
#endif
    gl::Probe(gLargest);
    return gLargest;
}

// The backend a call goes to: the chosen one on Linux; OpenGL's, the only one, elsewhere.
#ifdef TPW_HAVE_VULKAN
#define TPW_GFX_CALL(call) (gChosen == Api::Vulkan ? vkr::call : gl::call)
#else
#define TPW_GFX_CALL(call) gl::call
#endif

// With two backends the window is made for the one that works, so the choice is settled first. Probe has run already
// when the launcher asked for the largest frame (tpw::LargestGameSize), but not when TPW_LARGEST_GAME_SIZE stood in for it.
uint32_t WindowFlags() {
#ifdef TPW_HAVE_VULKAN
    if (!gProbed) Probe();
#endif
    return TPW_GFX_CALL(WindowFlags());
}
bool Create(SDL_Window* window, bool fit, int gameWide, int gameHigh) {
#ifdef TPW_HAVE_VULKAN
    if (!gProbed) Probe();
#endif
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
