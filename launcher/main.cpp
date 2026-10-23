// main.cpp: the native launcher. It points the platform layer at the installed game and at
// a folder for what the game writes, then runs the game's own main (ThemeParkWorldMain,
// TPMain.cpp). Whatever is not translated or implemented yet is a generated stand-in that
// reports itself; a summary of the ones that were hit is printed at the end. Without --game
// (or TPW_GAME_DIR) the launcher's own window (launcher/ui/) opens first: the player drops
// their discs there, and Play starts the chosen edition from the verified data folder.
#include <SDL3/SDL.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <vector>

#include <dlfcn.h>
#ifndef _WIN32
#include <execinfo.h>
#include <sys/ucontext.h>
#endif
#include <unistd.h>
#if defined(__APPLE__)
#include <objc/message.h>
#include <objc/runtime.h>
#endif

#include "TPMain.h"
#include "tpw_probe.h"
#include "Arrival.h"
#include "ResolutionBalance.h"
#include "UI_Base.h"
#include "platform.h"
#include "handoff.h"
#ifdef _WIN32
#include "host_win.h"
#endif
#include "platform/game_tables.h"
#include "startup.h"
#include "static_init.h"
#include "stubs.h"
#include "ui/backend.h"
#include "ui/theme.h"
#include "ui/window.h"

