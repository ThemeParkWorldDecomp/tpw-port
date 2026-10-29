// log_export.cpp: the About box's Export logs button (log_export.h).
#include "log_export.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

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

std::string ExportedLogName(const std::string& version, std::time_t when, int copy) {
    std::tm local{};
    localtime_r(&when, &local);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H-%M", &local);
    std::string text = "STP-TPW Launcher ";
    if (!version.empty()) text += "v" + version + " ";
    text += std::string("logs ") + stamp;
    if (copy > 1) text += " (" + std::to_string(copy) + ")";
    return text + ".txt";
}

namespace {

// launcher-YYYYMMDD-HHMMSS.log, or with a -N before .log (log_file.cpp's rule).
bool IsLauncherLog(const std::string& name) {
    return name.size() > 13 && name.compare(0, 9, "launcher-") == 0 && name.compare(name.size() - 4, 4, ".log") == 0;
}

struct Found {
    fs::path path;
    fs::file_time_type time;
};

// Newest first: the later time, then (same time) the later name, as NewestLog.
bool Newer(const Found& a, const Found& b) {
    if (a.time != b.time) return a.time > b.time;
    const std::string sa = a.path.stem().string(), sb = b.path.stem().string();
    return sa > sb;
}

Found Stat(const fs::path& path) {
    std::error_code ec;
    fs::file_time_type time = fs::last_write_time(path, ec);
    if (ec) time = fs::file_time_type::min();
    return {path, time};
}

// "2026-10-25 10:11:00", the local time of a file's last change.
std::string LocalTime(fs::file_time_type time) {
    if (time == fs::file_time_type::min()) return std::string();
    const auto system = std::chrono::system_clock::now() +
                        std::chrono::duration_cast<std::chrono::system_clock::duration>(time - fs::file_time_type::clock::now());
    const std::time_t when = std::chrono::system_clock::to_time_t(system);
    std::tm local{};
    localtime_r(&when, &local);
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%d %H:%M:%S", &local);
    return text;
}

}  // namespace

