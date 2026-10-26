// app_icon.cpp: the launcher's Dock and taskbar icon (app_icon.h).
#include "app_icon.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "icon.h"
#include "ui/theme.h"

// platform.h is not included: it brings the game's own headers, whose Rect and DebugStr clash with Apple's MacTypes.h
// (platform/gdi_text.cpp keeps clear of it the same way). The one thing wanted from it is the log line.
namespace tpw {
void Log(const char* format, ...) __attribute__((format(printf, 1, 2)));
}

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <dlfcn.h>
#include <objc/message.h>
#include <objc/runtime.h>
#endif

namespace tpw::launcher {

namespace {

using tpw::assets::IconImage;
using tpw::ui::Rgba;
namespace colour = tpw::ui::colour;

// The stand-in, 256 x 256: a rounded tile with the launcher's sky, a lime lawn, a sun and a cloud, outlined in ink, as the
// launcher window's panels are. Drawn with distance functions, so its edges are soft at any size.
IconImage StandIn() {
    constexpr int n = 256;
    IconImage out;
    out.width = out.height = n;
    out.bitDepth = 32;
    out.rgba.assign(static_cast<size_t>(n) * n * 4, 0);
    auto roundedBox = [](float x, float y, float half, float radius) {   // signed distance from the tile's edge, centred
        const float dx = std::fabs(x) - (half - radius), dy = std::fabs(y) - (half - radius);
        const float ox = std::max(dx, 0.0f), oy = std::max(dy, 0.0f);
        return std::sqrt(ox * ox + oy * oy) + std::min(std::max(dx, dy), 0.0f) - radius;
    };
    auto coverage = [](float distance) { return std::min(1.0f, std::max(0.0f, 0.5f - distance)); };   // 1 inside, 0 outside
    auto mix = [](Rgba under, Rgba over, float amount) {
        auto lerp = [amount](uint8_t a, uint8_t b) { return static_cast<uint8_t>(a + (b - a) * amount + 0.5f); };
        return Rgba{lerp(under.r, over.r), lerp(under.g, over.g), lerp(under.b, over.b), under.a};
    };
    const float half = 116, corner = 40, outline = 9;   // the tile's half size, corner radius and outline, in pixels
    for (int py = 0; py < n; ++py) {
        for (int px = 0; px < n; ++px) {
            const float x = px + 0.5f - n / 2.0f, y = py + 0.5f - n / 2.0f;
            const float edge = roundedBox(x, y, half, corner);
            const float alpha = coverage(edge);
            if (alpha <= 0) continue;
            // the sky, top to bottom
            const float t = std::min(1.0f, std::max(0.0f, (y + half) / (2 * half)));
            Rgba c = mix(colour::kSkyTop, colour::kSkyBottom, t);
            // the sun, top right, with its deeper rim
            const float sun = std::sqrt((x - 46) * (x - 46) + (y + 50) * (y + 50));
            c = mix(c, colour::kOrange, coverage(sun - 38));
            c = mix(c, colour::kYellow, coverage(sun - 30));
            // a cloud, left: three discs
            float cloud = std::min(std::sqrt((x + 52) * (x + 52) + (y + 6) * (y + 6)) - 22,
                                   std::min(std::sqrt((x + 24) * (x + 24) + (y + 16) * (y + 16)) - 26,
                                            std::sqrt((x + 2) * (x + 2) + (y + 4) * (y + 4)) - 20));
            c = mix(c, colour::kPaper, coverage(cloud) * 0.92f);
            // the lawn, rolling, with its deeper edge
            const float ground = y - (44 + 10 * std::sin((x + 60) / 48.0f));
            c = mix(c, colour::kLimeDeep, coverage(-ground - 0));
            c = mix(c, colour::kLime, coverage(-ground + 7));
            // the ink outline all round
            c = mix(c, colour::kInk, coverage(-edge - outline));
            uint8_t* p = &out.rgba[(static_cast<size_t>(py) * n + px) * 4];
            p[0] = c.r, p[1] = c.g, p[2] = c.b, p[3] = static_cast<uint8_t>(alpha * 255 + 0.5f);
        }
    }
    return out;
}

#if defined(__APPLE__)
// [NSApp setApplicationIconImage:] with the pixels as a CGImage (straight alpha, as the PNG has it).
bool SetDockIcon(const IconImage& icon, const char** reason) {
    void** appVar = reinterpret_cast<void**>(dlsym(RTLD_DEFAULT, "NSApp"));
    void* app = appVar != nullptr ? *appVar : nullptr;
    if (app == nullptr) {
        *reason = "no application yet";
        return false;
    }
    CFDataRef data = CFDataCreate(nullptr, icon.rgba.data(), static_cast<CFIndex>(icon.rgba.size()));
    CGDataProviderRef provider = data != nullptr ? CGDataProviderCreateWithCFData(data) : nullptr;
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGImageRef cgImage = provider != nullptr && space != nullptr
                             ? CGImageCreate(static_cast<size_t>(icon.width), static_cast<size_t>(icon.height), 8, 32,
                                             static_cast<size_t>(icon.width) * 4, space,
                                             kCGImageAlphaLast | kCGBitmapByteOrderDefault, provider, nullptr, true,
                                             kCGRenderingIntentDefault)
                             : nullptr;
    if (space != nullptr) CGColorSpaceRelease(space);
    if (provider != nullptr) CGDataProviderRelease(provider);
    if (data != nullptr) CFRelease(data);
    if (cgImage == nullptr) {
        *reason = "no CGImage for the pixels";
        return false;
    }
    using SendId = void* (*)(void*, SEL);
    using SendInit = void* (*)(void*, SEL, CGImageRef, CGSize);
    using SendSet = void (*)(void*, SEL, void*);
    void* imageClass = reinterpret_cast<void*>(objc_getClass("NSImage"));
    void* image = imageClass != nullptr ? reinterpret_cast<SendId>(objc_msgSend)(imageClass, sel_registerName("alloc")) : nullptr;
    // the point size: the Dock scales it; 0 x 0 would take the pixel size, which is what we want for a 32 or 64 pixel original
    if (image != nullptr)
        image = reinterpret_cast<SendInit>(objc_msgSend)(image, sel_registerName("initWithCGImage:size:"), cgImage, CGSizeMake(0, 0));
    CGImageRelease(cgImage);
    if (image == nullptr) {
        *reason = "no NSImage";
        return false;
    }
    reinterpret_cast<SendSet>(objc_msgSend)(app, sel_registerName("setApplicationIconImage:"), image);
    reinterpret_cast<SendId>(objc_msgSend)(image, sel_registerName("release"));
    return true;
}
#endif

}  // namespace

void ApplyAppIcon(const std::string& dataFolder, SDL_Window* window) {
    IconImage icon;
    std::string source;
    const std::string path = dataFolder + "/icon.png";
    const bool own = !dataFolder.empty() && tpw::assets::ReadIconPng(path, icon, &source);
    if (!own) icon = StandIn();
    const char* what = own ? "the game's icon" : "the launcher's stand-in (no icon.png in the data folder yet)";
#if defined(__APPLE__)
    (void)window;
    const char* reason = nullptr;
    if (SetDockIcon(icon, &reason)) tpw::Log("app icon: %s%s%s (%dx%d)", what, own ? ", from " : "", own ? source.c_str() : "", icon.width, icon.height);
    else tpw::Log("app icon: not set (%s)", reason != nullptr ? reason : "");
#else
    if (window == nullptr) {
        tpw::Log("app icon: not set (no window)");
        return;
    }
    SDL_Surface* surface = SDL_CreateSurfaceFrom(icon.width, icon.height, SDL_PIXELFORMAT_RGBA32, icon.rgba.data(), icon.width * 4);
    if (surface == nullptr || !SDL_SetWindowIcon(window, surface)) tpw::Log("app icon: not set (%s)", SDL_GetError());
    else tpw::Log("app icon: %s%s%s (%dx%d)", what, own ? ", from " : "", own ? source.c_str() : "", icon.width, icon.height);
    if (surface != nullptr) SDL_DestroySurface(surface);
#endif
}

}  // namespace tpw::launcher
