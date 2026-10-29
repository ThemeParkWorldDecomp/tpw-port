// app_bundle.cpp: the launcher's application bundle on macOS (app_bundle.h).
#include "app_bundle.h"

#include <SDL3/SDL.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(__APPLE__) || defined(_WIN32)
#include "app_icon.h"
#include "game_app.h"

extern "C" void* objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void* pool);
#endif

namespace tpw::launcher {

namespace {

const char* const kNoteVariable = "TPW_APP_BUNDLE_NOTE";

#if defined(__APPLE__) || defined(_WIN32)
// The runs that open no window (--write-icon included: the install script runs it before the launcher's first start):
// Cmd-Tab and the Dock never see them, so they stay where they are.
bool WindowlessRun(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--version") == 0 || std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0 ||
            std::strcmp(arg, "--check-updates") == 0 || std::strcmp(arg, "--import") == 0 ||
            std::strcmp(arg, "--verify") == 0 || std::strcmp(arg, "--write-icon") == 0)
            return true;
    }
    return false;
}

bool HasPlay(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--play") == 0) return true;
    return false;
}
#endif

#if defined(__APPLE__)
namespace fs = std::filesystem;

std::string ReadWhole(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

// Writes `text` to `path` when the file does not hold it already (through a .part file and a rename).
bool KeepFile(const fs::path& path, const std::string& text, std::string& why) {
    std::error_code ec;
    if (fs::is_regular_file(path, ec) && ReadWhole(path) == text) return true;
    const fs::path part = path.string() + ".part";
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        if (!out || !(out << text) || !out.flush()) {
            why = "cannot write " + part.string() + ": " + std::strerror(errno);
            return false;
        }
    }
    fs::rename(part, path, ec);
    if (ec) {
        why = "cannot rename " + part.string() + ": " + ec.message();
        fs::remove(part, ec);
        return false;
    }
    return true;
}

// Whether the two paths are the same file (a hard link of the other).
bool SameFile(const fs::path& a, const fs::path& b) {
    struct stat sa{}, sb{};
    return stat(a.c_str(), &sa) == 0 && stat(b.c_str(), &sb) == 0 && S_ISREG(sb.st_mode) && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

// Runs the program at `exe` from the bundle `app` (made or refreshed with `plist` and `dataFolder`'s icon) in this process, with `argv`'s
// arguments: returns only when it could not.
bool ExecFromBundle(const fs::path& exe, const fs::path& app, const std::string& plist, const std::string& dataFolder, int argc, char** argv,
                    const char* dockName, std::string& note) {
    std::string program, made, why;
    if (!KeepLauncherBundle(exe.string(), app.string(), plist, dataFolder, program, made, why)) {
        note = "not from the bundle (" + why + "); the Dock names this run after the program's folder";
        return false;
    }
    std::vector<char*> args;
    args.push_back(program.data());
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
    args.push_back(nullptr);
    const std::string left = "running from " + app.string() + " (" + made + "; the Dock names it " + dockName + ")";
    setenv(kNoteVariable, left.c_str(), 1);
    execv(program.c_str(), args.data());
    unsetenv(kNoteVariable);
    note = "not from the bundle: cannot run " + program + ": " + std::strerror(errno);
    return false;
}

// The application macOS opened this process as: [[NSRunningApplication currentApplication] bundleURL], asked only when
// macOS's launch said it opened one with the launcher's identifier (__CFBundleIdentifier, which LaunchServices sets for
// every application it opens; a Terminal's shell has the Terminal's). Empty otherwise, and for a program started from a
// shell, whose bundleURL is the program itself.
std::string OpenedAsApp(const std::string& plist) {
    const char* opened = std::getenv("__CFBundleIdentifier");
    if (opened == nullptr || PlistString(plist, "CFBundleIdentifier") != opened) return "";
    using Id = void*;
    using SendId = Id (*)(Id, SEL);
    const auto send = [](Id to, const char* selector) {
        return to != nullptr ? reinterpret_cast<SendId>(objc_msgSend)(to, sel_registerName(selector)) : nullptr;
    };
    void* pool = objc_autoreleasePoolPush();
    Id path = send(send(send(reinterpret_cast<Id>(objc_getClass("NSRunningApplication")), "currentApplication"), "bundleURL"), "path");
    const char* text = path != nullptr ? reinterpret_cast<const char* (*)(Id, SEL)>(objc_msgSend)(path, sel_registerName("UTF8String")) : nullptr;
    std::string app = text != nullptr ? text : "";
    objc_autoreleasePoolPop(pool);
    return app.size() > 4 && app.compare(app.size() - 4, 4, ".app") == 0 ? app : "";
}
#endif

}  // namespace

