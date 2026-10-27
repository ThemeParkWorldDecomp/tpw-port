// app_bundle.h: on macOS the launcher runs from an application bundle, so that the Dock names it "STP/TPW Launcher"
// (the maintainer, 2026-10-25). The Dock names a tile after the application's file (the .app's name as the Finder shows it; an
// Info.plist's CFBundleName, embedded in the program or in a bundle, only names it in Cmd-Tab and Force Quit), and a
// program outside a bundle is named after itself ("tpw_launcher") or, with an embedded Info.plist, after its folder
// ("build"). The bare build/tpw_launcher is the one file the install script, its themepark command, the updater, CI and
// the README all run, so it stays; what it does first is make (or bring up to date) "STP:TPW Launcher.app" beside
// itself, with its own embedded Info.plist as the bundle's and itself hard-linked as the bundle's program, and run from
// there with execv: the same process id, arguments, environment and standard streams, only the executable's path is
// inside the bundle. (A colon in a file's name is what the Finder shows as a slash.) Launched from inside the bundle
// (that run, or the .app opened from the Finder or kept in the Dock) it runs as it is. The game itself plays from a
// bundle of its own beside it, "Sim Theme Park.app" or "Theme Park World.app", made the same way by the window's Play
// (launcher/game_app.h); one of those opened without --play (double-clicked in the Finder) runs the launcher from the
// launcher's own bundle, as the bare program does. On Windows the launcher is tpw_launcher.exe as it is (the taskbar and
// the Task Manager name it after its version resource, "STP/TPW Launcher"), and the game's program beside it, "Sim Theme
// Park.exe" or "Theme Park World.exe", started without --play hands over to it the same way (port_CONTRIBUTING.md,
// "The game's own process").
#pragma once
#include <string>
#include <vector>

namespace tpw::launcher {

// macOS, outside a bundle, for a run that opens a window: makes or refreshes the bundle beside the program and runs the
// program from it; returns only when it could not (`note` says why; the run goes on as it is). Inside a bundle already,
// for a run with none of a window's work (--version, --help, --check-updates, --import), or not on macOS, returns at
// once with `note` empty. On success the run that follows logs the note it left in TPW_APP_BUNDLE_NOTE (TakeBundleNote).
// On Windows, from the game's program without --play, starts tpw_launcher.exe with this run's arguments and exits
// (game_app.h HandOverToLauncher); returns only when it could not.
bool RunFromAppBundle(int argc, char** argv, std::string& note);
// The note RunFromAppBundle's execv left for the run that followed, once; empty when there was none.
std::string TakeBundleNote();

// The folder holding the bare program (SDL_GetBasePath's folder, with its trailing separator): for a program inside a
// bundle, the folder that holds the .app, where the install script's files are (launcher/ui/updater.h FindInstall).
std::string ProgramFolder();

#if defined(__APPLE__)
inline constexpr const char* kLauncherBundleName = "STP:TPW Launcher.app";   // the Finder shows "STP/TPW Launcher.app"

// The program's own path, with links resolved; empty when unknown.
std::string ExecutablePath();
// The .app folder `exePath` is the program of (the part before /Contents/MacOS/), or empty outside a bundle.
std::string BundleOf(const std::string& exePath);
// The Info.plist the build embedded in the program (CMakeLists.txt, -sectcreate __TEXT __info_plist); empty when none.
std::string EmbeddedPlist();
// Makes `app` (the .app folder) hold the program at `exe`, or brings it up to date: Contents/Info.plist as `plist`,
// Contents/PkgInfo, the program hard-linked (a copy where links are impossible) as Contents/MacOS/<the plist's
// CFBundleExecutable, or exe's name without one> and, when `icns` is not empty, Contents/Resources/icon.icns.
// `program` is the bundle's program; `made` says what was done.
bool KeepBundle(const std::string& exe, const std::string& app, const std::string& plist, const std::vector<uint8_t>& icns,
                std::string& program, std::string& made, std::string& why);
#endif

}  // namespace tpw::launcher
