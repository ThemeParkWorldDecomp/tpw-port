// game_app.h: on macOS the game runs as an application of its own (the maintainer, 2026-10-26). The Dock names a tile after the
// .app the process was opened from, and Cmd-Tab and Force Quit after that .app's CFBundleName, so a game started inside
// the launcher's process is "STP/TPW Launcher" everywhere but the menu bar (launcher/app_bundle.h, main.cpp
// NameAppMenu). So the launcher window's Play makes, or brings up to date, "Sim Theme Park.app" (US) or "Theme Park
// World.app" (European) beside its own bundle inside build/, with the game's name and its own identifier (the
// launcher's with .stp or .tpw), the launcher's program hard-linked as the bundle's (as app_bundle.cpp links its own),
// and the game's icon as an .icns the launcher writes itself (EncodeIcns: the data folder's icon.png, or the stand-in,
// at every size the Finder and the Dock take, so no sips or iconutil runs and nothing is written outside build/).
// Then it opens that bundle through macOS's application activation (NSWorkspace, game_app_mac.cpp OpenApplication)
// with its own arguments plus `--play E --from-launcher DIR` and its environment, as a process of its own, which
// macOS names and fronts as any application it opens; the launcher waits for it to finish launching, keeping its
// Starting screen up, then exits. The game process, started from the bundle, is the --play run as before, in the
// launcher's working folder (DIR: every relative path in the arguments means what it meant), with "Back to the
// launcher after playing" read from launcher.ini since the marker says the window's Play started it; at its exit that
// setting opens the launcher's own bundle beside it (OpenLauncherApp; the installer's application in Applications
// instead when the launcher ran as that, app_bundle.h), the active application again. When the bundle cannot be made
// or opened the game plays in the launcher's process as before, and the log says why.
//
// Windows does the same in its own terms (the maintainer, 2026-10-27; the rule for every platform is port_CONTRIBUTING.md, "The
// game's own process"). The Task Manager's Processes tab names a process after its program's version resource
// (FileDescription) and its Details tab after the program's file; the taskbar groups windows by AppUserModelID and
// shows a window's own icon. So the window's Play makes, or brings up to date, "Sim Theme Park.exe" or "Theme Park
// World.exe" beside tpw_launcher.exe: a copy of the launcher's program (a hard link would share the launcher's version
// resource, and so its name, with tpw_launcher.exe), whose version resource names the game (GameVersionResource) and
// whose icon is the game's (IconResources: icon.png, or the stand-in, as for the .icns), written with Windows' own
// resource update calls (game_app_win.cpp KeepGameExe). The game's icon is the player's file, so it can only be put in
// at run time, as on the Mac. Then it starts that program with GameArguments, waits for the game's window as the Mac
// waits for the application, and exits. The game's process gives itself an AppUserModelID of its own (GameAppId, the
// game bundle's identifier) and its window the game's icon and the same ID with what pinning it starts (TagGameWindow),
// so the taskbar shows a button of its own named and drawn as the game. The launcher keeps the ID Windows derives from
// its program's path, which is what the Start menu shortcut install.ps1 makes points at. At the game's exit "Back to
// the launcher after playing" starts tpw_launcher.exe again (OpenLauncherApp). The game's program started without
// --play (pinned to the taskbar, double-clicked) starts tpw_launcher.exe with its arguments instead
// (HandOverToLauncher).
//
// The functions above the platforms' lines are plain and run everywhere (launcher/tests/game_app_tests.cpp checks them).
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "icon.h"

struct SDL_Window;

