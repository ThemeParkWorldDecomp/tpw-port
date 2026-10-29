// installed_icon.cpp: the launcher's icon where the installer put the launcher (installed_icon.h).
#include "installed_icon.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include "game_app.h"

#if defined(_WIN32)
// The Programs folder is a Known Folder (SHGetKnownFolderPath), its id written out as platform/log_export.cpp writes
// the Downloads folder's, so that no GUID library is linked; SHChangeNotify is shell32's.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace tpw::launcher {

namespace fs = std::filesystem;

namespace {

std::string ReadWhole(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return in ? std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()) : std::string();
}

bool Same(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    return fs::equivalent(a, b, ec) && !ec;
}

// DIR from the dir='DIR' line of install.sh's program (a ' in it written as '\''), or empty.
std::string ScriptDir(const std::string& script) {
    const std::string start = "\ndir='";
    const size_t at = script.find(start);
    if (at == std::string::npos) return "";
    const size_t from = at + start.size();
    const size_t end = script.find('\n', from);
    std::string quoted = script.substr(from, end == std::string::npos ? std::string::npos : end - from);
    if (quoted.empty() || quoted.back() != '\'') return "";
    quoted.pop_back();
    std::string dir;
    for (size_t i = 0; i < quoted.size(); ++i) {
        if (quoted.compare(i, 4, "'\\''") == 0) {
            dir += '\'';
            i += 3;
        } else {
            dir += quoted[i];
        }
    }
    return dir;
}

// Whether `app` is the installer's application for the launcher of `build`.
bool StartsLauncherOf(const fs::path& app, const fs::path& build) {
    std::error_code ec;
    if (!fs::is_directory(app, ec)) return false;
    const std::string named = PlistString(ReadWhole(app / "Contents" / "Info.plist"), "CFBundleExecutable");
    const fs::path program = app / "Contents" / "MacOS" / (named.empty() ? "STP-TPW-Launcher" : named);
    // a link to DIR/build/tpw_launcher: the program itself, from the folder it is in
    if (fs::is_symlink(program, ec)) {
        const fs::path target = fs::canonical(program, ec);
        return !ec && target.filename() == "tpw_launcher" && Same(target.parent_path(), build);
    }
    if (!fs::is_regular_file(program, ec)) return false;
    // install.sh's script, which names DIR
    const std::string script = ReadWhole(program);
    if (script.find(kInstallerAppMark) == std::string::npos) return false;
    const std::string dir = ScriptDir(script);
    return !dir.empty() && Same(fs::path(dir) / "build", build);
}

}  // namespace

std::string InstallerApp(const std::vector<std::string>& appsFolders, const std::string& buildFolder) {
    if (buildFolder.empty()) return "";
    const fs::path build = fs::u8path(buildFolder);
    for (const std::string& folder : appsFolders) {
        if (folder.empty()) continue;
        const fs::path app = fs::u8path(folder) / kInstallerAppName;
        if (StartsLauncherOf(app, build)) return app.u8string();
    }
    return "";
}

std::string ShortcutIconFile(const std::string& buildFolder) {
    fs::path build = fs::u8path(buildFolder);
    if (!build.has_filename()) build = build.parent_path();   // "DIR\build\", as ProgramFolder gives it
    if (build.filename() != "build") return "";
    const fs::path icon = build.parent_path() / "icon.ico";
    std::error_code ec;
    return fs::is_regular_file(icon, ec) ? icon.u8string() : "";
}

IconRefresh KeepIconFile(const std::string& path, const std::vector<uint8_t>& bytes, std::string& why) {
    const fs::path file = fs::u8path(path);
    const std::string text(bytes.begin(), bytes.end());
    std::error_code ec;
    if (fs::is_regular_file(file, ec) && ReadWhole(file) == text) return IconRefresh::Kept;
    fs::path part = file;
    part += ".part";
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        if (!out || !(out << text) || !out.flush()) {
            why = "cannot write " + part.u8string();
            out.close();
            fs::remove(part, ec);
            return IconRefresh::Failed;
        }
    }
    // std::filesystem's rename replaces the file on Windows too (std::rename would not: tpw-port issue #62)
    fs::rename(part, file, ec);
    if (ec) {
        why = "cannot replace " + file.u8string() + ": " + ec.message();
        fs::remove(part, ec);
        return IconRefresh::Failed;
    }
    return IconRefresh::Written;
}

IconRefresh RefreshAppIcns(const std::string& app, const std::vector<uint8_t>& icns, std::string& why) {
    const fs::path bundle = fs::u8path(app);
    if (icns.empty() || PlistString(ReadWhole(bundle / "Contents" / "Info.plist"), "CFBundleIconFile") != "icon") return IconRefresh::None;
    const IconRefresh done = KeepIconFile((bundle / "Contents" / "Resources" / "icon.icns").u8string(), icns, why);
    if (done != IconRefresh::Written) return done;
    // install.sh's touch: a changed folder, which the Finder and Launchpad read again
    std::error_code ec;
    fs::last_write_time(bundle, fs::file_time_type::clock::now(), ec);
    return IconRefresh::Written;
}

#if defined(_WIN32)
void TellExplorer(const std::string& iconFile) {
    const fs::path icon = fs::u8path(iconFile);
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, icon.c_str(), nullptr);
    fs::path programs;
    const wchar_t* test = _wgetenv(L"TPW_INSTALL_PROGRAMS");   // the wide variable: a user name with accents survives
    if (test != nullptr && *test != 0) {
        programs = fs::path(test);
    } else {
        // {A77F5D77-2E2B-44C3-A6A2-ABA601054A51}, FOLDERID_Programs.
        static const GUID kProgramsId = {0xA77F5D77, 0x2E2B, 0x44C3, {0xA6, 0xA2, 0xAB, 0xA6, 0x01, 0x05, 0x4A, 0x51}};
        PWSTR wide = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(kProgramsId, 0, nullptr, &wide)) && wide != nullptr) programs = fs::path(wide);
        CoTaskMemFree(wide);
    }
    if (programs.empty()) return;
    const fs::path shortcut = programs / kShortcutName;
    std::error_code ec;
    if (fs::is_regular_file(shortcut, ec)) SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, shortcut.c_str(), nullptr);
}
#endif

}  // namespace tpw::launcher
