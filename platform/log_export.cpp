// log_export.cpp: the About box's Export logs button (log_export.h).
#include "log_export.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
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
    text += "This file holds " + std::to_string(logs.size()) + (logs.size() == 1 ? " run" : " runs") +
            " of the launcher's log, newest first:\n";
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
    return "[... " + std::to_string(bytes) + " bytes of this run's earlier lines are left out, to keep this file under " +
           LimitWords(limit) + " ...]\n";
}

}  // namespace

std::string ExportText(const std::string& firstLine, const std::string& exported, const std::vector<ExportedLog>& logs,
                       size_t limit) {
    // Each run is whole (0), keeps the end of its body (1) or is left out (2). The room is reckoned with the longest
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
        // The oldest content goes first: this run keeps the end of its body when a useful part fits, and every older
        // run is left out.
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

// The run an exit line's tag names (" [run 12]" at its end); 0 when it has none.
int TaggedRun(const std::string& line) {
    const size_t open = line.rfind(" [run ");
    if (open == std::string::npos || line.empty() || line.back() != ']') return 0;
    int run = 0;
    for (size_t i = open + 6; i + 1 < line.size(); ++i) {
        if (line[i] < '0' || line[i] > '9' || i > open + 14) return 0;
        run = run * 10 + (line[i] - '0');
    }
    return run;
}

// One run's section: where it starts, its number (0: the lines before the first run header).
struct Section {
    size_t start;
    int run;
};

// The program of a run, in words, from its header and its lines.
std::string ProgramOf(const std::string& headerLine, const std::string& head, const std::string& body) {
    if (headerLine.find("| from the old file launcher.log ") != std::string::npos)
        return "what the launcher printed on Windows (the old launcher.log)";
    if (headerLine.find("| from the old file game.log ") != std::string::npos)
        return "what the game's own process printed on Windows (the old game.log)";
    if (headerLine.empty()) return "lines from before the first run header";
    const std::string args = LineStarting(head, "args: ", false);
    const std::string play = LineStarting(body, "play: ", false);
    const std::string edition = Edition(play, args);
    if (args.find(" --from-launcher") != std::string::npos)
        return "the game" + (edition.empty() ? std::string() : " (" + edition + ")") + ", in its own process";
    if (args.find(" --play") != std::string::npos) return "the launcher, playing" + (edition.empty() ? std::string(" a game") : " " + edition);
    if (!play.empty() && play.find("handed to") != std::string::npos)
        return "the launcher (its Play started the game's own process, a run of its own after this one)";
    if (!play.empty()) return "the launcher, then" + (edition.empty() ? std::string(" the game") : " " + edition) + " in the same process";
    if (args.empty()) return "a run whose header lines are not there";
    return "the launcher";
}

}  // namespace

