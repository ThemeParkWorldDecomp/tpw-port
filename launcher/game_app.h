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
// setting opens the launcher's own bundle beside it (OpenLauncherApp), the active application again. When the bundle
// cannot be made or opened the game plays in the launcher's process as before, and the log says why.
//
// The functions above the macOS line are plain and run everywhere (launcher/tests/game_app_tests.cpp checks them).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "icon.h"

namespace tpw::launcher {

// The option that marks a --play run the launcher window's Play started, with the launcher's working folder as its value.
inline constexpr const char* kFromLauncherOption = "--from-launcher";

// "Sim Theme Park" (US) or "Theme Park World" (European), and the bundle's name, with .app.
const char* GameAppName(bool us);
std::string GameBundleName(bool us);

// The <string> value of `key` in a plist's text, or empty.
std::string PlistString(const std::string& plist, const char* key);
// The game bundle's Info.plist, from the launcher's own (its program, identifier and version): the game's name, the
// launcher's identifier with .stp or .tpw, the icon file "icon" (icon.icns in Contents/Resources).
std::string GamePlist(bool us, const std::string& launcherPlist);

// The game process's arguments (without the program): this run's (LauncherArguments), then --play us|eur and
// --from-launcher `workingFolder`.
std::vector<std::string> GameArguments(int argc, char** argv, bool us, const std::string& workingFolder);
// The launcher's arguments again from a game process's: the --play and --from-launcher pairs taken out, and the
// --window-click ones (a scripted click in the launcher's window was for the run that pressed Play; the launcher opened
// again after the game would press it again, and the game has no use for it).
std::vector<std::string> LauncherArguments(int argc, char** argv);
// The --from-launcher value in these arguments, or empty when the run is not the launcher window's Play.
std::string FromLauncherFolder(int argc, char** argv);

// `icon` at `size` x `size` pixels: each pixel the average of the source area it covers (an exact enlargement when the
// size is a multiple; alpha-weighted, so edges do not darken).
tpw::assets::IconImage ResampleIcon(const tpw::assets::IconImage& icon, int size);
// An Apple icon file (.icns) with `icon` as PNG at the ten sizes iconutil makes from an iconset: 16, 32, 128, 256 and
// 512 points, each at 1x and 2x (the element types icp4 ic11 icp5 ic12 ic07 ic13 ic08 ic14 ic09 ic10).
std::vector<uint8_t> EncodeIcns(const tpw::assets::IconImage& icon);

#if defined(__APPLE__)
// A game application the launcher opened (StartGameApp): where, and the NSRunningApplication macOS gave back.
struct GameApp {
    std::string bundle;   // the .app's path
    std::string made;     // what KeepBundle did with it
    int pid = 0;
    void* running = nullptr;   // NSRunningApplication, retained; ReleaseGameApp lets it go
};
// Makes or refreshes the game's bundle beside this program's own bundle (this program must run from inside a bundle)
// and opens it with GameArguments and this process's environment; `activate` fronts it (not for --background).
// False, with why, when it could not: the game then plays in this process.
bool StartGameApp(int argc, char** argv, bool us, const std::string& dataFolder, bool activate, GameApp& app, std::string& why);
// Whether the opened application has finished launching (its NSApplication is up: SDL's video has started).
bool GameAppFinishedLaunching(const GameApp& app);
void ReleaseGameApp(GameApp& app);
// The game bundle this process runs from (its .app path), or empty when it runs from the launcher's bundle or bare.
std::string CurrentGameBundle();
// From a game process: opens the launcher's bundle beside this one, as the active application, with LauncherArguments.
bool OpenLauncherApp(int argc, char** argv, std::string& why);
#endif

}  // namespace tpw::launcher