#if defined(__APPLE__)
std::string ExecutablePath() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1, 0);
    if (size == 0 || _NSGetExecutablePath(buffer.data(), &size) != 0) return "";
    char resolved[PATH_MAX];
    if (realpath(buffer.data(), resolved) == nullptr) return buffer.data();
    return resolved;
}

std::string BundleOf(const std::string& exePath) {
    const size_t at = exePath.find("/Contents/MacOS/");
    if (at == std::string::npos) return "";
    const std::string app = exePath.substr(0, at);
    return app.size() > 4 && app.compare(app.size() - 4, 4, ".app") == 0 ? app : "";
}

std::string EmbeddedPlist() {
    unsigned long size = 0;
    const uint8_t* data = getsectiondata(&_mh_execute_header, "__TEXT", "__info_plist", &size);
    if (data == nullptr || size == 0) return "";
    return std::string(reinterpret_cast<const char*>(data), size);
}

bool KeepBundle(const std::string& exePath, const std::string& appPath, const std::string& plist, const std::vector<uint8_t>& icns,
                std::string& program, std::string& made, std::string& why) {
    std::error_code ec;
    const fs::path exe = exePath, app = appPath;
    const fs::path contents = app / "Contents", macos = contents / "MacOS";
    const bool existed = fs::is_directory(macos, ec);
    fs::create_directories(macos, ec);
    if (ec) {
        why = "cannot make " + macos.string() + ": " + ec.message();
        return false;
    }
    if (!KeepFile(contents / "Info.plist", plist, why)) return false;
    if (!KeepFile(contents / "PkgInfo", "APPL????", why)) return false;
    if (!icns.empty()) {
        fs::create_directories(contents / "Resources", ec);
        if (ec) {
            why = "cannot make " + (contents / "Resources").string() + ": " + ec.message();
            return false;
        }
        if (!KeepFile(contents / "Resources" / "icon.icns", std::string(icns.begin(), icns.end()), why)) return false;
    }
    // The program's name inside the bundle is what its Info.plist says (CFBundleExecutable): tpw_launcher in the
    // launcher's own, the game's name in the game's (game_app.h), so that ps, Activity Monitor and System Events, which
    // name a process after its program, name the game too.
    const std::string named = PlistString(plist, "CFBundleExecutable");
    const fs::path linked = macos / (named.empty() ? exe.filename().string() : named);
    program = linked.string();
    if (SameFile(exe, linked)) {
        made = existed ? "kept" : "made";
        return true;
    }
    fs::remove(linked, ec);
    if (link(exe.c_str(), linked.c_str()) == 0) {
        made = existed ? "brought up to date" : "made";
        return true;
    }
    // no hard links on this volume: a copy of the program instead
    fs::copy_file(exe, linked, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        why = "cannot link or copy the program into " + macos.string() + ": " + ec.message();
        return false;
    }
    made = existed ? "brought up to date (a copy of the program: no hard links here)" : "made (a copy of the program: no hard links here)";
    return true;
}

IconRefresh KeepBundleIcon(const std::string& appPath, const tpw::assets::IconImage& icon, std::string& why) {
    const fs::path app = appPath, resources = app / "Contents" / "Resources";
    if (PlistString(ReadWhole(app / "Contents" / "Info.plist"), "CFBundleIconFile") != "icon") return IconRefresh::None;
    // the .icns takes a tenth of a second to encode at every size: a start with the icon of the last one keeps it
    const std::string key = IconKey(icon);
    std::error_code ec;
    if (fs::is_regular_file(resources / "icon.icns", ec) && ReadWhole(resources / "icon.key") == key) return IconRefresh::Kept;
    fs::create_directories(resources, ec);
    if (ec) {
        why = "cannot make " + resources.string() + ": " + ec.message();
        return IconRefresh::Failed;
    }
    const IconRefresh done = KeepIconFile((resources / "icon.icns").string(), EncodeIcns(icon), why);
    if (done == IconRefresh::Failed) return done;
    if (!KeepFile(resources / "icon.key", key, why)) return IconRefresh::Failed;
    if (done != IconRefresh::Written) return done;
    fs::last_write_time(app, fs::file_time_type::clock::now(), ec);
    return IconRefresh::Written;
}

