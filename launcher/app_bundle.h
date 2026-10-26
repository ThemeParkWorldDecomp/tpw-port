// app_bundle.h: on macOS the launcher runs from an application bundle, so that the Dock names it "STP/TPW Launcher"
// (the maintainer, 2026-10-25). The Dock names a tile after the application's file (the .app's name as the Finder shows it; an
// Info.plist's CFBundleName, embedded in the program or in a bundle, only names it in Cmd-Tab and Force Quit), and a
// program outside a bundle is named after itself ("tpw_launcher") or, with an embedded Info.plist, after its folder
// ("build"). The bare build/tpw_launcher is the one file the install script, its themepark command, the updater, CI and
// the README all run, so it stays; what it does first is make (or bring up to date) "STP:TPW Launcher.app" beside
// itself, with its own embedded Info.plist as the bundle's and itself hard-linked as the bundle's program, and run from
// there with execv: the same process id, arguments, environment and standard streams, only the executable's path is
// inside the bundle. (A colon in a file's name is what the Finder shows as a slash.) Launched from inside the bundle
// (that run, or the .app opened from the Finder or kept in the Dock) it runs as it is.
#pragma once
#include <string>

namespace tpw::launcher {

// macOS, outside a bundle, for a run that opens a window: makes or refreshes the bundle beside the program and runs the
// program from it; returns only when it could not (`note` says why; the run goes on as it is). Inside a bundle already,
// for a run with none of a window's work (--version, --help, --check-updates, --import), or not on macOS, returns at
// once with `note` empty. On success the run that follows logs the note it left in TPW_APP_BUNDLE_NOTE (TakeBundleNote).
bool RunFromAppBundle(int argc, char** argv, std::string& note);
// The note RunFromAppBundle's execv left for the run that followed, once; empty when there was none.
std::string TakeBundleNote();

// The folder holding the bare program (SDL_GetBasePath's folder, with its trailing separator): for a program inside a
// bundle, the folder that holds the .app, where the install script's files are (launcher/ui/updater.h FindInstall).
std::string ProgramFolder();

}  // namespace tpw::launcher