namespace {
// The largest preset, 4096 x 3072 (3840 x 2160 at 16:9), rendered offscreen and scaled to the window (the maintainer,
// 2026-10-20; it was Resolution.sam's 2048 x 1536 preset, 7, from 2026-10-11). Presets 8 to 10 are the port's own.
constexpr int32_t kDefaultResolution = 10;
// The screen's shape: 16:9 unless --aspect asks otherwise (the maintainer, 2026-10-20). Presets 1 to 7 keep their
// height and take the width the shape gives them, so 2048 x 1536 becomes 2730 x 1536; presets 8 to 10 keep the width of
// their 16:9 size (2560, 3200, 3840) and take the height. 4:3 is the original game's layout exactly.
constexpr int kDefaultAspectWide = 16;
constexpr int kDefaultAspectHigh = 9;
bool gAbortOnStub = false;
bool gBackToLauncher = false;   // the launcher's "Back to the launcher after playing", for a game started from its window
bool gTraceStubs = false;
long gStubCalls = 0;
const char* volatile gLastStub = nullptr;
// Calls per stand-in (keyed by its name, which the stub table keeps for the whole run), so the
// report can name the ones the game leans on.
std::unordered_map<const char*, long> gStubCallsByName;

// Apply the native override after the game's balance files and saved settings load,
// before the first Process call chooses fonts and the screen size. The translated
// start-up and the original entry point remain unchanged.
class ResolutionOverrideWorld final : public ThemeParkWorld {
public:
    explicit ResolutionOverrideWorld(int32_t preset) : mPreset(preset) {}
    bool Initialise() override {
        if (!ThemeParkWorld::Initialise()) return false;
        gResolutionBalance.mValues[1].mInteger = mPreset;
        return true;
    }
private:
    int32_t mPreset;
};

int ResolutionPreset(const char* size) {
    const char* sizes[] = {"512x384",   "640x480",   "800x600",   "1024x768",  "1280x1024",
                           "1600x1200", "2048x1536", "2560x1920", "3200x2400", "4096x3072"};
    for (int i = 0; i < 10; ++i)
        if (std::strcmp(size, sizes[i]) == 0) return i + 1;
    return 0;
}

// W:H with both whole and positive, no narrower than 4:3; 0 for anything else.
float ParseAspect(const char* text) {
    int wide = 0;
    int high = 0;
    char end = 0;
    if (std::sscanf(text, "%d:%d%c", &wide, &high, &end) != 2 || wide <= 0 || high <= 0 || wide * 3 < high * 4) return 0.0f;
    return static_cast<float>(wide) / static_cast<float>(high);
}

#ifdef TPW_PORT_FIX
// The largest preset, from `preset` down, whose size at the launcher's aspect this machine's OpenGL can render (limit:
// tpw::LargestGameSize, 0 when unknown); said once in the log when it is not `preset`.
int32_t FittingPreset(int32_t preset, int limit) {
    if (preset <= 0 || limit <= 0) return preset;
    int32_t chosen = preset;
    int32_t wide = 0;
    int32_t high = 0;
    UI_PresetSize(chosen, gUIWideAspect, wide, high);
    while (chosen > 1 && (wide > limit || high > limit)) UI_PresetSize(--chosen, gUIWideAspect, wide, high);
    if (chosen != preset) {
        int32_t wanted = 0;
        int32_t wantedHigh = 0;
        UI_PresetSize(preset, gUIWideAspect, wanted, wantedHigh);
        tpw::Log("resolution: %dx%d is larger than this machine's %d-pixel framebuffers; %dx%d (preset %d) instead", wanted,
                 wantedHigh, limit, wide, high, chosen);
    }
    return chosen;
}
#endif

int RunGame(int32_t resolution) {
    // What the Mac's runtime did before main (launcher/static_init.cpp).
    RunStaticInitialisers();
    tpw::StartupStamp("static initialisers done");
    tpw::OnStartupProgress();
    tpw::StartupStamp("ThemeParkWorldMain entered");
    if (resolution == 0) return ThemeParkWorldMain();
    ResolutionOverrideWorld world(resolution);
    world.Run();
    return 0;
}

// A crash right after a stand-in returned is usually caused by it (a zero where the caller
// expected an object, or a structure result left unwritten): name the last one reached.
void OnCrash(int signal) {
    static char text[512];
    const int length = std::snprintf(text, sizeof text, "launcher: crashed (signal %d); the last stand-in reached was: %s\n",
                                     signal, gLastStub != nullptr ? gLastStub : "(none)");
    if (length > 0) (void)!write(2, text, static_cast<size_t>(length));
    if (const char* running = RunningStaticInitialiser()) {
        const int more = std::snprintf(text, sizeof text, "launcher: inside the static initialiser %s\n", running);
        if (more > 0) (void)!write(2, text, static_cast<size_t>(more));
    }
#ifdef _WIN32
    // Where it happened: the exception and the stack that host_win.cpp kept, as module offsets.
    if (const char* fatal = tpw::win32::LastFatalException()) {
        (void)!write(2, "launcher: ", 10);
        (void)!write(2, fatal, static_cast<unsigned>(std::strlen(fatal)));
        (void)!write(2, "\n", 1);
    }
#else
    // Where it happened (the launcher keeps its symbols).
    static void* frames[48];
    backtrace_symbols_fd(frames, backtrace(frames, 48), 2);
#endif
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}

#ifndef _WIN32   // Windows has no sigaction: host_win.cpp records the fault itself
// The faulting instruction and address first: the backtrace's frame after _sigtramp is the caller when the fault is in a
// function without a frame record (a leaf), so it can name the wrong place.
void OnCrashInfo(int signal, siginfo_t* info, void* context) {
    uintptr_t pc = 0;
#if defined(__arm64__) || defined(__aarch64__)
    pc = static_cast<uintptr_t>(static_cast<ucontext_t*>(context)->uc_mcontext->__ss.__pc);
#elif defined(__x86_64__)
    pc = static_cast<uintptr_t>(static_cast<ucontext_t*>(context)->uc_mcontext->__ss.__rip);
#endif
    Dl_info where{};
    const bool named = dladdr(reinterpret_cast<void*>(pc), &where) != 0 && where.dli_sname != nullptr;
    static char text[512];
    const int length = std::snprintf(text, sizeof text, "launcher: fault at address %p, instruction %p (%s + %ld)\n", info->si_addr,
                                     reinterpret_cast<void*>(pc), named ? where.dli_sname : "?",
                                     named ? static_cast<long>(pc - reinterpret_cast<uintptr_t>(where.dli_saddr)) : 0L);
    if (length > 0) (void)!write(2, text, static_cast<size_t>(length));
    OnCrash(signal);
}
#endif

void Usage(const char* program) {
    std::fprintf(stderr,
                 "usage: %s [options]\n"
                 "  --game DIR     the installed game: the folder that holds \"data\" (default: $TPW_GAME_DIR). Without\n"
                 "                 either, the launcher's own window opens: drop your discs on it, then press Play; the\n"
                 "                 verified files, the saves and launcher.ini are kept in the data folder (below)\n"
                 "  --play E       without the window: start edition E (us or eur) from the data folder, as Play does\n"
                 "  --no-preload   the launcher's window reads nothing ahead while it is open (by default it reads the\n"
                 "                 game's tables and the first files of the edition last played, so Play starts sooner)\n"
                 "  --window-click X,Y@F  a scripted left click in the launcher's own window at point (X, Y) of it (it\n"
                 "                 opens 800 x 560 points), pressed before its drawn frame F and released before F + 1\n"
                 "  --data DIR     the folder launcher.ini is in (default: the per-user folder SDL names for the\n"
                 "                 launcher, such as ~/Library/Application Support/STP-TPW Launcher on a Mac)\n"
                 "  --program FILE the game's data tables are read from this full Mac \"SimTheme Park\" or \"Theme Park\n"
                 "                 World\" application (its data fork; the CD's 2.0.1, the 2.04b3 patch's or a 2.1 release)\n"
                 "                 (default: $TPW_GAME_PROGRAM, else \"SimTheme Park\" in the game folder)\n"
                 "  --user DIR     where saves and settings go (default: a per-user folder, one for each edition; from\n"
                 "                 the launcher's window or --play, saves/us or saves/eur in the data folder)\n"
                 "  --edition E    the edition the game folder holds: stp, Sim Theme Park (the US Mac disc), or tpw, Theme\n"
                 "                 Park World (the UK/European disc, from devtools/import_eur.py); the window's title\n"
                 "                 follows it (default: told by the folder's data:Language, American being stp)\n"
                 "  --stp, --eur   the same as --edition stp, --edition tpw\n"
                 "  --windowed     run in a window with a title bar instead of full screen (by default the game is a real\n"
                 "                 full-screen window on its screen, a Space of its own with the menu bar and the dock\n"
                 "                 hidden, as Cmd+Ctrl+F makes it; Cmd+Ctrl+F also leaves it)\n"
                 "  --resolution WxH  the game's resolution, named by its 4:3 size: a Resolution.sam preset (512x384,\n"
                 "                    640x480, 800x600, 1024x768, 1280x1024, 1600x1200, 2048x1536) or a larger one of the\n"
                 "                    port's (2560x1920, 3200x2400, 4096x3072; default: 4096x3072). The game renders at it\n"
                 "                    offscreen and the window shows it scaled\n"
                 "  --aspect W:H   the screen's shape (default 16:9). The Resolution.sam presets keep their height and are\n"
                 "                 widened (2048x1536 becomes 2730x1536 at 16:9, 2458x1536 at 16:10); the larger ones keep\n"
                 "                 the width of their 16:9 size (2560x1440, 3200x1800, 3840x2160 at 16:9; 3840x2400 at\n"
                 "                 16:10). The park is seen further to the sides, the panels keep to the screen's edges\n"
                 "                 and the 4:3 screens are pillarboxed. --aspect 4:3 is the original layout exactly, at\n"
                 "                 the 4:3 size\n"
                 "  --no-intro     skip the logo, the intro movie and the lobby: straight into a park\n"
                 "  --frames N     stop after N passes of the main loop (the launcher's own window: after N drawn frames, exit 0)\n"
                 "  --click X,Y@N  a scripted left click at game pixel (X, Y): pressed on pass N, released on pass N+1\n"
                 "                 (repeatable; the pointer stays there until the real mouse moves)\n"
                 "  --drag X0,Y0-X1,Y1@N[/K]  a scripted left drag: the pointer to (X0, Y0) on pass N, the button down on\n"
                 "                 pass N+2, K steps (default 8) to (X1, Y1), the button up on pass N+K+4 (repeatable)\n"
                 "  --type TEXT@N  scripted typing: one character every two passes from pass N (letters, digits, space;\n"
                 "                 \\r for return)\n"
                 "  --no-movies    skip the logo and intro films\n"
                 "  --mute         no sound\n"
                 "  --display D    open the window on screen D: an SDL display number (0, 1, ...) or part of the\n"
                 "                 screen's name, case-blind (\"builtin\" is the MacBook's own screen; the log lists them)\n"
                 "  --no-fit       a window as large as the game's resolution in points, drawn into directly (by default the\n"
                 "                 window is fitted to the screen and the game renders at its own resolution offscreen)\n"
                 "  --background   open the window behind the others, without taking the focus or moving the mouse\n"
                 "                 (a test run on a screen someone is working on; a click focuses it as usual). Not full\n"
                 "                 screen: a full-screen Space cannot sit behind anything, so the game's window is fitted\n"
                 "                 to the screen instead, inside the menu bar and the dock\n"
                 "  --dump-frames DIR  save the presented frames there as frame-NNNN.bmp (each one that differs from\n"
                 "                     the frame before it; the first 200; the launcher's own window's too, as\n"
                 "                     launcher-frame-NNNN.bmp)\n"
                 "  --dump-every N     with --dump-frames: save only every Nth of those frames\n"

                 "  --stub-abort   abort at the first unimplemented function (for a debugger)\n"
                 "  --stub-trace   log every call of an unimplemented function, not only the first\n",
                 program);
}

// ---- the launcher's window and its data folder (launcher-design section 4: assets/{us,eur,gold}, saves/{us,eur},
// launcher.ini)

// Copies an edition's saves from the folder earlier builds used into the data folder's saves/us or saves/eur, once:
// only while that folder does not exist yet. The folders looked at, in this order, are SDL's pref path for
// "tpw-decomp" ("Theme Park World" or "Theme Park World UK") and the data folder this launcher was first called
// "Midway" (its saves/us or saves/eur); the first one that exists is used. The old folder is left as it was. SDL's pref
// path is <base>/<app>/ without an organisation and <base>/<org>/<app>/ with one, on every system, so the old folders
// are found beside the launcher's own.
void AdoptOldSaves(const std::filesystem::path& prefPath, const std::filesystem::path& saves, bool us) {
    std::error_code ec;
    if (std::filesystem::exists(saves, ec)) return;
    std::filesystem::path base = prefPath;
    if (!base.has_filename()) base = base.parent_path();   // the pref path ends in a separator
    const std::filesystem::path parent = base.parent_path();
    const std::filesystem::path candidates[] = {
        parent / "tpw-decomp" / (us ? "Theme Park World" : "Theme Park World UK"),
        parent / "Midway" / "saves" / (us ? "us" : "eur"),
    };
    for (const std::filesystem::path& old : candidates) {
        if (!std::filesystem::is_directory(old, ec)) continue;
        std::filesystem::create_directories(saves.parent_path(), ec);
        std::filesystem::copy(old, saves, std::filesystem::copy_options::recursive, ec);
        if (ec) tpw::Log("saves: could not copy \"%s\" to \"%s\": %s", old.string().c_str(), saves.string().c_str(), ec.message().c_str());
        else tpw::Log("saves: copied from \"%s\" (the folder earlier builds used; it is left as it was)", old.string().c_str());
        return;
    }
}

// The application menu's title (the bold name beside the Apple menu): "Theme Park World" for the European edition, "Sim
// Theme Park" for the US one, so the menu bar names the game being played rather than the launcher (the maintainer, 2026-10-22).
// Set through the Objective-C runtime (as platform/window.cpp sends orderBack:) on the menu SDL made when its video
// started: [[[[NSApp mainMenu] itemAtIndex:0] submenu] setTitle:]. Nothing is created here: without a running
// NSApplication (the --play path, whose video starts later in the game's own window code) the title is left alone and
// the log says why; creating NSApplication this early would come before SDL's own application class. The Dock's and
// Cmd-Tab's names cannot be changed this way: macOS takes them from the process and its bundle (CFBundleName), so they
// stay the program's name until the launcher ships as a .app bundle.
void NameAppMenu(const char* name) {
#if defined(__APPLE__)
    const char* reason = nullptr;
    void** appVar = reinterpret_cast<void**>(dlsym(RTLD_DEFAULT, "NSApp"));
    void* app = appVar != nullptr ? *appVar : nullptr;
    using SendId = void* (*)(void*, SEL);
    using SendIndex = void* (*)(void*, SEL, long);
    using SendCount = long (*)(void*, SEL);
    using SendText = void* (*)(void*, SEL, const char*);
    using SendSet = void (*)(void*, SEL, void*);
    using SendUtf8 = const char* (*)(void*, SEL);
    void* menu = nullptr;
    void* item = nullptr;
    void* submenu = nullptr;
    void* title = nullptr;
    void* stringClass = reinterpret_cast<void*>(objc_getClass("NSString"));
    if (app == nullptr) reason = "no application yet: the game's window starts it later";
    else if ((menu = reinterpret_cast<SendId>(objc_msgSend)(app, sel_registerName("mainMenu"))) == nullptr)
        reason = "the application has no main menu";
    else if (reinterpret_cast<SendCount>(objc_msgSend)(menu, sel_registerName("numberOfItems")) < 1)
        reason = "the main menu is empty";
    else if ((item = reinterpret_cast<SendIndex>(objc_msgSend)(menu, sel_registerName("itemAtIndex:"), 0)) == nullptr)
        reason = "the main menu has no first item";
    else if ((submenu = reinterpret_cast<SendId>(objc_msgSend)(item, sel_registerName("submenu"))) == nullptr)
        reason = "the first menu item has no submenu";
    else if (stringClass == nullptr ||
             (title = reinterpret_cast<SendText>(objc_msgSend)(stringClass, sel_registerName("stringWithUTF8String:"),
                                                               name)) == nullptr)
        reason = "no NSString for the name";
    if (reason != nullptr) {
        tpw::Log("app name: not changed (%s)", reason);
        return;
    }
    reinterpret_cast<SendSet>(objc_msgSend)(submenu, sel_registerName("setTitle:"), title);
    void* now = reinterpret_cast<SendId>(objc_msgSend)(submenu, sel_registerName("title"));
    const char* text = now != nullptr ? reinterpret_cast<SendUtf8>(objc_msgSend)(now, sel_registerName("UTF8String")) : nullptr;
    if (text != nullptr && std::strcmp(text, name) == 0) tpw::Log("app name: %s", name);
    else tpw::Log("app name: not changed (the menu kept \"%s\")", text != nullptr ? text : "");
#else
    tpw::Log("app name: not changed (only the macOS menu bar has one)");
    (void)name;
#endif
}

// No --game: the edition comes from the data folder, through the launcher's window or --play. Fills in the game
// folder, the edition, the user folder and the program (each unless an option named it already) and returns -1 to
// start the game; any other value is the launcher's exit code (0 when the window was closed).
int ChooseFromDataFolder(tpw::Config& config, std::filesystem::path& program, const std::string& dataDir,
                         const std::string& play) {
    std::string prefPath = dataDir;
    if (prefPath.empty()) {
        char* pref = SDL_GetPrefPath("", tpw::ui::kDataFolderName);
        if (pref == nullptr) {
            std::fprintf(stderr, "launcher: no per-user folder for the launcher's data: %s\n", SDL_GetError());
            return 1;
        }
        prefPath = pref;
        SDL_free(pref);
    }
    tpw::ui::ImporterBackend backend(prefPath);
    std::string err;
    const auto openStart = std::chrono::steady_clock::now();
    if (!backend.Open(err)) {
        std::fprintf(stderr, "launcher: the data folder (launcher.ini in \"%s\") cannot be used: %s\n", prefPath.c_str(),
                     err.c_str());
        return 1;
    }
    {   // the startup check (assets/importer.h OpenCheck): what it compared, rehashed and removed, and how long it took
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - openStart).count();
        const tpw::assets::OpenCheck check = backend.importer()->LastOpenCheck();
        if (!check.hadRecord)
            tpw::Log("data folder opened in %.1f ms; startup check: no record yet, written for %llu files", ms,
                     static_cast<unsigned long long>(check.checked));
        else
            tpw::Log("data folder opened in %.1f ms; startup check: %llu files compared, %llu rehashed, %llu removed", ms,
                     static_cast<unsigned long long>(check.checked), static_cast<unsigned long long>(check.rehashed),
                     static_cast<unsigned long long>(check.Removed()));
        if (check.Removed() > 0) tpw::Log("startup check: %s", check.Notice().c_str());
    }
    tpw::ui::LaunchChoice choice;
    if (!play.empty()) {
        choice.edition = play == "us" ? tpw::assets::kEditionUs : tpw::assets::kEditionEur;
        choice.skipIntro = backend.Setting(tpw::ui::kSkipIntroKey) == "1";
        const tpw::assets::EditionStatus status = backend.Status(choice.edition);
        if (!status.unlocked) {
            std::fprintf(stderr,
                         "launcher: --play %s: %zu of %zu required files of that edition are in \"%s\"\n%s"
                         "launcher: open the launcher without --game and --play, and drop your discs on its window\n",
                         play.c_str(), status.requiredPresent, status.required, backend.DataFolder().c_str(),
                         choice.edition == tpw::assets::kEditionEur && !backend.HasProgram()
                             ? "launcher: the European edition also needs the US Mac CD (the game's tables come from its "
                               "program)\n"
                             : "");
            return 1;
        }
        backend.SetLastChoice(tpw::assets::EditionName(choice.edition), err);
        tpw::launcher::BeginStartup(backend, choice.edition);
    } else {
        // The window opens where the game's would: on the --display screen, behind the others with --background.
        tpw::ui::WindowPlacement placement;
        placement.background = config.background;
        placement.maxFrames = config.maxFrames;   // --frames, --dump-frames, --dump-every, --dump-limit: the window's too
        placement.dumpDir = config.dumpFramesDir.string();
        placement.dumpEvery = config.dumpEvery;
        placement.dumpLimit = config.dumpLimit;
        placement.clicks = tpw::launcher::gWindowClicks;   // --window-click
        if (!config.display.empty()) {
            if (config.background) {   // before SDL's video starts, as the game's window sets them
                SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
                SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, "0");
            }
            // Video stays started (the window and then the game use it; display numbers hold while it runs).
            if (SDL_InitSubSystem(SDL_INIT_VIDEO)) placement.display = tpw::ChosenDisplay();
        }
        switch (tpw::launcher::OpenLauncherWindow(backend, choice, err, placement)) {
            case tpw::ui::WindowOutcome::Quit: return 0;
            case tpw::ui::WindowOutcome::Failed:
                std::fprintf(stderr, "launcher: the launcher's window failed: %s\n", err.c_str());
                return 1;
            case tpw::ui::WindowOutcome::Play: break;
        }
    }
    // The launcher's settings are written to launcher.ini again here, after the importer's last write of the file (the
    // choice it saved on Play), which kept the importer's older copy of the keys. "Skip the intro films" is as
    // --no-movies; "Back to the launcher after playing" opens the launcher again when a game started from its window
    // quits (ReopenLauncher).
    for (const char* key : {tpw::ui::kSkipIntroKey, tpw::ui::kBackToLauncherKey, tpw::ui::kVisitorArrivalsKey,
                            tpw::ui::kVisitorLimitKey, tpw::ui::kSmoothZoomKey}) {
        const std::string value = backend.Setting(key);
        std::string ignored;
        if (!value.empty()) tpw::ui::ImporterBackend::SaveIniExtra(prefPath, key, value, ignored);
    }
    gBackToLauncher = choice.backToLauncher;