std::vector<fs::path> LogsToExport(const fs::path& logsFolder, fs::file_time_type now) {
    std::error_code ec;
    std::vector<Found> logs;
    for (fs::directory_iterator it(logsFolder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code typeError;
        if (IsLauncherLog(it->path().filename().string()) && it->is_regular_file(typeError)) logs.push_back(Stat(it->path()));
    }
    std::sort(logs.begin(), logs.end(), Newer);
    const fs::file_time_type since = now - std::chrono::hours(kExportRecentHours);
    std::vector<Found> chosen;
    for (size_t i = 0; i < logs.size() && chosen.size() < kLogFilesKept; ++i)
        if (i < kExportNewestLogs || logs[i].time >= since) chosen.push_back(logs[i]);
    // What the processes print on Windows, beside the logs folder (host_win.cpp LogToFile).
    for (const char* name : {"launcher.log", "game.log"}) {
        const fs::path capture = logsFolder.parent_path() / name;
        std::error_code typeError, sizeError;
        if (fs::is_regular_file(capture, typeError) && fs::file_size(capture, sizeError) > 0 && !sizeError)
            chosen.push_back(Stat(capture));
    }
    std::sort(chosen.begin(), chosen.end(), Newer);
    std::vector<fs::path> paths;
    for (const Found& found : chosen) paths.push_back(found.path);
    return paths;
}

namespace {

// "8 MB", or the bytes of a smaller limit (the tests').
std::string LimitWords(size_t limit) {
    if (limit >= 1000000 && limit % 1000000 == 0) return std::to_string(limit / 1000000) + " MB";
    return std::to_string(limit) + " bytes";
}

// What the list says of a log: nothing when it is whole, else what was left out.
std::string ListNote(int state, size_t limit) {
    if (state == 1) return "; its beginning is left out (the " + LimitWords(limit) + " limit)";
    if (state == 2) return "; left out (the " + LimitWords(limit) + " limit)";
    return std::string();
}

std::string Rule(const std::string& title) {
    std::string line = "==================== " + title + " ";
    if (line.size() < 100) line.append(100 - line.size(), '=');
    return line + "\n";
}

std::string Intro(const std::string& firstLine, const std::string& exported, const std::vector<ExportedLog>& logs,
                  const std::vector<int>& states, size_t limit) {
    std::string text = firstLine + ": logs exported " + exported + "\n";
    text += "For a bug report. Your name, your computer's name and your home folder are replaced (<user>, <computer>, ~).\n";
    text += "This file holds " + std::to_string(logs.size()) + (logs.size() == 1 ? " log" : " logs") + ", newest first:\n";
    for (size_t i = 0; i < logs.size(); ++i) {
        const ExportedLog& log = logs[i];
        text += std::to_string(i + 1) + ". " + log.name + ": " + log.program;
        if (!log.started.empty()) text += "; started " + log.started;
        else if (!log.written.empty()) text += "; last written " + log.written;
        if (!log.ended.empty()) text += "; " + log.ended;
        text += ListNote(states[i], limit) + "\n";
    }
    return text + "\n";
}

std::string SectionHead(const ExportedLog& log, size_t number, size_t count) {
    std::string text = Rule(std::to_string(number) + " of " + std::to_string(count) + ": " + log.name);
    text += "program: " + log.program + "\n";
    if (!log.started.empty()) text += "started: " + log.started + "\n";
    if (!log.written.empty()) text += "last written: " + log.written + "\n";
    if (!log.ended.empty()) text += "ended: " + log.ended + "\n";
    text += std::string(100, '-') + "\n";
    return text;
}

std::string LeftOutLine(size_t bytes, size_t limit) {
    return "[... " + std::to_string(bytes) + " bytes of this log's earlier lines are left out, to keep this file under " +
           LimitWords(limit) + " ...]\n";
}

}  // namespace

std::string ExportText(const std::string& firstLine, const std::string& exported, const std::vector<ExportedLog>& logs,
                       size_t limit) {
    // Each log is whole (0), keeps the end of its body (1) or is left out (2). The room is reckoned with the longest
    // list and the longest "left out" line, so the text never passes the limit.
    const size_t count = logs.size();
    std::vector<int> states(count, 0);
    std::vector<size_t> keep(count, 0);
    const size_t listMost = Intro(firstLine, exported, logs, std::vector<int>(count, 1), limit).size();
    const size_t markMost = LeftOutLine(static_cast<size_t>(-1), limit).size();
    size_t room = limit > listMost ? limit - listMost : 0;
    bool full = false;
    for (size_t i = 0; i < count; ++i) {
        const ExportedLog& log = logs[i];
        const size_t fixed = SectionHead(log, i + 1, count).size() + log.head.size() + markMost + 2;
        if (!full && fixed + log.body.size() <= room) {
            room -= fixed + log.body.size();
            keep[i] = log.body.size();
            continue;
        }
        // The oldest content goes first: this log keeps the end of its body when a useful part fits, and every older
        // log is left out.
        if (!full && room > fixed + 256) {
            states[i] = 1;
            keep[i] = room - fixed;
            room = 0;
        } else {
            states[i] = 2;
        }
        full = true;
    }

    std::string text = Intro(firstLine, exported, logs, states, limit);
    for (size_t i = 0; i < count; ++i) {
        if (states[i] == 2) continue;
        const ExportedLog& log = logs[i];
        text += SectionHead(log, i + 1, count);
        text += log.head;
        if (!log.head.empty() && log.head.back() != '\n') text += '\n';
        size_t from = log.body.size() - keep[i];
        if (from > 0) {   // start at a line's beginning
            const size_t newline = log.body.find('\n', from - 1);
            from = newline == std::string::npos ? log.body.size() : newline + 1;
        }
        if (log.omitted + from > 0) text += LeftOutLine(log.omitted + from, limit);
        text.append(log.body, from, std::string::npos);
        if (from < log.body.size() && log.body.back() != '\n') text += '\n';
        text += '\n';
    }
    return text;
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

// `length` bytes of `in` from `offset`, or fewer at its end.
std::string ReadAt(std::ifstream& in, uintmax_t offset, size_t length) {
    std::string bytes(length, '\0');
    in.clear();
    in.seekg(static_cast<std::streamoff>(offset));
    in.read(&bytes[0], static_cast<std::streamsize>(length));
    bytes.resize(static_cast<size_t>(in.gcount()));
    return bytes;
}

// The text of the line of `text` that starts with `key`, the last one when `last` (empty: none).
std::string LineStarting(const std::string& text, const std::string& key, bool last) {
    size_t at = std::string::npos;
    if (last) {
        at = text.rfind("\n" + key);
        if (at != std::string::npos) ++at;
        else if (text.compare(0, key.size(), key) == 0) at = 0;
    } else if (text.compare(0, key.size(), key) == 0) {
        at = 0;
    } else {
        at = text.find("\n" + key);
        if (at != std::string::npos) ++at;
    }
    if (at == std::string::npos) return std::string();
    const size_t end = text.find('\n', at);
    std::string line = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

// Sim Theme Park or Theme Park World, from a play: line or --play's edition; empty when neither says.
std::string Edition(const std::string& play, const std::string& args) {
    if (play.compare(0, 20, "play: Sim Theme Park") == 0 || args.find(" --play us") != std::string::npos) return "Sim Theme Park";
    if (play.compare(0, 22, "play: Theme Park World") == 0 || args.find(" --play eur") != std::string::npos) return "Theme Park World";
    return std::string();
}

// Reads one log for the export, at most `limit` bytes of it: a longer file keeps its header lines and its end. False when
// it cannot be read.
bool ReadLog(const fs::path& path, size_t limit, const ScrubIdentity* identity, ExportedLog& log) {
    std::error_code ec;
    const uintmax_t size = fs::file_size(path, ec);
    if (ec) return false;
    // A log is open for appending while its program runs, so it is read like any reader would (never copied by the
    // system's copy, which on Windows refuses a file another handle has open for writing).
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    log.name = path.filename().u8string();
    const bool launcherLog = IsLauncherLog(path.filename().string());
    std::string start = ReadAt(in, 0, static_cast<size_t>(std::min<uintmax_t>(size, 4096)));
    size_t headEnd = 0;
    if (launcherLog) {   // the header: up to and with the "log: " line
        const std::string logLine = LineStarting(start, "log: ", false);
        if (!logLine.empty()) {
            const size_t newline = start.find('\n', start.find("\n" + logLine) + 1);
            headEnd = newline == std::string::npos ? start.size() : newline + 1;
        }
    }
    std::string head = start.substr(0, headEnd);
    std::string body;
    if (size <= limit) {
        body = ReadAt(in, headEnd, static_cast<size_t>(size - headEnd));
    } else {
        const uintmax_t from = std::max<uintmax_t>(headEnd, size - limit);
        body = ReadAt(in, from, limit);
        const size_t newline = body.find('\n');   // from a line's beginning
        const size_t skip = newline == std::string::npos ? body.size() : newline + 1;
        body.erase(0, skip);
        log.omitted = static_cast<size_t>(from - headEnd) + skip;
    }
    if (in.bad()) return false;
    log.head = Scrub(identity, head);
    log.body = Scrub(identity, body);

    std::error_code timeError;
    const fs::file_time_type written = fs::last_write_time(path, timeError);
    if (!timeError) log.written = LocalTime(written);
    if (!launcherLog) {
        log.program = log.name == "game.log" ? "what the game printed (Windows: game.log, the latest game run)"
                                             : "what the launcher printed (Windows: launcher.log, the latest launcher run)";
        return true;
    }
    const std::string date = LineStarting(log.head, "date: ", false);
    if (!date.empty()) log.started = date.substr(6);
    const std::string args = LineStarting(log.head, "args: ", false);
    const std::string play = LineStarting(log.body, "play: ", false);
    const std::string edition = Edition(play, args);
    if (args.find(" --from-launcher") != std::string::npos) {
        log.program = "the game" + (edition.empty() ? std::string() : " (" + edition + ")") + ", in its own process";
    } else if (args.find(" --play") != std::string::npos) {
        log.program = "the launcher, playing" + (edition.empty() ? std::string(" a game") : " " + edition);
    } else if (!play.empty() && play.find("handed to") != std::string::npos) {
        log.program = "the launcher (its Play started the game's own process, whose log is another file)";
    } else if (!play.empty()) {
        log.program = "the launcher, then" + (edition.empty() ? std::string(" the game") : " " + edition) + " in the same process";
    } else {
        log.program = "the launcher";
    }
    log.ended = LineStarting(log.body, "exit: ", true);
    if (log.ended.empty()) {
        std::error_code same;
        const std::string running = LogFilePath();
        log.ended = !running.empty() && fs::equivalent(path, fs::path(running), same)
                        ? "still running (the launcher that made this file)"
                        : "no exit line (it was stopped, or ended before it could write one)";
    }
    return true;
}

}  // namespace

LogExportResult ExportLogs(const fs::path& logsFolder, const fs::path& downloads, std::time_t when,
                           const ExportProgram& program, const ScrubIdentity* identity) {
    const std::vector<fs::path> paths = LogsToExport(logsFolder, fs::file_time_type::clock::now());
    bool anyLauncherLog = false;
    for (const fs::path& path : paths) anyLauncherLog = anyLauncherLog || IsLauncherLog(path.filename().string());
    if (!anyLauncherLog) return Failed("There is no launcher log to export.");
    std::error_code ec;
    if (downloads.empty() || !fs::is_directory(downloads, ec)) return Failed("The Downloads folder was not found.");

    ScrubIdentity own;
    if (identity == nullptr) identity = gScrubIdentity.load();
    if (identity == nullptr) {
        own = MakeScrubIdentity(CollectScrubInputs());
        identity = &own;
    }
    // Everything is read before anything is made. A log that cannot be read is named in the list, not dropped silently.
    std::vector<ExportedLog> logs;
    size_t read = 0;
    for (const fs::path& path : paths) {
        ExportedLog log;
        if (ReadLog(path, kExportLimitBytes, identity, log)) {
            ++read;
        } else {
            log = ExportedLog{};
            log.name = path.filename().u8string();
            log.program = "could not be read";
        }
        logs.push_back(std::move(log));
    }
    if (read == 0) return Failed("Could not read the launcher log.");
    std::tm local{};
    localtime_r(&when, &local);
    char exported[32];
    std::strftime(exported, sizeof exported, "%Y-%m-%d %H:%M:%S", &local);
    const std::string firstLine = program.firstLine.empty() ? std::string("STP/TPW Launcher") : program.firstLine;
    const std::string bytes = ExportText(firstLine, exported, logs);

    for (int copy = 1; copy < 100; ++copy) {
        const fs::path target = downloads / ExportedLogName(program.version, when, copy);
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
        result.message = "Saved to Downloads: " + target.filename().u8string();
        result.file = target;
        result.folder = downloads;
        return result;
    }
    return Failed("Could not save to the Downloads folder: every file name is taken.");
}

}  // namespace tpw
