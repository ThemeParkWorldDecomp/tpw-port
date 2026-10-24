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
#include <cmath>
#include <vector>

#include <dlfcn.h>
// The _WIN32 parts of this file: Windows port contributed by samueleagle91 (tpw-port PR #1).
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
#include "ResolutionBalance.h"
#include "OptionsBalance.h"
#include "AudioPlaceHolder.h"
#include "UI_Base.h"
#include "lobby.h"
#include "TPSettings.h"
#include "TPAdvisor.h"
#include "TrueTypeRender.h"
#include "analyser.h"
#include "rsse.h"
#include "asset_packs.h"
#include "packs.h"
#include "platform.h"
#include "handoff.h"
#ifdef _WIN32
#include "host_win.h"
#endif
#include "platform/game_tables.h"
#include "startup.h"
#include "static_init.h"
#include "stubs.h"
#include "ui_main.h"
#include "ui/backend.h"
#include "ui/theme.h"
#include "ui/window.h"

namespace {
// The largest preset, 4096 x 3072 (3840 x 2160 at 16:9), rendered offscreen and scaled to the window (the maintainer,
// 2026-10-20; it was Resolution.sam's 2048 x 1536 preset, 7, from 2026-10-11). Presets 8 to 10 are the port's own.
constexpr int32_t kDefaultResolution = 10;
// The screen's shape: the nearest of 4:3, 16:10 and 16:9 to the screen's own unless --aspect asks otherwise
// (ScreenAspect, from tpw-port PR #9 by samueleagle91), and 16:9 when the screen cannot be asked (the maintainer chose
// 16:9, 2026-10-20; a fixed shape left bars on other screens). Presets 1 to 7 keep their
// height and take the width the shape gives them, so 2048 x 1536 becomes 2730 x 1536; presets 8 to 10 keep the width of
// their 16:9 size (2560, 3200, 3840) and take the height. 4:3 is the original game's layout exactly.
constexpr int kDefaultAspectWide = 16;
constexpr int kDefaultAspectHigh = 9;
bool gAbortOnStub = false;
bool gBackToLauncher = false;   // the launcher's "Back to the launcher after playing", for a game started from its window
bool gReopenLauncher = false;   // a game started from the window has quit with gBackToLauncher set (main, after its exit line)
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

#ifdef TPW_PORT_FIX
// The shape of the screen the game opens on (its desktop mode: --display's screen, else the main one), as the nearest of
// the three the UI was laid out for: 4:3, 16:10 and 16:9 (a MacBook's 2056 x 1329 is 16:10, a 21:9 screen 16:9, a 5:4
// or upright one 4:3). False when there is no screen to ask. Video is started for it and stays started (the window and
// the game use it).
bool ScreenAspect(int& wide, int& high) {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return false;
    SDL_DisplayID display = tpw::ChosenDisplay();
    if (display == 0) display = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* mode = display != 0 ? SDL_GetDesktopDisplayMode(display) : nullptr;
    if (mode == nullptr || mode->w <= 0 || mode->h <= 0) return false;
    static const int kShapes[3][2] = {{4, 3}, {16, 10}, {16, 9}};
    const float shape = static_cast<float>(mode->w) / static_cast<float>(mode->h);
    int nearest = 0;
    for (int i = 1; i < 3; ++i) {
        const float distance = std::fabs(shape - static_cast<float>(kShapes[i][0]) / static_cast<float>(kShapes[i][1]));
        const float best = std::fabs(shape - static_cast<float>(kShapes[nearest][0]) / static_cast<float>(kShapes[nearest][1]));
        if (distance < best) nearest = i;
    }
    wide = kShapes[nearest][0];
    high = kShapes[nearest][1];
    return true;
}
#endif

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

// The log file's "exit:" line is written (main's return, a crash; startup.cpp's Pump says its own).
volatile sig_atomic_t gExitLogged = 0;

void LogExit(int code) {
    if (gExitLogged) return;
    gExitLogged = 1;
    tpw::LogFileF("exit: code %d", code);
}

// exit() from somewhere other than main's return (atexit): its code is not known here.
void LogExitAtExit() {
    if (gExitLogged) return;
    gExitLogged = 1;
    tpw::LogToFile("", "exit: through exit() (its code is not known here)");
}

// A crash report's text to stderr and to the log file (write(2) only: this runs in a signal handler).
void CrashWrite(const char* text, int length) {
    if (length <= 0) return;
    (void)!write(2, text, static_cast<size_t>(length));
    const int fd = tpw::LogFileDescriptor();
    if (fd >= 0) (void)!write(fd, text, static_cast<size_t>(length));
}

// A crash right after a stand-in returned is usually caused by it (a zero where the caller
// expected an object, or a structure result left unwritten): name the last one reached.
void OnCrash(int signal) {
    static char text[512];
    CrashWrite(text, std::snprintf(text, sizeof text, "launcher: crashed (signal %d); the last stand-in reached was: %s\n",
                                   signal, gLastStub != nullptr ? gLastStub : "(none)"));
    if (const char* running = RunningStaticInitialiser())
        CrashWrite(text, std::snprintf(text, sizeof text, "launcher: inside the static initialiser %s\n", running));
#ifdef _WIN32
    // Where it happened: the exception and the stack that host_win.cpp kept, as module offsets.
    if (const char* fatal = tpw::win32::LastFatalException()) {
        CrashWrite("launcher: ", 10);
        CrashWrite(fatal, static_cast<int>(std::strlen(fatal)));
        CrashWrite("\n", 1);
    }
    const int fd = tpw::LogFileDescriptor();
    if (fd >= 0) {
#else
    // Where it happened (the launcher keeps its symbols); in the log file too.
    static void* frames[48];
    const int depth = backtrace(frames, 48);
    backtrace_symbols_fd(frames, depth, 2);
    const int fd = tpw::LogFileDescriptor();
    if (fd >= 0) {
        backtrace_symbols_fd(frames, depth, fd);
#endif
        gExitLogged = 1;
        const int length = signal == SIGABRT ? std::snprintf(text, sizeof text, "exit: abort\n")
                                             : std::snprintf(text, sizeof text, "exit: crashed (signal %d)\n", signal);
        if (length > 0) (void)!write(fd, text, static_cast<size_t>(length));
    }
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
    CrashWrite(text, length);
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
                 "  --import PATH [PATH ...]  without the window: import these discs, disc images, zips, installers or\n"
                 "                 folders into the data folder, as a drop on the window does, log what was found, and\n"
                 "                 exit: 0 when the US, European and Gold editions are all ready, 1 otherwise\n"
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
                 "  --packs DIR    the asset packs: zips of replacement game files, each at its place in the game folder\n"
                 "                 (data/init/640/Splash_American.tga), used in name order, a later one's file winning\n"
                 "                 (default: from the launcher's window or --play, packs in the data folder; else none)\n"
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
                 "  --aspect W:H   the screen's shape (default: the nearest of 4:3, 16:10 and 16:9 to the screen's own).\n"
                 "                 The Resolution.sam presets keep their height and are\n"
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
                 "  --move X,Y@N   a scripted pointer move to game pixel (X, Y) on pass N, no button (repeatable)\n"
                 "  --rclick X,Y@N  a scripted right click, as --click (repeatable)\n"
                 "  --key NAME@N   a scripted key: down on pass N, up on pass N+1 (esc, return, space, tab, cmd, ctrl, shift,\n"
                 "                 option or an SDL key name; repeatable)\n"
                 "  --hold NAME@N-M  a scripted key held from pass N to pass M (its modifier counts for the clicks between)\n"
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
                 "  --dump-limit N     with --dump-frames: save at most N frames (200 by default)\n"

                 "  --stub-abort   abort at the first unimplemented function (for a debugger)\n"
                 "  --stub-trace   log every call of an unimplemented function, not only the first\n"
                 "  --version      print the launcher's version and build, and exit\n"
                 "Every run's log is also written to logs/launcher-YYYYMMDD-HHMMSS.log in the data folder (the %zu most\n"
                 "recent runs are kept), with the launcher's version, the system and the hardware at its top: a bug report\n"
                 "attaches the newest one\n",
                 program, tpw::kLogFilesKept);
}

// The options that take a value (main's loop), so the early look below skips their values.
bool TakesValue(const std::string& arg) {
    static const char* const kWithValue[] = {"--game",  "--data",        "--play",       "--import",     "--program",    "--window-click",
                                             "--user",  "--edition",     "--resolution", "--aspect",     "--frames",
                                             "--click", "--drag",        "--type",       "--dump-frames", "--dump-every",
                                             "--dump-limit", "--display"};
    for (const char* option : kWithValue)
        if (arg == option) return true;
    return false;
}

// --version and --help answer before the log file is opened (no file for them): the exit code, or -1 to run. `dataDir`
// gets --data's value, where the log file goes.
int EarlyOptions(int argc, char** argv, std::string& dataDir) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--version") {
            std::printf("%s\nbuild: %s\n", tpw::ui::kProductNameWithVersion, TPW_LAUNCHER_BUILD);
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            Usage(argv[0]);
            return 0;
        }
        if (TakesValue(arg) && i + 1 < argc) {
            if (arg == "--data") dataDir = argv[i + 1];
            ++i;
        }
    }
    return -1;
}