#ifdef TPW_PORT_FIX
    {   // the Park rules box (CArrival::HowManyPeople)
        const std::string arrivals = backend.Setting(tpw::ui::kVisitorArrivalsKey);
        gArrivalScale = arrivals == "more" ? 1.5f : arrivals == "many" ? 2.0f : 1.0f;
        const std::string limit = backend.Setting(tpw::ui::kVisitorLimitKey);
        gVisitorLimit = limit == "3000" ? 3000 : limit == "5000" ? 5000 : 1500;
        if (gArrivalScale != 1.0f || gVisitorLimit != 1500)
            tpw::Log("park rules: visitors arriving x%.1f, visitor limit %d", static_cast<double>(gArrivalScale),
                     static_cast<int>(gVisitorLimit));
    }
    // The Settings box's "Smooth wheel zoom" (Will_HandleCameraMessages); off, the original's step, by default.
    gUISmoothWheelZoom = backend.Setting(tpw::ui::kSmoothZoomKey) == "1" ? 1 : 0;
    if (gUISmoothWheelZoom != 0) tpw::Log("smooth wheel zoom (the launcher's Settings)");
#endif
    if (choice.skipIntro) {
        config.movies = false;
        tpw::Log("intro films: skipped (the launcher's Settings)");
    }
    const bool us = choice.edition == tpw::assets::kEditionUs;
    const std::string edition = us ? "stp" : "tpw";
    if (!config.edition.empty() && config.edition != edition)
        tpw::Log("edition: %s was asked for, but the data folder's %s edition was chosen", config.edition.c_str(),
                 tpw::assets::EditionName(choice.edition));
    config.gameDir = backend.importer()->GameFolder(choice.edition);
    config.edition = edition;
    NameAppMenu(us ? "Sim Theme Park" : "Theme Park World");
    if (config.userDir.empty()) {
        const std::filesystem::path saves = std::filesystem::path(backend.DataFolder()) / "saves" / (us ? "us" : "eur");
        AdoptOldSaves(prefPath, saves, us);
        config.userDir = saves;
    }
    if (program.empty()) program = backend.ProgramPath();
    tpw::Log("data folder: %s", backend.DataFolder().c_str());
    if (choice.gold) tpw::Log("Gold rides: switched on, but this build does not add them to the game yet");
    return -1;
}

