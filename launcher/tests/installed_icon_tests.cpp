// installed_icon_tests.cpp: the launcher's icon where the installer put it (launcher/installed_icon.h), in a scratch
// folder: which application in an Applications folder is the installer's for this launcher (install.sh's script, an
// application linked to the launcher; never another install's or one the installer did not make), the Start menu
// shortcut's icon file, and the icon written only when it changed, the .app's time moved as install.sh's touch moves it.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include "installed_icon.h"

using namespace tpw::launcher;
namespace fs = std::filesystem;

namespace {

int gFailures = 0;

void Check(bool ok, const std::string& what) {
    if (ok) return;
    ++gFailures;
    std::printf("FAILED: %s\n", what.c_str());
}

void Write(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

std::string Read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

#if !defined(_WIN32)
std::string Plist(bool icon) {
    return std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\">\n<dict>\n") +
           "\t<key>CFBundleExecutable</key>\n\t<string>STP-TPW-Launcher</string>\n" +
           (icon ? "\t<key>CFBundleIconFile</key>\n\t<string>icon</string>\n" : "") + "</dict>\n</plist>\n";
}

// install.sh's application for the install `dir`, as write_app writes it (the program's lines that matter here)
fs::path ScriptApp(const fs::path& apps, const std::string& dir, bool mark, bool icon) {
    const fs::path app = apps / kInstallerAppName;
    std::string quoted;
    for (char c : dir) quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
    Write(app / "Contents" / "MacOS" / "STP-TPW-Launcher",
          std::string("#!/bin/sh\n") + (mark ? kInstallerAppMark : "# something else") + "\n# Starts the launcher.\ndir='" +
              quoted + "'\ncd \"$dir\" && nohup \"$dir/build/tpw_launcher\" >/dev/null 2>&1 </dev/null &\n");
    Write(app / "Contents" / "Info.plist", Plist(icon));
    return app;
}

#endif

// an install folder, DIR/build/tpw_launcher
fs::path Install(const fs::path& dir) {
    Write(dir / "build" / "tpw_launcher", "launcher");
    return dir / "build";
}

#if !defined(_WIN32)
// The tests of the macOS application install.sh makes: its name has a ':' in it (Finder's '/'), which Windows cannot hold.
void FindsTheApp(const fs::path& scratch) {
    const fs::path build = Install(scratch / "it's mine");   // a ' in the folder's name, as install.sh quotes it
    const fs::path other = Install(scratch / "another");
    const fs::path apps = scratch / "Applications", home = scratch / "home" / "Applications";
    const fs::path app = ScriptApp(apps, (scratch / "it's mine").string(), true, true);
    Check(InstallerApp({apps.string()}, build.string()) == app.string(), "install.sh's application for this install is found");
    Check(InstallerApp({apps.string()}, build.string() + "/") == app.string(), "the build folder with a trailing slash");
    Check(InstallerApp({"", (scratch / "none").string(), apps.string()}, build.string()) == app.string(),
          "a missing or empty Applications folder is passed over");
    Check(InstallerApp({apps.string()}, other.string()).empty(), "another install's application is not this launcher's");
    Check(InstallerApp({apps.string()}, "").empty(), "no build folder: no application");
    // the one in the player's own Applications, when the first folder's is another install's
    const fs::path mine = ScriptApp(home, (scratch / "another").string(), true, true);
    Check(InstallerApp({apps.string(), home.string()}, other.string()) == mine.string(), "the second folder's application");
    // a program of that name the installer did not make
    const fs::path foreign = scratch / "Foreign";
    ScriptApp(foreign, (scratch / "it's mine").string(), false, true);
    Check(InstallerApp({foreign.string()}, build.string()).empty(), "an application without the installer's mark is not ours");
    // the link form: the application's program a link to DIR/build/tpw_launcher
    const fs::path linked = scratch / "Linked";
    Write(linked / kInstallerAppName / "Contents" / "Info.plist", Plist(true));
    std::error_code ec;
    fs::create_symlink(build / "tpw_launcher", linked / kInstallerAppName / "Contents" / "MacOS" / "STP-TPW-Launcher", ec);
    if (!ec) {   // (Windows makes links only with developer mode on)
        Check(InstallerApp({linked.string()}, build.string()) == (linked / kInstallerAppName).string(),
              "an application whose program is a link to this launcher");
        Check(InstallerApp({linked.string()}, other.string()).empty(), "a link to another install's launcher");
    }
}

void RefreshesTheIcns(const fs::path& scratch) {
    const fs::path apps = scratch / "Refresh";
    const fs::path app = ScriptApp(apps, (scratch / "x").string(), true, true);
    const fs::path icns = app / "Contents" / "Resources" / "icon.icns";
    Write(icns, "the stand-in");
    std::error_code ec;
    const auto old = fs::file_time_type::clock::now() - std::chrono::hours(48);
    fs::last_write_time(app, old, ec);
    std::string why;
    const std::vector<uint8_t> game = {'g', 'a', 'm', 'e'};
    Check(RefreshAppIcns(app.string(), game, why) == IconRefresh::Written, "the game's icon is written: " + why);
    Check(Read(icns) == "game", "icon.icns holds the game's icon");
    Check(!fs::exists(icns.string() + ".part"), "no icon.icns.part is left");
    Check(fs::last_write_time(app, ec) > old + std::chrono::hours(24), "the .app is touched after a write");
    fs::last_write_time(app, old, ec);
    Check(RefreshAppIcns(app.string(), game, why) == IconRefresh::Kept, "the same icon again is kept");
    Check(fs::last_write_time(app, ec) == old, "the .app is not touched when nothing changed");
    Check(RefreshAppIcns(app.string(), {'n', 'e', 'w'}, why) == IconRefresh::Written && Read(icns) == "new",
          "a different icon replaces the one written before");
    Check(RefreshAppIcns(app.string(), {}, why) == IconRefresh::None && Read(icns) == "new", "no icon: nothing written");
    // install.sh made the application without an icon (no sips or iconutil): it is left so
    const fs::path bare = ScriptApp(scratch / "Bare", (scratch / "x").string(), true, false);
    Check(RefreshAppIcns(bare.string(), game, why) == IconRefresh::None, "an application with no icon of the installer's");
    Check(!fs::exists(bare / "Contents" / "Resources" / "icon.icns"), "nothing written into it");
    // a file in the way of the rename
    const fs::path stuck = ScriptApp(scratch / "Stuck", (scratch / "x").string(), true, true);
    fs::create_directories(stuck / "Contents" / "Resources" / "icon.icns" / "inside", ec);
    Check(RefreshAppIcns(stuck.string(), game, why) == IconRefresh::Failed && !why.empty(), "a write that fails says why");
    Check(!fs::exists(stuck / "Contents" / "Resources" / "icon.icns.part"), "and leaves no .part");
}
#endif

void ShortcutIcon(const fs::path& scratch) {
    const fs::path dir = scratch / "STP-TPW-Launcher";
    const fs::path build = Install(dir);
    Check(ShortcutIconFile(build.string()).empty(), "no icon.ico yet: none");
    Write(dir / "icon.ico", "ico");
    Check(ShortcutIconFile(build.string()) == (dir / "icon.ico").string(), "DIR/icon.ico for DIR/build");
    Check(ShortcutIconFile((build / "").string()) == (dir / "icon.ico").string(), "DIR/build/, as ProgramFolder gives it");
    Check(ShortcutIconFile(dir.string()).empty(), "a launcher outside a build folder has no shortcut icon");
    Check(ShortcutIconFile("").empty(), "no folder: none");
    // install.ps1's .ico written twice (std::rename refused the second on Windows: tpw-port issue #62)
    std::string why;
    Check(KeepIconFile((dir / "icon.ico").string(), {'o', 'n', 'e'}, why) == IconRefresh::Written, "icon.ico replaced: " + why);
    Check(KeepIconFile((dir / "icon.ico").string(), {'t', 'w', 'o'}, why) == IconRefresh::Written, "and again: " + why);
    Check(Read(dir / "icon.ico") == "two" && !fs::exists(dir / "icon.ico.part"), "the second icon, no .part");
    Check(KeepIconFile((dir / "icon.ico").string(), {'t', 'w', 'o'}, why) == IconRefresh::Kept, "the same icon is kept");
}

}  // namespace

int main() {
    std::error_code ec;
    const fs::path scratch =
        fs::temp_directory_path(ec) / ("tpw-installed-icon-" + std::to_string(std::time(nullptr)) + "-" + std::to_string(std::rand()));
    fs::create_directories(scratch, ec);
    if (ec) {
        std::printf("FAILED: cannot make the scratch folder %s: %s\n", scratch.string().c_str(), ec.message().c_str());
        return 1;
    }
#if !defined(_WIN32)
    FindsTheApp(scratch);
    RefreshesTheIcns(scratch);
#endif
    ShortcutIcon(scratch);
    fs::remove_all(scratch, ec);
    if (gFailures == 0) std::printf("installed_icon_tests: all passed\n");
    return gFailures == 0 ? 0 : 1;
}
