// gfx.cpp: the choice of backend behind gfx.h, the calls handed to it, and the mip filter (HalfSizeRGBA). The OpenGL
// backend (platform/gl_backend.cpp) is every build's; the Vulkan renderer (platform/vulkan/) is the Linux build's
// (TPW_HAVE_VULKAN, CMakeLists.txt), where the player can choose it, with OpenGL the fallback when no Vulkan device can
// draw to the window. A Mac and Windows have OpenGL only, and their calls go to it as before.
#include "gfx.h"

#include <algorithm>
#include <cctype>
#include <string>

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
#ifdef TPW_HAVE_VULKAN
const char* ProbedRenderer();   // the probe's GL_RENDERER ("" before a probe that made a context)
#endif
}  // namespace gl

namespace {
// The backend drawn with when none is asked for: OpenGL on every system, the API the original draws with (the
// maintainer, 2026-10-28: the Settings box's Renderer row is off by default, and off is the original). On Linux the
// port's own Vulkan renderer is the player's to choose (--renderer, launcher.ini's renderer key, which the Renderer row
// writes). With Vulkan as this default, a Vulkan whose only device draws in software would give way to a hardware
// OpenGL (OpenGLForSoftwareVulkan); one asked for draws in software.
constexpr Api kDefault = Api::OpenGL;
Api gRequested = kDefault;
Api gChosen = gRequested;
bool gProbed = false;
int gLargest = 0;

#ifdef TPW_HAVE_VULKAN
bool gAsked = false;   // Request was called (--renderer, launcher.ini's renderer key): not just the build's default

// Whether an OpenGL driver's GL_RENDERER names a renderer that draws on the processor: Mesa's llvmpipe (also inside
// what zink on lavapipe reports), softpipe and the old swrast ("Software Rasterizer"), SwiftShader (as ANGLE or
// SwiftShader's own GL names it) and Microsoft's WARP under WSL's d3d12 driver ("Microsoft Basic Render Driver").
bool SoftwareOpenGL(std::string renderer) {
    for (char& c : renderer) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* name : {"llvmpipe", "softpipe", "software rasterizer", "swiftshader", "basic render driver"})
        if (renderer.find(name) != std::string::npos) return true;
    return false;
}

// Vulkan's only device draws in software (Mesa's lavapipe, SwiftShader: vkr::ProbedSoftware). Unless Vulkan was asked
// for, the system's OpenGL is tried too, and draws instead when its renderer is hardware: a GPU without a Vulkan driver
// often has an OpenGL one (an older card, a driver without Vulkan), and the game would otherwise be drawn on the
// processor. When OpenGL is software too, or cannot be made, Vulkan's software device draws. True when OpenGL takes over.
bool OpenGLForSoftwareVulkan() {
    const char* device = vkr::ProbedDevice();
    if (gAsked) {
        tpw::Log("vulkan: only a software device (%s); Vulkan was asked for, so the game is drawn in software", device);
        return false;
    }
    gChosen = Api::OpenGL;   // the probe's own OpenGL calls go to the system's OpenGL (platform/gl_entry.cpp)
    int largest = 0;
    const bool made = gl::Probe(largest);
    const std::string renderer = made ? gl::ProbedRenderer() : "";
    if (made && !renderer.empty() && !SoftwareOpenGL(renderer)) {
        tpw::Log("vulkan: only a software device (%s); the system's OpenGL is hardware (%s), so OpenGL draws", device,
                 renderer.c_str());
        gLargest = largest;
        return true;
    }
    if (!made)
        tpw::Log("vulkan: only a software device (%s); the system's OpenGL cannot be made, so Vulkan draws", device);
    else if (renderer.empty())
        tpw::Log("vulkan: only a software device (%s); the system's OpenGL names no renderer, so Vulkan draws", device);
    else
        tpw::Log("vulkan: only a software device (%s); the system's OpenGL is software too (%s), so Vulkan draws", device,
                 renderer.c_str());
    gChosen = Api::Vulkan;
    return false;
}
#endif
}  // namespace

void Request(Api api) {
    gRequested = api;   // as asked, which the launcher's summary line names
    gChosen = Available(api) ? api : Api::OpenGL;
    if (gChosen != api) tpw::Log("renderer: %s is not in this build; %s", Name(api), Name(gChosen));
#ifdef TPW_HAVE_VULKAN
    gAsked = true;
#endif
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
            if (vkr::ProbedSoftware() && OpenGLForSoftwareVulkan()) gChosen = Api::OpenGL;
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
