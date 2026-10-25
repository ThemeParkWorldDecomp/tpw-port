// log_tests.cpp: the launcher's log file (platform/log_file.h), in a temporary folder: the file's name, its header,
// the lines it takes and the ten-file rotation. No window and no game.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "log_export.h"
#include "log_file.h"
#include "platform.h"

namespace fs = std::filesystem;

namespace {

int gFailures = 0;
int gChecks = 0;

void Check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::fprintf(stderr, "FAILED: %s\n", what.c_str());
    }
}

std::vector<std::string> Lines(const fs::path& file) {
    std::ifstream in(file);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) lines.push_back(line);
    return lines;
}

std::set<std::string> Names(const fs::path& folder) {
    std::set<std::string> names;
    for (const auto& entry : fs::directory_iterator(folder)) names.insert(entry.path().filename().string());
    return names;
}

bool StartsWith(const std::string& text, const std::string& start) { return text.compare(0, start.size(), start) == 0; }

void Touch(const fs::path& file) { std::ofstream(file) << "old run\n"; }

}  // namespace

int main() {
#ifdef _WIN32
    // Windows: no mkdtemp and no /tmp. A new folder in the user's temporary folder, named by the time and a number;
    // create_directory is false when the name is taken, and the next number is tried.
    std::error_code tempError;
    const fs::path temp = fs::temp_directory_path(tempError);
    fs::path made;
    for (int n = 0; n < 100 && made.empty() && !tempError; ++n) {
        const fs::path candidate =
            temp / ("tpw-log-tests-" + std::to_string(static_cast<long long>(std::time(nullptr))) + "-" + std::to_string(n));
        if (fs::create_directory(candidate, tempError)) made = candidate;
    }
    if (made.empty()) {
        std::fprintf(stderr, "no temporary folder: %s\n", tempError ? tempError.message().c_str() : "every name was taken");
        return 1;
    }
#else
    char pattern[] = "/tmp/tpw-log-tests-XXXXXX";
    const char* made = mkdtemp(pattern);
    if (made == nullptr) {
        std::perror("mkdtemp");
        return 1;
    }
#endif
    const fs::path root = made;
    const fs::path folder = root / "logs";   // OpenLogFile makes it

    // The name holds the local time.
    std::tm when{};
    when.tm_year = 2026 - 1900;
    when.tm_mon = 9;
    when.tm_mday = 22;
    when.tm_hour = 19;
    when.tm_min = 46;
    when.tm_sec = 29;
    when.tm_isdst = -1;
    Check(tpw::LogFileName(std::mktime(&when)) == "launcher-20261022-194629.log", "LogFileName: launcher-YYYYMMDD-HHMMSS.log");

    // Twelve older runs and three files that are not the launcher's logs.
    fs::create_directories(folder);
    for (int i = 1; i <= 12; ++i) {
        char name[64];
        std::snprintf(name, sizeof name, "launcher-202601%02d-120000.log", i);
        Touch(folder / name);
    }
    Touch(folder / "notes.txt");
    Touch(folder / "launcher-notes.txt");
    Touch(folder / "other-20260101-120000.log");

    const char* argv[] = {"/build/tpw_launcher", "--mute", "--data", "/Users/a b/STP-TPW Launcher", ""};
    std::string err;
    const bool opened = tpw::OpenLogFile(folder, "STP/TPW Launcher 0.0.2 (build: test)", 5, argv, err);
    Check(opened, "OpenLogFile: " + err);
    if (!opened) return 1;
    const fs::path path = tpw::LogFilePath();
    Check(path.parent_path() == folder, "the log file is in the folder given");
    Check(StartsWith(path.filename().string(), "launcher-") && path.extension() == ".log", "the log file's name");

    // Rotation: thirteen launcher-*.log files, the ten newest kept (the new one among them), nothing else touched.
    const std::set<std::string> names = Names(folder);
    int logs = 0;
    for (const std::string& name : names)
        if (StartsWith(name, "launcher-") && fs::path(name).extension() == ".log") ++logs;
    Check(logs == 10, "10 launcher-*.log files are kept, not " + std::to_string(logs));
    Check(names.count(path.filename().string()) == 1, "the new log file is kept");
    Check(names.count("launcher-20260101-120000.log") == 0 && names.count("launcher-20260102-120000.log") == 0 &&
              names.count("launcher-20260103-120000.log") == 0,
          "the three oldest runs are removed");
    Check(names.count("launcher-20260104-120000.log") == 1 && names.count("launcher-20260112-120000.log") == 1,
          "the nine newest older runs are kept");
    Check(names.count("notes.txt") == 1 && names.count("launcher-notes.txt") == 1 &&
              names.count("other-20260101-120000.log") == 1,
          "files that are not launcher-*.log are left alone");

    // Lines: the platform log (whatever logToStderr says), the raw lines and the file-only ones, each as it comes.
    tpw::config().logToStderr = false;
    tpw::Log("game folder: %s", "/games/tpw");
    tpw::config().logToStderr = true;
    tpw::LogRawF("launcher: %d of %d stand-ins were reached (%ld calls in all)\n", 0, 158, 0L);
    tpw::LogFileF("exit: code %d", 0);

    const std::vector<std::string> lines = Lines(path);
    Check(lines.size() == 9, "6 header lines and 3 logged lines, not " + std::to_string(lines.size()));
    if (lines.size() == 9) {
        Check(lines[0] == "STP/TPW Launcher 0.0.2 (build: test)", "header 1: the launcher's name, version and build");
        // "date: 2026-10-22T20:02:17-03:00 (ADT)": the time, then the zone's offset
        Check(StartsWith(lines[1], "date: 20") && lines[1].find('T') == 16 && lines[1].find_first_of("+-", 25) == 25 &&
                  lines[1].size() >= 31 && lines[1][28] == ':',
              "header 2: the ISO local time with its zone: " + lines[1]);
#if defined(__APPLE__)
        Check(StartsWith(lines[2], "os: macOS ") && lines[2].back() == ')' && lines[2].find("unknown") == std::string::npos,
              "header 3: the macOS version and build: " + lines[2]);
        Check(StartsWith(lines[3], "hardware: ") && lines[3].find(" cores, ") != std::string::npos &&
                  lines[3].find(" GB RAM") != std::string::npos && lines[3].find("unknown") == std::string::npos,
              "header 4: model, architecture, cpu, cores and memory: " + lines[3]);
#else
        Check(StartsWith(lines[2], "os: "), "header 3: the system: " + lines[2]);
        Check(StartsWith(lines[3], "hardware: "), "header 4: the hardware: " + lines[3]);
#endif
        Check(lines[4] == "args: /build/tpw_launcher --mute --data \"/Users/a b/STP-TPW Launcher\" \"\"",
              "header 5: the command line, quoted where it has to be: " + lines[4]);
        Check(lines[5] == "log: " + path.string(), "header 6: the file's own path");
        Check(lines[6] == "platform: game folder: /games/tpw", "a platform line, with logToStderr off: " + lines[6]);
        Check(lines[7] == "launcher: 0 of 158 stand-ins were reached (0 calls in all)", "a raw line, one newline");
        Check(lines[8] == "exit: code 0", "a file-only line");
    }

    // A second run in the same second gets -2 (or the next second's name); by name it sorts as the newer one.
    const std::string first = path.string();
    Check(tpw::OpenLogFile(folder, "second", 1, argv, err), "a second OpenLogFile: " + err);
    const std::string second = tpw::LogFilePath();
    Check(second != first && fs::exists(first) && fs::exists(second), "the second run has a file of its own");
    Check(fs::path(second).stem().string() > fs::path(first).stem().string(), "the second run's name sorts after the first's");
    tpw::RemoveOldLogs(folder, 1);
    Check(fs::exists(second) && !fs::exists(first), "RemoveOldLogs keeps the newest by name");
    tpw::CloseLogFile();
    Check(tpw::LogFilePath().empty() && tpw::LogFileDescriptor() < 0, "CloseLogFile");
    tpw::LogFileF("nothing: no file is open");   // must not crash

    // A folder that cannot be made: false and why.
    Touch(root / "a-file");
    Check(!tpw::OpenLogFile(root / "a-file" / "logs", "x", 1, argv, err) && !err.empty(), "OpenLogFile on a file's path fails");


    // ---- Export logs (platform/log_export.h)
    {
        // The Downloads rule, a pure function: the Known Folder first on Windows, then %USERPROFILE%\Downloads; $HOME/Downloads
        // elsewhere; nothing to go on gives an empty path.
        Check(tpw::DownloadsFolderFor(false, "", "/Users/a b", "") == fs::path("/Users/a b") / "Downloads",
              "Downloads: HOME/Downloads, with a space");
        Check(tpw::DownloadsFolderFor(false, "/known", "", "/profile").empty(), "Downloads: no HOME, nothing (not Windows)");
        Check(tpw::DownloadsFolderFor(true, "D:/Moved/Downloads", "/home", "/profile") == fs::path("D:/Moved/Downloads"),
              "Downloads: the Known Folder wins on Windows");
        Check(tpw::DownloadsFolderFor(true, "", "/home", "/profile") == fs::path("/profile") / "Downloads",
              "Downloads: USERPROFILE/Downloads when the Known Folder is not there");
        Check(tpw::DownloadsFolderFor(true, "", "/home", "").empty(), "Downloads: nothing on Windows without either");
        const fs::path accents = fs::u8path("/Users/Ren\xC3\xA9" "e \xC3\x85ngstr\xC3\xB6m");
        Check(tpw::DownloadsFolderFor(false, "", accents, "") == accents / "Downloads", "Downloads: accents in the user's name");

        // DownloadsFolder() reads the environment: HOME (POSIX) or USERPROFILE (Windows, when the Known Folder is not asked;
        // there the Known Folder is asked and only checked to be an absolute path).
        const fs::path fakeHome = root / fs::u8path("home dir \xC3\xA9");
        fs::create_directories(fakeHome / "Downloads");
#ifdef _WIN32
        Check(tpw::DownloadsFolder().empty() || tpw::DownloadsFolder().is_absolute(),
              "DownloadsFolder() on Windows is an absolute path (the player may have moved the folder)");
#else
        const char* oldHome = std::getenv("HOME");
        const std::string savedHome = oldHome != nullptr ? oldHome : "";
        setenv("HOME", fakeHome.c_str(), 1);
        Check(tpw::DownloadsFolder() == fakeHome / "Downloads", "DownloadsFolder() is HOME/Downloads: " + tpw::DownloadsFolder().string());
        unsetenv("HOME");
        Check(tpw::DownloadsFolder().empty(), "DownloadsFolder() without HOME is empty");
        if (oldHome != nullptr) setenv("HOME", savedHome.c_str(), 1);
#endif

        // Explorer's /select argument: quoted, backslashes (also from a Unix-style path), accents and spaces kept.
        Check(tpw::ExplorerSelectArguments(fs::path(L"C:\\Users\\Ren\u00E9e a\\logs\\launcher-1.log")) ==
                  L"/select,\"C:\\Users\\Ren\u00E9e a\\logs\\launcher-1.log\"",
              "Explorer arguments: a Windows path, quoted");
        Check(tpw::ExplorerSelectArguments(fs::u8path("D:/My Logs/\xC3\xA9/a.log")) == L"/select,\"D:\\My Logs\\\u00E9\\a.log\"",
              "Explorer arguments: slashes become backslashes");

#ifdef __APPLE__
        // The Finder call is started and waited for without showing anything when the file is not there: open -R refuses.
        Check(!tpw::RevealFile(root / "no such file.log"), "RevealFile: a missing file is not shown");
#endif

        // The name: no colon, the minutes, then " (2)" for a second copy.
        std::tm tmWhen{};
        tmWhen.tm_year = 126;
        tmWhen.tm_mon = 9;
        tmWhen.tm_mday = 25;
        tmWhen.tm_hour = 10;
        tmWhen.tm_min = 12;
        tmWhen.tm_isdst = -1;
        const std::time_t when = std::mktime(&tmWhen);
        Check(tpw::ExportedLogName(when, 1) == "STP-TPW Launcher log 2026-10-25 10-12.txt",
              "exported name: " + tpw::ExportedLogName(when, 1));
        Check(tpw::ExportedLogName(when, 2) == "STP-TPW Launcher log 2026-10-25 10-12 (2).txt", "exported name, copy 2");

        // A logs folder with three runs: the newest by name is the one copied, with its bytes (binary-safe: a CR, a NUL and
        // a high byte) and the original left where it is.
        const fs::path logs = root / fs::u8path("export logs \xC3\xA9") / "logs";
        const fs::path out = root / fs::u8path("Downloads \xC3\xA9 x");
        fs::create_directories(logs);
        fs::create_directories(out);
        Check(tpw::NewestLog(logs).empty() && tpw::NewestLog(root / "no such folder").empty(), "NewestLog: none");
        const std::string oldBytes = "older run\n", newBytes = std::string("newest run\r\n\0\xFF end\n", 19);
        std::ofstream(logs / "launcher-20261024-090000.log", std::ios::binary) << oldBytes;
        std::ofstream(logs / "launcher-20261025-101100.log", std::ios::binary) << "second\n";
        std::ofstream(logs / "launcher-20261025-101100-2.log", std::ios::binary).write(newBytes.data(), newBytes.size());
        std::ofstream(logs / "notes.txt") << "not a log\n";
        std::ofstream(logs / "launcher-99999999-999999.txt") << "not a log either\n";
        // By modification time first, then (same time) by name; non-log files and folders never count, however new.
        const auto now = fs::file_time_type::clock::now();
        const auto stamp = [&](const char* name, int minutesAgo) {
            fs::last_write_time(logs / name, now - std::chrono::minutes(minutesAgo));
        };
        fs::create_directories(logs / "launcher-20270101-000000.log");   // a folder with a log's name
        stamp("launcher-20261024-090000.log", 30);
        stamp("launcher-20261025-101100.log", 5);
        stamp("launcher-20261025-101100-2.log", 20);
        stamp("notes.txt", 0);
        stamp("launcher-99999999-999999.txt", 0);
        Check(tpw::NewestLog(logs) == logs / "launcher-20261025-101100.log", "NewestLog: the latest modification time wins over the name");
        stamp("launcher-20261024-090000.log", 1);
        Check(tpw::NewestLog(logs) == logs / "launcher-20261024-090000.log", "NewestLog: an older name with a newer time wins");
        for (const char* name : {"launcher-20261024-090000.log", "launcher-20261025-101100.log", "launcher-20261025-101100-2.log"})
            fs::last_write_time(logs / name, now - std::chrono::minutes(10));
        Check(tpw::NewestLog(logs) == logs / "launcher-20261025-101100-2.log", "NewestLog: the same time goes by name, -2 after the plain one");
        Check(tpw::NewestLog(logs / "notes.txt").empty(), "NewestLog: a file is not a logs folder");

        const tpw::LogExportResult first = tpw::ExportLog(logs, out, when);
        Check(first.ok && first.message == "Saved to Downloads: STP-TPW Launcher log 2026-10-25 10-12.txt",
              "export: saved, with the message: " + first.message);
        Check(first.file == out / "STP-TPW Launcher log 2026-10-25 10-12.txt" && first.folder == out, "export: where it went");
        const auto slurp = [](const fs::path& file) {
            std::ifstream in(file, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        };
        Check(slurp(first.file) == newBytes && newBytes.size() == 19, "export: the copied bytes equal the source");
        Check(slurp(logs / "launcher-20261025-101100-2.log") == newBytes && fs::exists(logs / "launcher-20261024-090000.log"),
              "export: the original is left as it was");

        // The name is taken: " (2)", " (3)"; the earlier copies are not touched.
        std::ofstream(first.file, std::ios::binary) << "someone edited this\n";
        const tpw::LogExportResult second = tpw::ExportLog(logs, out, when);
        Check(second.ok && second.file == out / "STP-TPW Launcher log 2026-10-25 10-12 (2).txt", "export: a taken name gets (2)");
        const tpw::LogExportResult third = tpw::ExportLog(logs, out, when);
        Check(third.ok && third.file == out / "STP-TPW Launcher log 2026-10-25 10-12 (3).txt", "export: and then (3)");
        Check(slurp(first.file) == "someone edited this\n" && slurp(second.file) == newBytes, "export: nothing was overwritten");
        size_t filesBefore = Names(out).size();
        Check(filesBefore == 3, "export: three files in Downloads");

        // Failures write nothing and say why.
        const tpw::LogExportResult noLogs = tpw::ExportLog(root / "no such folder", out, when);
        Check(!noLogs.ok && noLogs.message == "There is no launcher log to export." && noLogs.file.empty() && Names(out).size() == filesBefore,
              "export: a missing logs folder: " + noLogs.message);
        fs::create_directories(root / "empty logs");
        Check(!tpw::ExportLog(root / "empty logs", out, when).ok && Names(out).size() == filesBefore, "export: a logs folder without logs");
        const tpw::LogExportResult noDownloads = tpw::ExportLog(logs, root / "no such Downloads", when);
        Check(!noDownloads.ok && noDownloads.message == "The Downloads folder was not found." &&
                  !fs::exists(root / "no such Downloads"),
              "export: a missing Downloads folder is not made: " + noDownloads.message);
        Check(!tpw::ExportLog(logs, fs::path(), when).ok, "export: no Downloads folder at all");
        Touch(root / "downloads-is-a-file");
        Check(!tpw::ExportLog(logs, root / "downloads-is-a-file", when).ok && fs::is_regular_file(root / "downloads-is-a-file"),
              "export: Downloads that is a file");
#ifndef _WIN32
        // A Downloads folder that cannot be written (not for root, which ignores the mode).
        const fs::path locked = root / "locked Downloads";
        fs::create_directories(locked);
        fs::permissions(locked, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
        if (geteuid() != 0) {
            const tpw::LogExportResult denied = tpw::ExportLog(logs, locked, when);
            Check(!denied.ok && StartsWith(denied.message, "Could not save to the Downloads folder: ") && Names(locked).empty(),
                  "export: an unwritable Downloads folder: " + denied.message);
        }
        fs::permissions(locked, fs::perms::owner_all, fs::perm_options::replace);
#endif
    }

    std::error_code ec;
    fs::remove_all(root, ec);
    std::printf("%d of %d log file checks passed\n", gChecks - gFailures, gChecks);
    return gFailures == 0 ? 0 : 1;
}
