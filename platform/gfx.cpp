// gfx.cpp: the choice of backend behind gfx.h, and the calls handed to it. The OpenGL backend (platform/gl_backend.cpp)
// is every build's; the Vulkan renderer (platform/vulkan/) is the Linux build's (TPW_HAVE_VULKAN, CMakeLists.txt), where it
// is the default and OpenGL the fallback when no Vulkan device can draw to the window.
#include "gfx.h"

#include <cstring>

#include "platform.h"
#ifdef TPW_HAVE_VULKAN
#include "vulkan/renderer.h"
#endif

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

#ifdef TPW_HAVE_VULKAN
#define TPW_GFX_CALL(call) (gChosen == Api::Vulkan ? vkr::call : gl::call)
#else
#define TPW_GFX_CALL(call) gl::call
#endif

uint32_t WindowFlags() {
    if (!gProbed) Probe();
    return TPW_GFX_CALL(WindowFlags());
}
bool Create(SDL_Window* window, bool fit, int gameWide, int gameHigh) {
    if (!gProbed) Probe();
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