std::vector<ExportedLog> RunsOfLog(const std::string& text, int runningRun, size_t omitted) {
    // One pass over the lines: where each run starts, and the tagged exit lines.
    std::vector<Section> sections;
    std::map<int, std::string> exits;
    for (size_t at = 0; at < text.size();) {
        const size_t end = text.find('\n', at);
        const size_t next = end == std::string::npos ? text.size() : end + 1;
        std::string line = text.substr(at, (end == std::string::npos ? text.size() : end) - at);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (const int run = RunNumberOf(line); run > 0) {
            sections.push_back({at, run});
        } else {
            if (sections.empty()) sections.push_back({0, 0});
            if (line.compare(0, 6, "exit: ") == 0)
                if (const int run = TaggedRun(line); run > 0) exits[run] = line;
        }
        at = next;
    }
    std::vector<ExportedLog> runs;
    for (size_t i = sections.size(); i-- > 0;) {
        const size_t start = sections[i].start;
        const size_t end = i + 1 < sections.size() ? sections[i + 1].start : text.size();
        const std::string part = text.substr(start, end - start);
        const int run = sections[i].run;
        ExportedLog log;
        // The head: the run header line and the lines after it up to and with the "log: " line, when one is among the
        // first eight.
        size_t headEnd = 0;
        if (run > 0) {
            headEnd = part.find('\n');
            headEnd = headEnd == std::string::npos ? part.size() : headEnd + 1;
            size_t at = headEnd;
            for (int lines = 0; lines < 8 && at < part.size(); ++lines) {
                const size_t newline = part.find('\n', at);
                const size_t after = newline == std::string::npos ? part.size() : newline + 1;
                if (part.compare(at, 5, "log: ") == 0) {
                    headEnd = after;
                    break;
                }
                at = after;
            }
        }
        log.head = part.substr(0, headEnd);
        log.body = part.substr(headEnd);
        if (i == 0) log.omitted = omitted;
        std::string headerLine = run > 0 ? log.head.substr(0, log.head.find('\n')) : std::string();
        if (!headerLine.empty() && headerLine.back() == '\r') headerLine.pop_back();
        log.name = run > 0 ? "run " + std::to_string(run) : "before the first run";
        const size_t old = headerLine.find("| from the old file ");
        if (old != std::string::npos) {
            const size_t nameStart = old + 20;
            const size_t nameEnd = headerLine.find(" ====", nameStart);
            log.name += " (from the old file " + headerLine.substr(nameStart, nameEnd - nameStart) + ")";
        }
        log.program = ProgramOf(headerLine, log.head, log.body);
        const std::string date = LineStarting(log.head, "date: ", false);
        if (!date.empty()) log.started = date.substr(6);
        const size_t written = headerLine.find("| last written ");
        if (written != std::string::npos)
            log.written = headerLine.substr(written + 15, headerLine.find(" |", written + 15) - (written + 15));
        if (const auto tagged = exits.find(run); run > 0 && tagged != exits.end()) {
            log.ended = tagged->second;
        } else {
            // An old file's lines have no tag: their own last exit line.
            for (size_t at = 0; at < log.body.size();) {
                const size_t newline = log.body.find('\n', at);
                const size_t lineEnd = newline == std::string::npos ? log.body.size() : newline;
                if (log.body.compare(at, 6, "exit: ") == 0) {
                    std::string line = log.body.substr(at, lineEnd - at);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (TaggedRun(line) == 0) log.ended = line;
                }
                at = lineEnd + 1;
            }
        }
        if (log.ended.empty())
            log.ended = run > 0 && run == runningRun ? "still running (the launcher that exported this file)"
                                                     : "no exit line (it was stopped, or ended before it could write one)";
        runs.push_back(std::move(log));
    }
    return runs;
}

LogExportResult ExportLogs(const fs::path& logFile, const fs::path& downloads, std::time_t when, const ExportProgram& program,
                           const ScrubIdentity* identity) {
    std::error_code ec;
    const uintmax_t size = fs::file_size(logFile, ec);
    if (ec || size == 0) return Failed("There is no launcher log to export.");
    if (downloads.empty() || !fs::is_directory(downloads, ec)) return Failed("The Downloads folder was not found.");

    ScrubIdentity own;
    if (identity == nullptr) identity = gScrubIdentity.load();
    if (identity == nullptr) {
        own = MakeScrubIdentity(CollectScrubInputs());
        identity = &own;
    }
    // The file is open for appending while the launcher (and the game's own process) run, so it is read like any reader
    // would (never copied by the system's copy, which on Windows refuses a file another handle has open for writing).
    // Everything is read before anything is made.
    std::ifstream in(logFile, std::ios::binary);
    if (!in) return Failed("Could not read the launcher log.");
    const uintmax_t from = size > 4 * kExportLimitBytes ? size - 4 * kExportLimitBytes : 0;
    std::string text(static_cast<size_t>(size - from), '\0');
    in.seekg(static_cast<std::streamoff>(from));
    in.read(&text[0], static_cast<std::streamsize>(text.size()));
    if (in.bad()) return Failed("Could not read the launcher log.");
    text.resize(static_cast<size_t>(std::max<std::streamsize>(in.gcount(), 0)));
    size_t omitted = static_cast<size_t>(from);
    if (from > 0) {   // from a line's start
        const size_t newline = text.find('\n');
        const size_t skip = newline == std::string::npos ? text.size() : newline + 1;
        text.erase(0, skip);
        omitted += skip;
    }
    std::error_code same;
    const std::string running = LogFilePath();
    const bool ours = !running.empty() && fs::equivalent(logFile, fs::path(running), same);
    const std::vector<ExportedLog> logs = RunsOfLog(Scrub(identity, text), ours ? LogRunNumber() : 0, omitted);
    if (logs.empty()) return Failed("There is no launcher log to export.");
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
