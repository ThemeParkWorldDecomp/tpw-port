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
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace tpw::launcher {

namespace {

const char* const kNoteVariable = "TPW_APP_BUNDLE_NOTE";

#if defined(__APPLE__)
namespace fs = std::filesystem;

const char* const kBundleName = "STP:TPW Launcher.app";   // the Finder shows "STP/TPW Launcher.app"

// The runs that open no window: Cmd-Tab and the Dock never see them, so they stay where they are.
bool WindowlessRun(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--version") == 0 || std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0 ||
            std::strcmp(arg, "--check-updates") == 0 || std::strcmp(arg, "--import") == 0)
            return true;
    }
    return false;
}

// The program's own path, with links resolved.
std::string ExecutablePath() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1, 0);
    if (size == 0 || _NSGetExecutablePath(buffer.data(), &size) != 0) return "";
    char resolved[PATH_MAX];
    if (realpath(buffer.data(), resolved) == nullptr) return buffer.data();
    return resolved;
}

bool InsideBundle(const std::string& path) { return path.find("/Contents/MacOS/") != std::string::npos; }

// The Info.plist the build embedded in the program (CMakeLists.txt, -sectcreate __TEXT __info_plist).
std::string EmbeddedPlist() {
    unsigned long size = 0;
    const uint8_t* data = getsectiondata(&_mh_execute_header, "__TEXT", "__info_plist", &size);
    if (data == nullptr || size == 0) return "";
    return std::string(reinterpret_cast<const char*>(data), size);
}

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

// Makes `app` (the .app folder) hold the program at `exe`, or brings it up to date. `made` says what was done.
bool KeepBundle(const fs::path& exe, const fs::path& app, const std::string& plist, fs::path& program, std::string& made, std::string& why) {
    std::error_code ec;
    const fs::path contents = app / "Contents", macos = contents / "MacOS";
    const bool existed = fs::is_directory(macos, ec);
    fs::create_directories(macos, ec);
    if (ec) {
        why = "cannot make " + macos.string() + ": " + ec.message();
        return false;
    }
    if (!KeepFile(contents / "Info.plist", plist, why)) return false;
    if (!KeepFile(contents / "PkgInfo", "APPL????", why)) return false;
    program = macos / exe.filename();
    if (SameFile(exe, program)) {
        made = existed ? "kept" : "made";
        return true;
    }
    fs::remove(program, ec);
    if (link(exe.c_str(), program.c_str()) == 0) {
        made = existed ? "brought up to date" : "made";
        return true;
    }
    // no hard links on this volume: a copy of the program instead
    fs::copy_file(exe, program, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        why = "cannot link or copy the program into " + macos.string() + ": " + ec.message();
        return false;
    }
    made = existed ? "brought up to date (a copy of the program: no hard links here)" : "made (a copy of the program: no hard links here)";
    return true;
}
#endif

}  // namespace

bool RunFromAppBundle(int argc, char** argv, std::string& note) {
    note.clear();
#if defined(__APPLE__)
    if (WindowlessRun(argc, argv)) return false;
    const std::string exePath = ExecutablePath();
    if (exePath.empty()) {
        note = "not from the bundle: the program's own path is unknown";
        return false;
    }
    if (InsideBundle(exePath)) return false;
    const std::string plist = EmbeddedPlist();
    if (plist.empty()) {
        note = "not from the bundle: the program has no Info.plist embedded";
        return false;
    }
    const fs::path exe = exePath, app = exe.parent_path() / kBundleName;
    fs::path program;
    std::string made, why;
    if (!KeepBundle(exe, app, plist, program, made, why)) {
        note = "not from the bundle (" + why + "); the Dock names this run after the program's folder";
        return false;
    }
    std::vector<char*> args;
    std::string first = program.string();
    args.push_back(first.data());
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
    args.push_back(nullptr);
    const std::string left = "running from " + app.string() + " (" + made + "; the Dock names it STP/TPW Launcher)";
    setenv(kNoteVariable, left.c_str(), 1);
    execv(first.c_str(), args.data());
    unsetenv(kNoteVariable);
    note = "not from the bundle: cannot run " + first + ": " + std::strerror(errno);
    return false;
#else
    (void)argc;
    (void)argv;
    return false;
#endif
}

std::string TakeBundleNote() {
    const char* left = std::getenv(kNoteVariable);
    if (left == nullptr) return "";
    std::string note = left;
#if !defined(_WIN32)
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