namespace tpw::launcher {

// The option that marks a --play run the launcher window's Play started, with the launcher's working folder as its value.
inline constexpr const char* kFromLauncherOption = "--from-launcher";
// The hand-over's test without a game (main.cpp GameAppTest): what the window's Play does, with a bare window as the game.
inline constexpr const char* kGameAppTestOption = "--game-app-test";

// "Sim Theme Park" (US) or "Theme Park World" (European), and the bundle's name, with .app.
const char* GameAppName(bool us);
std::string GameBundleName(bool us);

// The <string> value of `key` in a plist's text, or empty.
std::string PlistString(const std::string& plist, const char* key);
// Whether the application `app` (a .app folder) has `program` as its program: Contents/MacOS/<its Info.plist's
// CFBundleExecutable>, links followed, is that very file (a symbolic link to it, a hard link of it, or it). The
// installer's application in /Applications has a link to build/tpw_launcher as its program (app_bundle.h).
bool AppRunsProgram(const std::string& app, const std::string& program);
// The game bundle's Info.plist, from the launcher's own (its program, identifier and version): the game's name, the
// launcher's identifier with .stp or .tpw, the icon file "icon" (icon.icns in Contents/Resources).
std::string GamePlist(bool us, const std::string& launcherPlist);
// The Info.plist of the launcher's own bundle beside the program (app_bundle.h), from the one embedded in the program:
// the same, with the icon file "icon" (icon.icns in Contents/Resources) as the game's bundle has it. Without it the
// Dock draws macOS's blank application picture for the bundle until the launcher's window sets the icon at run time,
// and for a tile kept in the Dock all the time it is not running.
std::string LauncherBundlePlist(const std::string& embeddedPlist);
// A short text that changes with the icon's pixels (FNV-1a over the size and the pixels, as hex): the launcher's bundle
// keeps it beside its icon.icns (icon.key), so that a start with the same icon does not encode the .icns again.
std::string IconKey(const tpw::assets::IconImage& icon);

// The game process's arguments (without the program): this run's (LauncherArguments), its --game-app-test pair when it
// has one, then --play us|eur and --from-launcher `workingFolder`.
std::vector<std::string> GameArguments(int argc, char** argv, bool us, const std::string& workingFolder);
// The launcher's arguments again from a game process's: the --play and --from-launcher pairs taken out, and the
// --window-click ones (a scripted click in the launcher's window was for the run that pressed Play; the launcher opened
// again after the game would press it again, and the game has no use for it), and --game-app-test's (the launcher opened
// again after the test's game opens its window, and does not start the test over).
std::vector<std::string> LauncherArguments(int argc, char** argv);
// The --from-launcher value in these arguments, or empty when the run is not the launcher window's Play.
std::string FromLauncherFolder(int argc, char** argv);

// `icon` at `size` x `size` pixels: each pixel the average of the source area it covers (an exact enlargement when the
// size is a multiple; alpha-weighted, so edges do not darken).
tpw::assets::IconImage ResampleIcon(const tpw::assets::IconImage& icon, int size);
// An Apple icon file (.icns) with `icon` as PNG at the ten sizes iconutil makes from an iconset: 16, 32, 128, 256 and
// 512 points, each at 1x and 2x (the element types icp4 ic11 icp5 ic12 ic07 ic13 ic08 ic14 ic09 ic10).
std::vector<uint8_t> EncodeIcns(const tpw::assets::IconImage& icon);

// The game's identifier: the Mac bundle's CFBundleIdentifier, and on Windows the game process's AppUserModelID
// ("org.tpw-port.launcher.stp" or ".tpw").
std::string GameAppId(bool us);
// Windows: the game's program beside tpw_launcher.exe, "Sim Theme Park.exe" or "Theme Park World.exe".
std::string GameExeName(bool us);
// Windows: a command line that CommandLineToArgvW and the C runtime split into `args` again (each quoted when it holds a
// space, a tab or a quote, or is empty; backslashes doubled only before a quote).
std::string WindowsCommandLine(const std::vector<std::string>& args);

// A Windows version resource (VS_VERSIONINFO, RT_VERSION): the fixed part as it is, and one string table's values.
struct VersionResource {
    std::vector<uint8_t> fixed;       // VS_FIXEDFILEINFO, 52 bytes (empty: none)
    std::string table = "040904B0";   // the StringTable's key: language and code page, as hex
    std::vector<std::pair<std::string, std::string>> strings;   // UTF-8, in their order
    uint32_t translation = 0x04B00409;   // VarFileInfo's Translation: the language in the low word, the code page above
};
// Reads `blob` (the resource's bytes); false when it is not a version resource.
bool ParseVersionResource(const std::vector<uint8_t>& blob, VersionResource& out);
std::vector<uint8_t> EncodeVersionResource(const VersionResource& resource);
std::string VersionString(const VersionResource& resource, const char* key);
void SetVersionString(VersionResource& resource, const char* key, const std::string& value);
// The value KeepGameExe writes under kGameExeStampKey, to know a game program it made before from the launcher program
// and the icon it was made from.
inline constexpr const char* kGameExeStampKey = "LauncherCopy";
// The game's program's version resource, from the launcher's: the game's name as FileDescription (the Task Manager's
// Processes tab), ProductName and InternalName, "<game>.exe" as OriginalFilename, the launcher's version and fixed part,
// a Comments line saying what the file is, and `stamp`.
VersionResource GameVersionResource(bool us, const VersionResource& launcher, const std::string& stamp);

// An .ico file as the resources a program carries it in: RT_GROUP_ICON (the directory, each entry naming its picture by
// resource ID, from `firstId` up) and one RT_ICON per picture. False when `ico` is not an icon file.
struct IconResources {
    std::vector<uint8_t> group;
    std::vector<std::vector<uint8_t>> images;
};
bool IconResourcesFromIco(const std::vector<uint8_t>& ico, uint16_t firstId, IconResources& out);

#if defined(__APPLE__) || defined(_WIN32)
// A game application the launcher opened (StartGameApp): where, and the process.
struct GameApp {
    std::string bundle;   // the .app's path (macOS) or the .exe's (Windows)
    std::string made;     // what KeepBundle or KeepGameExe did with it
    int pid = 0;
    void* running = nullptr;   // NSRunningApplication, retained (macOS), or the process's handle (Windows); ReleaseGameApp lets it go
};
// Makes or refreshes the game's bundle beside this program's own bundle (this program must run from inside a bundle,
// or as the installer's application, whose program is the bare one in build/: then beside that), or the game's program
// beside tpw_launcher.exe, and starts it with GameArguments and this process's environment; `activate` fronts it (not
// for --background). False, with why, when it could not: the game then plays in this process.
bool StartGameApp(int argc, char** argv, bool us, const std::string& dataFolder, bool activate, GameApp& app, std::string& why);
// Whether the opened application has finished launching (macOS: its NSApplication is up, SDL's video has started;
// Windows: the process shows a window), or has ended already.
bool GameAppFinishedLaunching(const GameApp& app);
void ReleaseGameApp(GameApp& app);
// The game bundle (its .app path) or the game's program (its .exe) this process runs from, or empty when it runs as the
// launcher.
std::string CurrentGameBundle();
// From a game process: starts the launcher (its bundle, or tpw_launcher.exe) beside this one, as the active
// application, with LauncherArguments; on a Mac, the installer's application instead when the launcher ran as it
// (TPW_LAUNCHER_APP) and its program is still this one. The Mac's bundle gets `dataFolder`'s icon (app_bundle.h
// KeepLauncherBundle).
bool OpenLauncherApp(int argc, char** argv, const std::string& dataFolder, std::string& why);
#endif

#if defined(_WIN32)
// The game's process: its own AppUserModelID (GameAppId), before any window, so the taskbar groups its window apart from
// the launcher's. Logs what it did.
void NameGameProcess(bool us);
// The game's window: the same ID in the window's property store, with what pinning it starts (the game's program) and
// the name and icon a pin shows. Logs what it did.
void TagGameWindow(SDL_Window* window, bool us);
// The game's program started without --play, for a run with a window: starts tpw_launcher.exe beside it with the same
// arguments (`note` for its log, as app_bundle.h's) and exits. Returns only when it could not, with `note` saying why.
void HandOverToLauncher(int argc, char** argv, std::string& note);
#endif

}  // namespace tpw::launcher