bool KeepLauncherBundle(const std::string& exe, const std::string& app, const std::string& plist, const std::string& dataFolder,
                        std::string& program, std::string& made, std::string& why) {
    if (!KeepBundle(exe, app, LauncherBundlePlist(plist), {}, program, made, why)) return false;
    bool own = false;
    std::string source, iconWhy;
    const IconRefresh done = KeepBundleIcon(app, LoadAppIcon(dataFolder, own, source), iconWhy);
    // no icon is no reason not to run from the bundle: the window still sets it at run time (app_icon.h)
    if (done == IconRefresh::Failed) made += ", no icon (" + iconWhy + ")";
    else made += own ? ", the game's icon" : ", the stand-in icon";
    return true;
}
#endif

bool RunFromAppBundle(int argc, char** argv, const std::string& dataFolder, std::string& note) {
    note.clear();
#if defined(__APPLE__)
    if (WindowlessRun(argc, argv)) return false;
    const std::string exePath = ExecutablePath();
    if (exePath.empty()) {
        note = "not from the bundle: the program's own path is unknown";
        return false;
    }
    const std::string plist = EmbeddedPlist();
    const fs::path exe = exePath;
    const std::string inside = BundleOf(exePath);
    if (!inside.empty()) {
        // The game's bundle opened without --play (double-clicked in the Finder): the launcher's window would be named
        // after the game, so it runs from the launcher's own bundle beside it, as the bare program does below.
        const fs::path app = inside;
        const bool gameBundle = app.filename() == GameBundleName(true) || app.filename() == GameBundleName(false);
        if (gameBundle && HasPlay(argc, argv)) return false;   // the game keeps TPW_LAUNCHER_APP: its way back
        unsetenv(kLauncherAppVariable);
        if (!gameBundle || plist.empty()) return false;
        return ExecFromBundle(exe, app.parent_path() / kLauncherBundleName, plist, dataFolder, argc, argv, "STP/TPW Launcher", note);
    }
    unsetenv(kLauncherAppVariable);
    if (plist.empty()) {
        note = "not from the bundle: the program has no Info.plist embedded";
        return false;
    }
    // Opened by macOS as the installer's application, whose program is a link to this one (app_bundle.h): it runs on as
    // that application, which the Dock shows; an execv into the bundle beside would be a second application.
    if (const std::string opened = OpenedAsApp(plist); !opened.empty() && AppRunsProgram(opened, exePath)) {
        setenv(kLauncherAppVariable, opened.c_str(), 1);
        note = "running as " + opened + " (macOS opened that application, whose program is a link to this one; the Dock shows it)";
        return false;
    }
    return ExecFromBundle(exe, exe.parent_path() / kLauncherBundleName, plist, dataFolder, argc, argv, "STP/TPW Launcher", note);
#elif defined(_WIN32)
    (void)dataFolder;
    // The game's program started without --play (its taskbar button pinned, or double-clicked): the launcher's window
    // would be the game's in the taskbar, so tpw_launcher.exe beside it runs instead (game_app.h HandOverToLauncher).
    if (WindowlessRun(argc, argv) || HasPlay(argc, argv) || CurrentGameBundle().empty()) return false;
    HandOverToLauncher(argc, argv, note);
    return false;
#else
    (void)argc;
    (void)argv;
    (void)dataFolder;
    return false;
#endif
}

#if defined(__APPLE__)
std::string LauncherApp() {
    const char* app = std::getenv(kLauncherAppVariable);
    return app != nullptr ? app : "";
}
#endif

std::string TakeBundleNote() {
    const char* left = std::getenv(kNoteVariable);
    if (left == nullptr) return "";
    std::string note = left;
#if defined(_WIN32)
    _putenv_s(kNoteVariable, "");   // the C runtime's copy and the process's: the game process started later has none
#else
    unsetenv(kNoteVariable);
#endif
    return note;
}

std::string ProgramFolder() {
    const char* base = SDL_GetBasePath();
    std::string folder = base != nullptr ? base : "";
#if defined(__APPLE__)
    // inside a bundle SDL answers Contents/Resources/ (or, were it asked for, Contents/MacOS/): the .app's own folder
    for (const char* inside : {"/Contents/Resources/", "/Contents/MacOS/"}) {
        const size_t at = folder.rfind(inside);
        if (at == std::string::npos || at + std::strlen(inside) != folder.size()) continue;
        const std::string app = folder.substr(0, at);
        if (app.size() < 4 || app.compare(app.size() - 4, 4, ".app") != 0) continue;
        const size_t slash = app.rfind('/');
        if (slash != std::string::npos) folder = app.substr(0, slash + 1);
        break;
    }
#endif
    return folder;
}

}  // namespace tpw::launcher
