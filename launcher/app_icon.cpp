// app_icon.cpp: the launcher's Dock and taskbar icon (app_icon.h). The stand-in is drawn in app_icon_standin.cpp.
#include "app_icon.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "app_bundle.h"
#include "app_icon_standin.h"
#include "game_app.h"
#include "icon.h"
#include "installed_icon.h"

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

// The icon to show: `dataFolder`'s icon.png (own: true, `source` its Source text) or the stand-in.
IconImage LoadIcon(const std::string& dataFolder, bool& own, std::string& source) {
    IconImage icon;
    const std::string path = dataFolder + "/icon.png";
    own = !dataFolder.empty() && tpw::assets::ReadIconPng(path, icon, &source);
    if (!own) icon = StandInAppIcon();
    return icon;
}

}  // namespace

IconImage LoadAppIcon(const std::string& dataFolder, bool& own, std::string& source) { return LoadIcon(dataFolder, own, source); }

void ApplyAppIcon(const std::string& dataFolder, SDL_Window* window) {
    bool own = false;
    std::string source;
    IconImage icon = LoadIcon(dataFolder, own, source);
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

void RefreshInstalledIcon(const std::string& dataFolder) {
#if defined(__APPLE__) || defined(_WIN32)
    std::string target;
#if defined(__APPLE__)
    // the launcher's build folder: the one holding its bundle (build/STP:TPW Launcher.app), or its program
    const std::string exe = ExecutablePath();
    const std::string bundle = BundleOf(exe);
    const std::string build = exe.empty() ? "" : std::filesystem::path(bundle.empty() ? exe : bundle).parent_path().string();
    std::vector<std::string> folders;   // where install.sh puts the application (apps_folder)
    const char* apps = std::getenv("TPW_INSTALL_APPS");
    const char* home = std::getenv("HOME");
    if (apps != nullptr && *apps != 0) folders.push_back(apps);
    else folders = {"/Applications", home != nullptr && *home != 0 ? std::string(home) + "/Applications" : ""};
    target = InstallerApp(folders, build);
    bool own = false;
    std::string source, why;
    const IconImage icon = LoadIcon(dataFolder, own, source);
    // the launcher's own bundle beside the program (app_bundle.h KeepLauncherBundle), installed or not
    if (!build.empty()) {
        const std::string app = (std::filesystem::path(build) / kLauncherBundleName).string();
        const IconRefresh kept = KeepBundleIcon(app, icon, why);
        if (kept == IconRefresh::Written) tpw::Log("app bundle: %s now shows %s", app.c_str(), own ? "the game's icon" : "the launcher's stand-in");
        else if (kept == IconRefresh::Failed) tpw::Log("app bundle: %s's icon not written (%s)", app.c_str(), why.c_str());
    }
    if (target.empty()) return;   // not installed (a build run where it was built): no installer's application
    const IconRefresh done = RefreshAppIcns(target, EncodeIcns(icon), why);
#else
    target = ShortcutIconFile(ProgramFolder());
    if (target.empty()) return;   // not installed (a build run where it was built): nothing to keep up to date
    bool own = false;
    std::string source, why;
    const IconImage icon = LoadIcon(dataFolder, own, source);
    const IconRefresh done = KeepIconFile(target, tpw::assets::EncodeIco(icon), why);
    if (done == IconRefresh::Written) TellExplorer(target);
#endif
    const char* what = own ? "the game's icon" : "the launcher's stand-in";
    if (done == IconRefresh::Written) tpw::Log("installed icon: %s now shows %s", target.c_str(), what);
    else if (done == IconRefresh::Failed) tpw::Log("installed icon: %s not written (%s)", target.c_str(), why.c_str());
#else
    (void)dataFolder;
#endif
}

bool WriteAppIcon(const std::string& dataFolder, const std::string& path, std::string& what, std::string& err) {
    bool own = false;
    std::string source;
    const IconImage icon = LoadIcon(dataFolder, own, source);
    what = own ? "the game's icon, from " + source : "the launcher's stand-in (no icon.png in the data folder yet)";
    // icon.png's Source text is "TP.ico, 64x64, 8-bit": the name alone goes in, the writer adds the size and depth again
    std::string name = own ? source : "the launcher's stand-in";
    for (int part = 0; own && part < 2; ++part) {
        const size_t comma = name.rfind(", ");
        if (comma == std::string::npos) break;
        name.erase(comma);
    }
    // a .ico (install.ps1's shortcut icon) or a PNG (install.sh's application): by the name, on every platform
    std::string ext = path.size() >= 4 ? path.substr(path.size() - 4) : "";
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".ico") return tpw::assets::WriteIconIco(path, icon, err);
    return tpw::assets::WriteIconPng(path, icon, name, err);
}

}  // namespace tpw::launcher
