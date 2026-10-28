// installed_icon.h: the launcher's icon where the installer put the launcher, kept up to date by the launcher itself.
// install.sh writes the application "STP:TPW Launcher.app" into Applications with an icon.icns made from the launcher's
// icon (THE APPLICATION), and install.ps1 the Start menu shortcut showing DIR\icon.ico (THE START MENU SHORTCUT); both
// only at an install, update or switch, so a game imported after the install left the launcher's stand-in in the
// Finder, Launchpad, a Dock tile kept there and the Start menu until the next update (reported by slappyhead on a Mac,
// 2026-10-27). So when the launcher's icon may have changed (an import ended, the data folder changed: app_icon.h
// RefreshInstalledIcon) it writes those files again itself: the application's icon.icns, then the .app touched as the
// installer touches it, so that the Finder reads the bundle again; DIR\icon.ico, which the shortcut names, and Explorer
// told. Only the installer's own application and shortcut file, and only those of the install the launcher runs from:
// another install's, or a program of the same name the installer did not make, is never written.
//
// Everything here but TellExplorer is plain and runs everywhere (launcher/tests/installed_icon_tests.cpp checks it); app_icon.cpp has
// the part that knows where the program is and what the icon is.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace tpw::launcher {

// The line install.sh writes into the application's program (APP_MARK), by which it knows the application as its own.
inline constexpr const char* kInstallerAppMark = "# tpw-port installer: the STP/TPW Launcher application";
// The application's name in Applications (install.sh APP_NAME; the Finder shows "STP/TPW Launcher.app").
inline constexpr const char* kInstallerAppName = "STP:TPW Launcher.app";
// The Start menu shortcut's name in the Programs folder (install.ps1 $ShortcutName).
inline constexpr const char* kShortcutName = "STP-TPW Launcher.lnk";

// The installer's application in the first of `appsFolders` that has one which starts the launcher of `buildFolder`
// (DIR/build): its program (Contents/MacOS/<the Info.plist's CFBundleExecutable>) is install.sh's script, with the mark
// and the line dir='DIR', or a link to DIR/build/tpw_launcher. Empty when there is none.
std::string InstallerApp(const std::vector<std::string>& appsFolders, const std::string& buildFolder);

// The icon file the Start menu shortcut shows for the launcher of `buildFolder`: DIR\icon.ico, when `buildFolder` is
// DIR\build and the file is there (install.ps1 wrote it). Empty otherwise.
std::string ShortcutIconFile(const std::string& buildFolder);

// What a refresh did.
enum class IconRefresh {
    None,      // nothing to refresh: the application has no icon of the installer's
    Kept,      // the file already held that icon
    Written,   // the file was written
    Failed,    // it could not be written (`why` says why)
};

// `bytes` as the file `path`, through `path`.part and a rename, when the file does not hold them already.
IconRefresh KeepIconFile(const std::string& path, const std::vector<uint8_t>& bytes, std::string& why);

// `icns` as the icon of the application `app` (Contents/Resources/icon.icns), when its Info.plist names that icon
// (install.sh names it only when it could make one); after a write the .app's time is set to now, as install.sh's
// touch does, so that the Finder, Launchpad and the Dock read the bundle again.
IconRefresh RefreshAppIcns(const std::string& app, const std::vector<uint8_t>& icns, std::string& why);

#if defined(_WIN32)
// Tells Explorer that the icon file `iconFile` changed, and the Start menu shortcut that shows it (in the Programs
// folder, or TPW_INSTALL_PROGRAMS in a test, as install.ps1 Get-ProgramsFolder), so that it draws them again.
void TellExplorer(const std::string& iconFile);
#endif

}  // namespace tpw::launcher
