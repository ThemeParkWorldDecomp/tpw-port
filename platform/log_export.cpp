// log_export.cpp: the About box's Export logs button (log_export.h).
#include "log_export.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

#include <fcntl.h>
#ifdef _WIN32
// Windows: only this file and log_file.cpp include <windows.h>. The Downloads folder is a Known Folder
// (SHGetKnownFolderPath), which the player may have moved (OneDrive, another drive); its id is written out here so that
// no GUID library is linked. The copy is made with the C runtime's _wopen and _write (the path is UTF-16, so a user name
// with accents or spaces works), as the log file itself is.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <io.h>
#include <sys/stat.h>
#include <shellapi.h>
#else
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

#include "log_file.h"

namespace fs = std::filesystem;

namespace tpw {

fs::path DownloadsFolderFor(bool windows, const fs::path& knownFolder, const fs::path& home, const fs::path& userProfile) {
    if (windows) {
        if (!knownFolder.empty()) return knownFolder;
        return userProfile.empty() ? fs::path() : userProfile / "Downloads";
    }
    return home.empty() ? fs::path() : home / "Downloads";
}

fs::path DownloadsFolder() {
#ifdef _WIN32
    // {374DE290-123F-4565-9164-39C4925E467B}, FOLDERID_Downloads.
    static const GUID kDownloadsId = {0x374DE290, 0x123F, 0x4565, {0x91, 0x64, 0x39, 0xC4, 0x92, 0x5E, 0x46, 0x7B}};
    fs::path known;
    PWSTR wide = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(kDownloadsId, 0, nullptr, &wide)) && wide != nullptr) known = fs::path(wide);
    CoTaskMemFree(wide);
    const wchar_t* profile = _wgetenv(L"USERPROFILE");   // the wide variable: a user name with accents survives
    return DownloadsFolderFor(true, known, fs::path(), profile != nullptr ? fs::path(profile) : fs::path());
#else
    const char* home = std::getenv("HOME");
    return DownloadsFolderFor(false, fs::path(), home != nullptr ? fs::path(home) : fs::path(), fs::path());
#endif
}

std::wstring ExplorerSelectArguments(const fs::path& file) {
    std::wstring path = file.wstring();
    for (wchar_t& c : path)
        if (c == L'/') c = L'\\';
    return L"/select,\"" + path + L"\"";
}

bool RevealFile(const fs::path& file) {
#ifdef _WIN32
    // ShellExecuteW answers a number above 32 when it started. Explorer takes the whole argument text as one string.
    const std::wstring arguments = ExplorerSelectArguments(file);
    const HINSTANCE started = ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(started) > 32;
#elif defined(__APPLE__)
    // /usr/bin/open -R reveals the file in the Finder with it selected. It returns at once, so it is waited for (no zombie).
    const std::string path = file.string();
    char* const arguments[] = {const_cast<char*>("open"), const_cast<char*>("-R"), const_cast<char*>(path.c_str()), nullptr};
    pid_t child = 0;
    if (posix_spawn(&child, "/usr/bin/open", nullptr, nullptr, arguments, environ) != 0) return false;
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#else
    (void)file;
    return false;
#endif
}

std::string ExportedLogName(std::time_t when, int copy) {
    std::tm local{};
    localtime_r(&when, &local);
    char name[64];
    std::strftime(name, sizeof name, "STP-TPW Launcher log %Y-%m-%d %H-%M", &local);
    std::string text = name;
    if (copy > 1) text += " (" + std::to_string(copy) + ")";
    return text + ".txt";
}

namespace {

LogExportResult Failed(const std::string& message) {
    LogExportResult result;
    result.message = message;
    return result;
}

// Opens a new file for writing, failing when the name is taken (errno EEXIST). -1 on failure.
int CreateNew(const fs::path& path) {
#ifdef _WIN32
    return _wopen(path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_NOINHERIT | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    return open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
#endif
}

// All of `bytes`, or false (errno set).
bool WriteAll(int fd, const std::string& bytes) {
    size_t done = 0;
    while (done < bytes.size()) {
#ifdef _WIN32
        const int wrote = _write(fd, bytes.data() + done, static_cast<unsigned>(bytes.size() - done));
#else
        const ssize_t wrote = write(fd, bytes.data() + done, bytes.size() - done);
        if (wrote < 0 && errno == EINTR) continue;
#endif
        if (wrote <= 0) return false;
        done += static_cast<size_t>(wrote);
    }
    return true;
}

}  // namespace

LogExportResult ExportLog(const fs::path& logsFolder, const fs::path& downloads, std::time_t when,
                          const ScrubIdentity* identity) {
    const fs::path source = NewestLog(logsFolder);
    if (source.empty()) return Failed("There is no launcher log to export.");
    std::error_code ec;
    if (downloads.empty() || !fs::is_directory(downloads, ec)) return Failed("The Downloads folder was not found.");

    // The log is open for appending by this run, so it is read like any reader would (never copied by the system's copy,
    // which on Windows refuses a file another handle has open for writing). All of it is read before anything is made.
    std::string bytes;
    {
        std::ifstream in(source, std::ios::binary);
        if (!in) return Failed("Could not read the launcher log.");
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (in.bad()) return Failed("Could not read the launcher log.");
    }
    ScrubIdentity own;
    if (identity == nullptr) identity = gScrubIdentity.load();
    if (identity == nullptr) {
        own = MakeScrubIdentity(CollectScrubInputs());
        identity = &own;
    }
    bytes = Scrub(identity, bytes);

    for (int copy = 1; copy < 100; ++copy) {
        const fs::path target = downloads / ExportedLogName(when, copy);
        const int fd = CreateNew(target);
        if (fd < 0) {
            if (errno == EEXIST) continue;
            return Failed(std::string("Could not save to the Downloads folder: ") + std::strerror(errno));
        }
        const bool written = WriteAll(fd, bytes);
        const int why = errno;
#ifdef _WIN32
        const bool closed = _close(fd) == 0;
#else
        const bool closed = close(fd) == 0;
#endif
        if (!written || !closed) {
            fs::remove(target, ec);   // our own half-written file, made a moment ago
            return Failed(std::string("Could not save to the Downloads folder: ") + std::strerror(written ? errno : why));
        }
        LogExportResult result;
        result.ok = true;
        result.message = "Saved to Downloads: " + target.filename().string();
        result.file = target;
        result.folder = downloads;
        return result;
    }
    return Failed("Could not save to the Downloads folder: every file name is taken.");
}

}  // namespace tpw