// "Back to the launcher after playing": the launcher starts again, with this run's arguments (which opened its window),
// as a process of its own, since the game's state lasts for the whole process and cannot be started twice in one.
void ReopenLauncher(int argc, char** argv) {
    std::vector<const char*> args(argv, argv + argc);
    args.push_back(nullptr);
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args.data());
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    SDL_Process* process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (process == nullptr) {
        tpw::Log("launcher: cannot open the launcher again (%s)", SDL_GetError());
        return;
    }
    tpw::Log("launcher: opened again (Settings: back to the launcher after playing)");
    SDL_DestroyProcess(process);   // the launcher runs on
}

void Report() {
    std::vector<const char*> names(static_cast<size_t>(tpw_stub_count()) + 1);
    const int hit = tpw_stub_hits(names.data(), static_cast<int>(names.size()));
    std::fprintf(stderr, "launcher: %d of %d stand-ins were reached (%ld calls in all)\n", hit, tpw_stub_count(),
                 gStubCalls);
    std::vector<std::pair<long, const char*>> byCalls;
    for (const auto& entry : gStubCallsByName) byCalls.emplace_back(entry.second, entry.first);
    std::sort(byCalls.begin(), byCalls.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& entry : byCalls) std::fprintf(stderr, "launcher: %10ld calls  %s\n", entry.first, entry.second);
}
}  // namespace