// Opens the run's log file (platform/log_file.h) in the data folder's logs/: --data's folder, else the per-user one
// (SDL's pref path, as ChooseFromDataFolder finds it); when that cannot be made, in the temporary folder. stderr's first
// line names it.
void OpenLog(int argc, char** argv, const std::string& dataDir) {
    const std::string firstLine = std::string(tpw::ui::kProductNameWithVersion) + " (build: " + TPW_LAUNCHER_BUILD + ")";
    std::filesystem::path folder;
    std::string why;
    if (!dataDir.empty()) {
        folder = std::filesystem::path(dataDir) / "logs";
    } else if (char* pref = SDL_GetPrefPath("", tpw::ui::kDataFolderName)) {
        folder = std::filesystem::path(pref) / "logs";
        SDL_free(pref);
    } else {
        why = std::string("no per-user folder: ") + SDL_GetError();
    }
    if (!folder.empty() && tpw::OpenLogFile(folder, firstLine, argc, argv, why)) {
        std::fprintf(stderr, "log: %s\n", tpw::LogFilePath().c_str());
        return;
    }
    std::error_code ec;
    const std::filesystem::path temp = std::filesystem::temp_directory_path(ec) / tpw::ui::kDataFolderName / "logs";
    std::string tempWhy;
    if (!ec && tpw::OpenLogFile(temp, firstLine, argc, argv, tempWhy)) {
        std::fprintf(stderr, "log: %s\n", tpw::LogFilePath().c_str());
        tpw::LogRawF("launcher: the log file is in the temporary folder: the data folder's logs/ cannot be used (%s)",
                     why.c_str());
        return;
    }
    tpw::LogRawF("launcher: no log file this run (%s; %s)\n", why.c_str(),
                 ec ? ec.message().c_str() : tempWhy.c_str());
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
// Theme Park" for the US one, so the menu bar names the game being played rather than the launcher (the maintainer, 2026-10-22);
// while the launcher's own window is up, the launcher's name with its version (kProductNameWithVersion, the maintainer, 2026-10-23).
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
int ChooseFromDataFolder(tpw::Config& config, std::filesystem::path& program, std::filesystem::path& packs,
                         const std::string& dataDir, const std::string& play) {
    std::string prefPath = dataDir;
    if (prefPath.empty()) {
        char* pref = SDL_GetPrefPath("", tpw::ui::kDataFolderName);
        if (pref == nullptr) {
            tpw::LogRawF("launcher: no per-user folder for the launcher's data: %s\n", SDL_GetError());
            return 1;
        }
        prefPath = pref;
        SDL_free(pref);
    }
    tpw::ui::ImporterBackend backend(prefPath);
    std::string err;
    const auto openStart = std::chrono::steady_clock::now();
    if (!backend.Open(err)) {
        tpw::LogRawF("launcher: the data folder (launcher.ini in \"%s\") cannot be used: %s\n", prefPath.c_str(),
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
            tpw::LogRawF(
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
        // The window's report lines and its screens go to the log file too; its About box shows the logs folder.
        placement.log = [](const char* line) { tpw::LogRaw(line); };
        placement.videoStarted = [] { tpw::LogDisplayOnce(); };
        // While the launcher window is up the menu bar names it with its version; Play renames it after the game below.
        placement.windowShown = [] { NameAppMenu(tpw::ui::kProductNameWithVersion); };
        if (!tpw::LogFilePath().empty()) placement.logsFolder = std::filesystem::path(tpw::LogFilePath()).parent_path().string();
        if (!config.display.empty()) {
            if (config.background) {   // before SDL's video starts, as the game's window sets them
                SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
                SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, "0");
            }
            // Video stays started (the window and then the game use it; display numbers hold while it runs).
            if (SDL_InitSubSystem(SDL_INIT_VIDEO)) {
                tpw::LogDisplayOnce();
                placement.display = tpw::ChosenDisplay();
            }
        }
        switch (tpw::launcher::OpenLauncherWindow(backend, choice, err, placement)) {
            case tpw::ui::WindowOutcome::Quit: return 0;
            case tpw::ui::WindowOutcome::Failed:
                tpw::LogRawF("launcher: the launcher's window failed: %s\n", err.c_str());
                return 1;
            case tpw::ui::WindowOutcome::Play: break;
        }
    }
    // The launcher's settings are written to launcher.ini again here, after the importer's last write of the file (the
    // choice it saved on Play), which kept the importer's older copy of the keys. "Skip the intro films" is as
    // --no-movies; "Back to the launcher after playing" opens the launcher again when a game started from its window
    // quits (ReopenLauncher). From tpw-port PRs #10, #20, #22 and #24 by samueleagle91.
    for (const char* key : {tpw::ui::kSkipIntroKey, tpw::ui::kBackToLauncherKey, tpw::ui::kSmoothZoomKey,
                            tpw::ui::kInterfaceSizeKey, tpw::ui::kPcSoundPicksKey}) {
        const std::string value = backend.Setting(key);
        std::string ignored;
        if (!value.empty()) tpw::ui::ImporterBackend::SaveIniExtra(prefPath, key, value, ignored);
    }
    for (const tpw::ui::SamOption& option : tpw::ui::SamOptions()) {   // the Graphics section's keys, as above
        const std::string value = backend.Setting(option.key);
        std::string ignored;
        if (!value.empty()) tpw::ui::ImporterBackend::SaveIniExtra(prefPath, option.key, value, ignored);
    }
    gBackToLauncher = choice.backToLauncher;
    // The Park rules box (tpw-port PR #23: more visitors per bus, a higher visitor limit) left the port, which changes
    // only how the game is shown and driven, never how a park plays; it is to become a tpw-mods mod. A launcher.ini that
    // still has its keys keeps them, and they are ignored.
    for (const char* key : {"visitor_arrivals", "visitor_limit"}) {
        const std::string value = backend.Setting(key);
        if (!value.empty())
            tpw::Log("park rules: launcher.ini's %s=%s is ignored (Park rules are no longer part of the port)", key,
                     value.c_str());
    }
#ifdef TPW_PORT_FIX
    // The Settings box's "Smooth wheel zoom" (Will_HandleCameraMessages); off, the original's step, by default.
    gUISmoothWheelZoom = backend.Setting(tpw::ui::kSmoothZoomKey) == "1" ? 1 : 0;
    if (gUISmoothWheelZoom != 0) tpw::Log("smooth wheel zoom (the launcher's Settings)");
    {   // the Settings box's interface size (UI_SetRealScreenRez)
        const std::string size = backend.Setting(tpw::ui::kInterfaceSizeKey);
        gUIScale = size == "85" ? 0.85f : size == "70" ? 0.7f : 1.0f;
        if (gUIScale != 1.0f) tpw::Log("interface size: %s%%", size.c_str());
    }
    // The Settings box's "Varied sounds" (SoundRandom, include/AudioPlaceHolder.h): on unless launcher.ini says 0.
    gPortPcSoundPicks = backend.Setting(tpw::ui::kPcSoundPicksKey) != "0";
    tpw::Log("sound picks: %s (the launcher's Settings)", gPortPcSoundPicks ? "varied, as on the PC" : "the Mac's");
    // The Settings box's Graphics section: the original's options files' values the player chose (EngineStartup_OptionsFile).
    for (const tpw::ui::SamOption& option : tpw::ui::SamOptions()) {
        const std::string value = backend.Setting(option.key);
        const int choice = tpw::ui::SamChoice(option, value);
        if (choice >= 0) {
            gPortSamOptions[option.slot] = option.values[static_cast<size_t>(choice)];
            tpw::Log("graphics: %s %s (the launcher's Settings)", option.title, option.shown[static_cast<size_t>(choice)].c_str());
        } else if (!value.empty() && value != "default") {
            tpw::Log("graphics: launcher.ini's %s=%s is not one of its choices; the detail level's own is kept", option.key,
                     value.c_str());
        }
    }
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
    if (packs.empty()) packs = std::filesystem::path(backend.DataFolder()) / "packs";
    tpw::Log("data folder: %s", backend.DataFolder().c_str());
    tpw::LogFileF("play: %s (%s), from %s; game folder %s", us ? "Sim Theme Park, the US edition" : "Theme Park World, the European edition",
                  tpw::assets::EditionName(choice.edition), play.empty() ? "the launcher window's Play" : "--play",
                  config.gameDir.string().c_str());
    if (choice.gold) tpw::Log("Gold rides: switched on, but this build does not add them to the game yet");
    return -1;
}

// --import: the drop on the launcher's window without the window, for a terminal or a test machine with no one to drop
// anything: the same importer on the same data folder (ChooseFromDataFolder's), its progress every ten seconds and its
// report in the log. Returns the exit code: 0 when the US, European and Gold editions are all ready, 1 otherwise.
int ImportFromOptions(const std::string& dataDir, const std::vector<std::string>& paths) {
    std::string prefPath = dataDir;
    if (prefPath.empty()) {
        char* pref = SDL_GetPrefPath("", tpw::ui::kDataFolderName);
        if (pref == nullptr) {
            tpw::LogRawF("launcher: no per-user folder for the launcher's data: %s\n", SDL_GetError());
            return 1;
        }
        prefPath = pref;
        SDL_free(pref);
    }
    tpw::ui::ImporterBackend backend(prefPath);
    std::string err;
    if (!backend.Open(err)) {
        tpw::LogRawF("launcher: the data folder (launcher.ini in \"%s\") cannot be used: %s\n", prefPath.c_str(),
                     err.c_str());
        return 1;
    }
    tpw::Log("import: into the data folder %s", backend.DataFolder().c_str());
    for (const std::string& path : paths) tpw::Log("import: %s", path.c_str());
    const auto start = std::chrono::steady_clock::now();
    if (!backend.Start(paths, err)) {
        tpw::LogRawF("launcher: --import cannot start: %s\n", err.c_str());
        return 1;
    }
    auto seconds = [&start] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    };
    uint64_t written = 0;
    auto take = [&written](const std::vector<tpw::assets::ImportProgress>& events) {
        for (const tpw::assets::ImportProgress& p : events)
            if (p.kind == tpw::assets::ImportProgress::Kind::Written) ++written;
    };
    double nextLine = 10.0;
    while (backend.Running()) {
        SDL_Delay(100);
        take(backend.Drain());
        if (seconds() < nextLine) continue;
        nextLine += 10.0;
        const tpw::assets::ImportProgress latest = backend.Latest();
        if (latest.kind == tpw::assets::ImportProgress::Kind::Hashing)
            tpw::Log("import: %.0f s: file %llu of %llu, %llu of %llu MB read, %llu written", seconds(),
                     static_cast<unsigned long long>(latest.n), static_cast<unsigned long long>(latest.total),
                     static_cast<unsigned long long>(latest.bytes >> 20),
                     static_cast<unsigned long long>(latest.totalBytes >> 20), static_cast<unsigned long long>(written));
        else
            tpw::Log("import: %.0f s: opening what was given, %llu files found so far", seconds(),
                     static_cast<unsigned long long>(latest.total));
    }
    const tpw::assets::ImportReport report = backend.Wait();
    take(backend.Drain());
    tpw::Log("import: finished in %.1f s: %s", seconds(), report.Summary().c_str());
    tpw::Log("import: %llu files seen in %llu containers, %llu matched, %llu written, %llu already there, %llu unknown",
             static_cast<unsigned long long>(report.entries), static_cast<unsigned long long>(report.containers),
             static_cast<unsigned long long>(report.matched), static_cast<unsigned long long>(report.written),
             static_cast<unsigned long long>(report.alreadyThere), static_cast<unsigned long long>(report.unknownFiles));
    for (const std::string& error : report.errors) tpw::Log("import: error: %s", error.c_str());
    for (const std::string& dest : report.differentVersions) tpw::Log("import: a different version (not used): %s", dest.c_str());
    if (report.cancelled || report.aborted) tpw::Log("import: stopped before the end%s", report.aborted ? " by a write error" : "");
    bool ready = !report.cancelled && !report.aborted;
    for (tpw::assets::Edition edition : {tpw::assets::kEditionUs, tpw::assets::kEditionEur, tpw::assets::kEditionGold}) {
        const tpw::assets::EditionStatus status = backend.Status(edition);
        std::string still;
        for (int disc = 0; disc < 3; ++disc) {
            if (status.missing[disc] == 0) continue;
            still += still.empty() ? "; still missing: " : ", ";
            still += std::to_string(status.missing[disc]) + " from " +
                     tpw::assets::SourceDiscName(static_cast<tpw::assets::DiscSource>(disc));
        }
        tpw::Log("import: %s: %s, %zu of %zu required files%s", tpw::assets::EditionName(edition), status.StateWord(),
                 status.requiredPresent, status.required, still.c_str());
        ready = ready && status.unlocked;
    }
    return ready ? 0 : 1;
}

// "Back to the launcher after playing": the launcher starts again, with this run's arguments (which opened its window),
// as a process of its own, since the game's state lasts for the whole process and cannot be started twice in one. It runs
// from main, after the log file's "exit:" line has the game's code. From tpw-port PR #22 by samueleagle91.
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
    tpw::LogRawF("launcher: %d of %d stand-ins were reached (%ld calls in all)\n", hit, tpw_stub_count(),
                 gStubCalls);
    std::vector<std::pair<long, const char*>> byCalls;
    for (const auto& entry : gStubCallsByName) byCalls.emplace_back(entry.second, entry.first);
    std::sort(byCalls.begin(), byCalls.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& entry : byCalls) tpw::LogRawF("launcher: %10ld calls  %s\n", entry.first, entry.second);
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

namespace {
int LauncherMain(int argc, char** argv) {
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
    bool aspectGiven = false;   // --aspect; without it the screen's own shape (ScreenAspect)
    if (const char* dir = std::getenv("TPW_GAME_DIR")) config.gameDir = dir;
    std::filesystem::path program;   // the application the data tables are read from (platform/game_tables.cpp)
    std::filesystem::path packs;     // --packs: the asset packs' folder (asset_packs.h)
    std::string dataDir, play;       // --data, --play: the launcher's data folder (no --game)
    std::vector<std::string> imports;   // --import: the paths to import into it, with no window
    bool gameGiven = false;             // --game (TPW_GAME_DIR alone does not stop --import)
    if (const char* file = std::getenv("TPW_GAME_PROGRAM")) program = file;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> const char* {
            if (i + 1 >= argc) {
                Usage(argv[0]);
                LogExit(2);
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--game") {
            config.gameDir = value();
            gameGiven = true;
        }
        else if (arg == "--data") dataDir = value();
        else if (arg == "--play") {
            play = value();
            if (play != "us" && play != "eur") {
                tpw::LogRawF("launcher: --play wants us or eur: %s\n", play.c_str());
                return 2;
            }
        }
        else if (arg == "--import") {
            imports.push_back(value());
            while (i + 1 < argc && argv[i + 1][0] != '-') imports.push_back(argv[++i]);   // the paths after it
        }
        else if (arg == "--program") program = value();
        else if (arg == "--no-preload") tpw::launcher::gNoPreload = true;
        else if (arg == "--window-click") {
            const char* spec = value();
            tpw::ui::WindowPlacement::Click click{};
            if (std::sscanf(spec, "%f,%f@%d", &click.x, &click.y, &click.frame) != 3 || click.frame < 1) {
                tpw::LogRawF("launcher: --window-click wants X,Y@F (window point and drawn frame from 1): %s\n",
                             spec);
                return 2;
            }
            tpw::launcher::gWindowClicks.push_back(click);
        }
        else if (arg == "--user") config.userDir = value();
        else if (arg == "--packs") packs = value();
        else if (arg == "--edition") {
            config.edition = value();
            if (config.edition != "stp" && config.edition != "tpw") {
                tpw::LogRawF("launcher: --edition wants stp or tpw: %s\n", config.edition.c_str());
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
                tpw::LogRawF("launcher: unsupported resolution: %s\n", size);
                Usage(argv[0]);
                return 2;
            }
            tpw::Log("resolution override: %s", size);
        }
        else if (arg == "--aspect") {
            const char* shape = value();
            if (ParseAspect(shape) == 0.0f || std::sscanf(shape, "%d:%d", &aspectWide, &aspectHigh) != 2) {
                tpw::LogRawF("launcher: --aspect wants W:H, no narrower than 4:3 (16:9, 16:10, 4:3): %s\n", shape);
                return 2;
            }
            aspectGiven = true;
        }
        else if (arg == "--no-intro") config.debugOptions[3] = 0;
        else if (arg == "--frames") config.maxFrames = std::atoi(value());
        else if (arg == "--click") {
            const char* spec = value();
            tpw::Config::ScriptedClick click{};
            if (std::sscanf(spec, "%d,%d@%d", &click.x, &click.y, &click.pass) != 3 || click.pass < 1) {
                tpw::LogRawF("launcher: --click wants X,Y@N (game pixel and pass number from 1): %s\n", spec);
                return 2;
            }
            config.clicks.push_back(click);
        }
        else if (arg == "--move" || arg == "--rclick") {
            const char* spec = value();
            int32_t x = 0, y = 0, pass = 0;
            if (std::sscanf(spec, "%d,%d@%d", &x, &y, &pass) != 3 || pass < 1) {
                std::fprintf(stderr, "launcher: %s wants X,Y@N (game pixel and pass number from 1): %s\n", arg.c_str(), spec);
                return 2;
            }
            if (arg == "--move") config.moves.push_back({pass, x, y});
            else config.rightClicks.push_back({pass, x, y});
        }
        else if (arg == "--key" || arg == "--hold") {
            const std::string spec = value();
            const size_t at = spec.rfind('@');
            tpw::Config::ScriptedKey key{};
            int32_t down = 0, up = 0;
            const int read = at == std::string::npos ? 0 : std::sscanf(spec.c_str() + at + 1, "%d-%d", &down, &up);
            key.scancode = at == std::string::npos ? 0 : tpw::ScancodeFromKeyName(spec.substr(0, at).c_str());
            key.down = down;
            key.up = arg == "--key" ? down + 1 : up;
            if (key.scancode == 0 || down < 1 || (arg == "--key" ? read != 1 : read != 2 || up <= down)) {
                std::fprintf(stderr, "launcher: %s wants NAME@N%s (esc, return, space, tab, cmd, ctrl, shift, option or an SDL key name): %s\n",
                             arg.c_str(), arg == "--key" ? "" : "-M", spec.c_str());
                return 2;
            }
            config.keys.push_back(key);
        }
        else if (arg == "--drag") {
            const char* spec = value();
            tpw::Config::ScriptedDrag drag{};
            drag.steps = 8;
            const int read = std::sscanf(spec, "%d,%d-%d,%d@%d/%d", &drag.x0, &drag.y0, &drag.x1, &drag.y1, &drag.pass, &drag.steps);
            if (read < 5 || drag.pass < 1 || drag.steps < 1) {
                tpw::LogRawF("launcher: --drag wants X0,Y0-X1,Y1@N or X0,Y0-X1,Y1@N/STEPS: %s\n", spec);
                return 2;
            }
            config.drags.push_back(drag);
        }
        else if (arg == "--type") {
            const std::string spec = value();
            const size_t at = spec.rfind('@');
            tpw::Config::ScriptedTyping typing{};
            if (at == std::string::npos || (typing.pass = std::atoi(spec.c_str() + at + 1)) < 1) {
                tpw::LogRawF("launcher: --type wants TEXT@N (the text and the pass number from 1): %s\n", spec.c_str());
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
    if (!imports.empty()) {
        if (gameGiven || !play.empty()) {
            tpw::LogRawF("launcher: --import fills the launcher's data folder and exits; it takes no --game or --play\n");
            return 2;
        }
        return ImportFromOptions(dataDir, imports);
    }
    if (config.gameDir.empty()) {
        const int exitCode = ChooseFromDataFolder(config, program, packs, dataDir, play);
        if (exitCode >= 0) {
            SDL_Quit();
            return exitCode;
        }
    } else if (!dataDir.empty() || !play.empty()) {
        tpw::LogRawF("launcher: --data and --play start from the launcher's data folder; --game names the game itself\n");
        return 2;
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(config.gameDir / "data", ec)) {
        tpw::LogRawF("launcher: no game found in \"%s\" (it must hold the game's \"data\" folder)\n",
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
    if (!packs.empty()) {
        tpw::launcher::UsePacks(packs);
#ifdef TPW_PORT_FIX
        // Only when a pack is in use: a pack's logo or loading picture may be of another shape (src/engine/tb_splash.cpp).
        gPortPackPictures = !tpw::PackDirs().empty();
#endif
    }

    // The original program's data tables (the advisor's, the window scripts, the ride templates and the save file's legal
    // texts) are not in the build: they come from the player's own copy of the game, before the static initialisers run.
    if (program.empty()) program = config.gameDir / "SimTheme Park";
    std::string why;
    if (!tpw::launcher::LoadTables(program, &why)) {
        tpw::LogRawF(
                     "launcher: the game's data tables could not be read from \"%s\": %s\n"
                     "launcher: --program (or TPW_GAME_PROGRAM) names a full Mac \"SimTheme Park\" application, such as the\n"
                     "launcher: US Mac CD's own, as devtools/import_originals.py writes it into the game folder\n",
                     program.string().c_str(), why.c_str());
        return 1;
    }
    tpw::Log("game tables: %s (%s)", tpw::GameTableSource().c_str(), program.string().c_str());

    // 4:3 leaves the game's own sizes alone (the 1280 x 1024 preset stays 5:4); anything wider widens the screen.
#ifdef TPW_PORT_FIX
    if (!aspectGiven && !ScreenAspect(aspectWide, aspectHigh)) {
        aspectWide = kDefaultAspectWide;
        aspectHigh = kDefaultAspectHigh;
    }
    gUIWideAspect = aspectWide * 3 == aspectHigh * 4 ? 0.0f : static_cast<float>(aspectWide) / static_cast<float>(aspectHigh);
    tpw::Log("aspect: %d:%d%s", aspectWide, aspectHigh, gUIWideAspect == 0.0f ? " (the original 4:3 layout)" : "");
    gUIHighPresets = 1;
    // The online world's servers shut down: its globe button stays on the islands (include/lobby.h).
    gPortOnlineWorldGone = true;
    // The park map's destroy callback at the quit asks the advisor of a world already deleted (include/ui_main.h).
    gPortNoWorldAdvisorGuard = true;
    // The fixes of samueleagle91 (tpw-port PRs #2, #5, #7, #12 and #13): the advisor's aspect (TPAdvisor.h), the Options
    // panel as a 4:3 panel (UI_Base.h), the signs' text (TrueTypeRender.h) and the spawns from the script code (analyser.h).
    gPortAdvisorAspect = true;
    gPortWidePanel = true;
    gPortSignText = true;
    gPortScriptCode = true;
    // The ride context read as the tag_sMesh it is (rsse.h; samueleagle91, tpw-port PR #25).
    gPortRideCtx = true;
    // The 2D map's pointer, zoom and view in the unit space and at the interface size (UI_Base.h; samueleagle91, tpw-port
    // PR #24).
    gPortWideMap = true;
    // The preset is the size the game renders at, not the window's; a machine whose framebuffers are smaller gets the
    // largest preset that fits. TPW_LARGEST_GAME_SIZE stands in for the machine's limit, to check the fallback.
    if (resolution > 0) {
        const char* forced = std::getenv("TPW_LARGEST_GAME_SIZE");
        resolution = FittingPreset(resolution, forced != nullptr ? std::atoi(forced) : tpw::LargestGameSize());
        // The launcher owns the size (the Resolution.sam value ResolutionOverrideWorld sets): the options panel shows
        // the size the game runs at and offers no other (include/TPSettings.h); Config.tcf's resolution is ignored.
        gPortScreenWidth = &gScreenWidth;
        gPortScreenHeight = &gScreenHeight;
        tpw::Log("video: the launcher sets the resolution; Config.tcf's resolution is ignored");
    }
#else
    tpw::Log("aspect: 4:3 (a build without the port's fixes has the original layout only)");
    if (resolution > 7) {
        tpw::Log("resolution: a build without the port's fixes has Resolution.sam's presets only; 2048x1536");
        resolution = 7;
    }
#endif

    // The size the game renders at, for the log file (the preset's size at the aspect above).
#ifdef TPW_PORT_FIX
    {
        int32_t gameWide = 0;
        int32_t gameHigh = 0;
        if (resolution > 0) UI_PresetSize(resolution, gUIWideAspect, gameWide, gameHigh);
        tpw::LogFileF("game size: %dx%d (preset %d), aspect %d:%d, %s, %s", gameWide, gameHigh, resolution, aspectWide,
                      aspectHigh, config.debugOptions[1] != 0 && !config.background ? "full screen" : "in a window",
                      config.fit ? "scaled to the window" : "drawn into the window directly");
    }
#else
    tpw::LogFileF("game size: preset %d, aspect 4:3", resolution);
#endif

    const int result = RunGame(resolution);
    Report();
    gReopenLauncher = gBackToLauncher;
    SDL_Quit();
    return result;
}
}  // namespace

int main(int argc, char** argv) {
    // --version and --help need no log file; every other run opens one before anything logs, and ends it with how the
    // run ended (exit: code N here; a crash or an abort writes its own from the signal handler).
    std::string dataDir;
    const int early = EarlyOptions(argc, argv, dataDir);
    if (early >= 0) return early;
    OpenLog(argc, argv, dataDir);
    std::atexit(LogExitAtExit);
    const int code = LauncherMain(argc, argv);
    LogExit(code);
    if (gReopenLauncher) ReopenLauncher(argc, argv);
    return code;
}