void tpw_stub_called(const char* name, bool platform, bool first) {
    ++gStubCalls;
    ++gStubCallsByName[name];
    gLastStub = name;
    if (first || gTraceStubs)
        tpw::LogLine(platform ? "unimplemented platform call: " : "not wired up yet (does nothing): ", name);
    if (gAbortOnStub) std::abort();
}

int main(int argc, char** argv) {
    // a trap (a hardened library check), an illegal instruction and an abort (a failed stack check is a silent one) get
    // the backtrace too
#ifdef _WIN32
    for (int crash : {SIGSEGV, SIGILL, SIGABRT}) std::signal(crash, OnCrash);   // Windows has no SIGBUS or SIGTRAP
    tpw::win32::DivideLikeTheMac();        // x86 traps what the Mac divides (host_win.h)
    tpw::win32::RecordFatalExceptions();   // for the crash report (OnCrash)
    if (!tpw::win32::HasStandardError()) {
        // Started from Explorer (a Windows app has no console): the log goes to launcher.log beside launcher.ini.
        std::string folder;
        for (int i = 1; i + 1 < argc; ++i)
            if (std::strcmp(argv[i], "--data") == 0) folder = argv[i + 1];
        if (folder.empty()) {
            if (char* pref = SDL_GetPrefPath("", tpw::ui::kDataFolderName)) {
                folder = pref;
                SDL_free(pref);
            }
        }
        if (!folder.empty()) tpw::win32::LogToFile((std::filesystem::u8path(folder) / "launcher.log").u8string());
    }
#else
    for (int crash : {SIGSEGV, SIGBUS, SIGTRAP, SIGILL, SIGABRT}) {
        struct sigaction action{};
        action.sa_sigaction = OnCrashInfo;
        action.sa_flags = SA_SIGINFO;
        sigemptyset(&action.sa_mask);
        sigaction(crash, &action, nullptr);
    }
#endif
    tpw::Config& config = tpw::config();
    int32_t resolution = kDefaultResolution;
    int aspectWide = kDefaultAspectWide;
    int aspectHigh = kDefaultAspectHigh;
    if (const char* dir = std::getenv("TPW_GAME_DIR")) config.gameDir = dir;
    std::filesystem::path program;   // the application the data tables are read from (platform/game_tables.cpp)
    std::string dataDir, play;       // --data, --play: the launcher's data folder (no --game)
    if (const char* file = std::getenv("TPW_GAME_PROGRAM")) program = file;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> const char* {
            if (i + 1 >= argc) {
                Usage(argv[0]);
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--game") config.gameDir = value();
        else if (arg == "--data") dataDir = value();
        else if (arg == "--play") {
            play = value();
            if (play != "us" && play != "eur") {
                std::fprintf(stderr, "launcher: --play wants us or eur: %s\n", play.c_str());
                return 2;
            }
        }
        else if (arg == "--program") program = value();
        else if (arg == "--no-preload") tpw::launcher::gNoPreload = true;
        else if (arg == "--window-click") {
            const char* spec = value();
            tpw::ui::WindowPlacement::Click click{};
            if (std::sscanf(spec, "%f,%f@%d", &click.x, &click.y, &click.frame) != 3 || click.frame < 1) {
                std::fprintf(stderr, "launcher: --window-click wants X,Y@F (window point and drawn frame from 1): %s\n",
                             spec);
                return 2;
            }
            tpw::launcher::gWindowClicks.push_back(click);
        }
        else if (arg == "--user") config.userDir = value();
        else if (arg == "--edition") {
            config.edition = value();
            if (config.edition != "stp" && config.edition != "tpw") {
                std::fprintf(stderr, "launcher: --edition wants stp or tpw: %s\n", config.edition.c_str());
                return 2;
            }
        }
        else if (arg == "--eur") config.edition = "tpw";
        else if (arg == "--stp") config.edition = "stp";
        else if (arg == "--windowed") config.debugOptions[1] = 0;
        else if (arg == "--resolution") {
            const char* size = value();
            resolution = ResolutionPreset(size);
            if (resolution == 0) {
                std::fprintf(stderr, "launcher: unsupported resolution: %s\n", size);
                Usage(argv[0]);
                return 2;
            }
            tpw::Log("resolution override: %s", size);
        }
        else if (arg == "--aspect") {
            const char* shape = value();
            if (ParseAspect(shape) == 0.0f || std::sscanf(shape, "%d:%d", &aspectWide, &aspectHigh) != 2) {
                std::fprintf(stderr, "launcher: --aspect wants W:H, no narrower than 4:3 (16:9, 16:10, 4:3): %s\n", shape);
                return 2;
            }
        }
        else if (arg == "--no-intro") config.debugOptions[3] = 0;
        else if (arg == "--frames") config.maxFrames = std::atoi(value());
        else if (arg == "--click") {
            const char* spec = value();
            tpw::Config::ScriptedClick click{};
            if (std::sscanf(spec, "%d,%d@%d", &click.x, &click.y, &click.pass) != 3 || click.pass < 1) {
                std::fprintf(stderr, "launcher: --click wants X,Y@N (game pixel and pass number from 1): %s\n", spec);
                return 2;
            }
            config.clicks.push_back(click);
        }
        else if (arg == "--drag") {
            const char* spec = value();
            tpw::Config::ScriptedDrag drag{};
            drag.steps = 8;
            const int read = std::sscanf(spec, "%d,%d-%d,%d@%d/%d", &drag.x0, &drag.y0, &drag.x1, &drag.y1, &drag.pass, &drag.steps);
            if (read < 5 || drag.pass < 1 || drag.steps < 1) {
                std::fprintf(stderr, "launcher: --drag wants X0,Y0-X1,Y1@N or X0,Y0-X1,Y1@N/STEPS: %s\n", spec);
                return 2;
            }
            config.drags.push_back(drag);
        }
        else if (arg == "--type") {
            const std::string spec = value();
            const size_t at = spec.rfind('@');
            tpw::Config::ScriptedTyping typing{};
            if (at == std::string::npos || (typing.pass = std::atoi(spec.c_str() + at + 1)) < 1) {
                std::fprintf(stderr, "launcher: --type wants TEXT@N (the text and the pass number from 1): %s\n", spec.c_str());
                return 2;
            }
            const std::string raw = spec.substr(0, at);
            for (size_t i = 0; i < raw.size(); ++i) {   // the two characters "\\r" stand for return
                if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == 'r') {
                    typing.text += '\r';
                    ++i;
                } else {
                    typing.text += raw[i];
                }
            }
            config.typing.push_back(typing);
        }
        else if (arg == "--probe") {
            config.probe = true;
            tpw_probe::gLog = [](const char* text) { tpw::Log("%s", text); };
        }
        else if (arg == "--dump-frames") config.dumpFramesDir = value();
        else if (arg == "--dump-every") config.dumpEvery = std::atoi(value());
        else if (arg == "--dump-limit") config.dumpLimit = std::atoi(value());
        else if (arg == "--no-movies") config.movies = false;
        else if (arg == "--mute") config.mute = true;
        else if (arg == "--display") config.display = value();
        else if (arg == "--background") config.background = true;
        else if (arg == "--no-fit") config.fit = false;
        else if (arg == "--stub-abort") gAbortOnStub = true;
        else if (arg == "--stub-trace") gTraceStubs = true;
        else {
            Usage(argv[0]);
            return arg == "--help" || arg == "-h" ? 0 : 2;
        }
    }
    if (config.gameDir.empty()) {
        const int exitCode = ChooseFromDataFolder(config, program, dataDir, play);
        if (exitCode >= 0) {
            SDL_Quit();
            return exitCode;
        }
    } else if (!dataDir.empty() || !play.empty()) {
        std::fprintf(stderr, "launcher: --data and --play start from the launcher's data folder; --game names the game itself\n");
        return 2;
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(config.gameDir / "data", ec)) {
        std::fprintf(stderr, "launcher: no game found in \"%s\" (it must hold the game's \"data\" folder)\n",
                     config.gameDir.string().c_str());
        return 1;
    }
    if (config.edition.empty()) config.edition = tpw::EditionOf(config.gameDir);
    if (config.userDir.empty()) {
        // the two editions keep apart: their saves hold text in their own language
        if (char* pref = SDL_GetPrefPath("tpw-decomp", config.edition == "stp" ? "Theme Park World" : "Theme Park World UK")) {
            config.userDir = pref;
            SDL_free(pref);
        } else {
            config.userDir = "tpw-user";
        }
    }
    std::filesystem::create_directories(config.userDir, ec);
    tpw::Log("game folder: %s", config.gameDir.string().c_str());
    tpw::Log("edition: %s", config.edition == "stp" ? "SimTheme Park (stp)" : "Theme Park World (tpw)");
    tpw::Log("user folder: %s", config.userDir.string().c_str());

    // The original program's data tables (the advisor's, the window scripts, the ride templates and the save file's legal
    // texts) are not in the build: they come from the player's own copy of the game, before the static initialisers run.
    if (program.empty()) program = config.gameDir / "SimTheme Park";
    std::string why;
    if (!tpw::launcher::LoadTables(program, &why)) {
        std::fprintf(stderr,
                     "launcher: the game's data tables could not be read from \"%s\": %s\n"
                     "launcher: --program (or TPW_GAME_PROGRAM) names a full Mac \"SimTheme Park\" application, such as the\n"
                     "launcher: US Mac CD's own, as devtools/import_originals.py writes it into the game folder\n",
                     program.string().c_str(), why.c_str());
        return 1;
    }
    tpw::Log("game tables: %s (%s)", tpw::GameTableSource().c_str(), program.string().c_str());

    // 4:3 leaves the game's own sizes alone (the 1280 x 1024 preset stays 5:4); anything wider widens the screen.
#ifdef TPW_PORT_FIX
    gUIWideAspect = aspectWide * 3 == aspectHigh * 4 ? 0.0f : static_cast<float>(aspectWide) / static_cast<float>(aspectHigh);
    tpw::Log("aspect: %d:%d%s", aspectWide, aspectHigh, gUIWideAspect == 0.0f ? " (the original 4:3 layout)" : "");
    gUIHighPresets = 1;
    // The preset is the size the game renders at, not the window's; a machine whose framebuffers are smaller gets the
    // largest preset that fits. TPW_LARGEST_GAME_SIZE stands in for the machine's limit, to check the fallback.
    if (resolution > 0) {
        const char* forced = std::getenv("TPW_LARGEST_GAME_SIZE");
        resolution = FittingPreset(resolution, forced != nullptr ? std::atoi(forced) : tpw::LargestGameSize());
    }
#else
    tpw::Log("aspect: 4:3 (a build without the port's fixes has the original layout only)");
    if (resolution > 7) {
        tpw::Log("resolution: a build without the port's fixes has Resolution.sam's presets only; 2048x1536");
        resolution = 7;
    }
#endif

    const int result = RunGame(resolution);
    Report();
    if (gBackToLauncher) ReopenLauncher(argc, argv);
    SDL_Quit();
    return result;
}
