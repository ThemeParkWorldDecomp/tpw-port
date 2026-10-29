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
        // The argument was /Users/a b/STP-TPW Launcher: a home folder is ~ in the log (log_scrub.h).
        Check(lines[4] == "args: /build/tpw_launcher --mute --data \"~/STP-TPW Launcher\" \"\"",
              "header 5: the command line, quoted where it has to be: " + lines[4]);
        // The path is scrubbed like every line: on Windows the temporary folder is under the home, so it reads %USERPROFILE%.
        Check(lines[5] == "log: " + tpw::Scrub(tpw::gScrubIdentity.load(), path.string()),
              "header 6: the file's own path, scrubbed: " + lines[5]);
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

        // The name: the product, the version, no colon, the minutes, then " (2)" for a second copy.
        std::tm tmWhen{};
        tmWhen.tm_year = 126;
        tmWhen.tm_mon = 9;
        tmWhen.tm_mday = 25;
        tmWhen.tm_hour = 10;
        tmWhen.tm_min = 12;
        tmWhen.tm_isdst = -1;
        const std::time_t when = std::mktime(&tmWhen);
        Check(tpw::ExportedLogName("0.1.1-dev", when, 1) == "STP-TPW Launcher v0.1.1-dev logs 2026-10-25 10-12.txt",
              "exported name: " + tpw::ExportedLogName("0.1.1-dev", when, 1));
        Check(tpw::ExportedLogName("0.1.1", when, 2) == "STP-TPW Launcher v0.1.1 logs 2026-10-25 10-12 (2).txt", "exported name, copy 2");
        Check(tpw::ExportedLogName("", when, 1) == "STP-TPW Launcher logs 2026-10-25 10-12.txt", "exported name without a version");

        // NewestLog (Show logs): by modification time first, then (same time) by name; non-log files and folders never
        // count, however new.
        const fs::path logs = root / fs::u8path("export logs \xC3\xA9") / "logs";
        const fs::path out = root / fs::u8path("Downloads \xC3\xA9 x");
        fs::create_directories(logs);
        fs::create_directories(out);
        Check(tpw::NewestLog(logs).empty() && tpw::NewestLog(root / "no such folder").empty(), "NewestLog: none");
        std::ofstream(logs / "launcher-20261024-090000.log", std::ios::binary) << "older run\n";
        std::ofstream(logs / "launcher-20261025-101100.log", std::ios::binary) << "second\n";
        std::ofstream(logs / "launcher-20261025-101100-2.log", std::ios::binary) << "third\n";
        std::ofstream(logs / "notes.txt") << "not a log\n";
        std::ofstream(logs / "launcher-99999999-999999.txt") << "not a log either\n";
        const auto now = fs::file_time_type::clock::now();
        const auto stamp = [&](const fs::path& file, int minutesAgo) {
            fs::last_write_time(file, now - std::chrono::minutes(minutesAgo));
        };
        fs::create_directories(logs / "launcher-20270101-000000.log");   // a folder with a log's name
        stamp(logs / "launcher-20261024-090000.log", 30);
        stamp(logs / "launcher-20261025-101100.log", 5);
        stamp(logs / "launcher-20261025-101100-2.log", 20);
        stamp(logs / "notes.txt", 0);
        stamp(logs / "launcher-99999999-999999.txt", 0);
        Check(tpw::NewestLog(logs) == logs / "launcher-20261025-101100.log", "NewestLog: the latest modification time wins over the name");
        stamp(logs / "launcher-20261024-090000.log", 1);
        Check(tpw::NewestLog(logs) == logs / "launcher-20261024-090000.log", "NewestLog: an older name with a newer time wins");
        for (const char* name : {"launcher-20261024-090000.log", "launcher-20261025-101100.log", "launcher-20261025-101100-2.log"})
            stamp(logs / name, 10);
        Check(tpw::NewestLog(logs) == logs / "launcher-20261025-101100-2.log", "NewestLog: the same time goes by name, -2 after the plain one");
        Check(tpw::NewestLog(logs / "notes.txt").empty(), "NewestLog: a file is not a logs folder");

        // LogsToExport: the three newest whatever their age, the others of the last 24 hours, and launcher.log and game.log
        // beside the logs folder when they hold something; newest first.
        const fs::path pick = root / "pick" / "logs";
        fs::create_directories(pick);
        const auto put = [&](const fs::path& file, const std::string& text, int minutesAgo) {
            std::ofstream(file, std::ios::binary) << text;
            stamp(file, minutesAgo);
        };
        put(pick / "launcher-20261020-080000.log", "a\n", 60 * 24 * 7);   // a week old
        put(pick / "launcher-20261021-080000.log", "b\n", 60 * 24 * 6);
        put(pick / "launcher-20261022-080000.log", "c\n", 60 * 24 * 5);
        put(pick / "launcher-20261023-080000.log", "d\n", 60 * 24 * 4);
        put(pick / "notes.txt", "not a log\n", 0);
        fs::create_directories(pick / "launcher-20270101-000000.log");
        const auto names = [](const std::vector<fs::path>& paths) {
            std::string text;
            for (const fs::path& path : paths) text += (text.empty() ? "" : " ") + path.filename().string();
            return text;
        };
        Check(names(tpw::LogsToExport(pick, now)) ==
                  "launcher-20261023-080000.log launcher-20261022-080000.log launcher-20261021-080000.log",
              "LogsToExport: the three newest, however old: " + names(tpw::LogsToExport(pick, now)));
        for (int i = 10; i < 22; ++i) put(pick / ("launcher-20261027-" + std::to_string(i) + "0000.log"), "e\n", 60 * (30 - i));
        const std::vector<fs::path> recent = tpw::LogsToExport(pick, now);
        Check(recent.size() == tpw::kLogFilesKept && recent.front().filename() == "launcher-20261027-210000.log" &&
                  recent.back().filename() == "launcher-20261027-120000.log",
              "LogsToExport: every log of the last 24 hours, never more than the folder keeps: " + names(recent));
        Check(names(tpw::LogsToExport(pick, now + std::chrono::hours(24 * 30))) ==
                  "launcher-20261027-210000.log launcher-20261027-200000.log launcher-20261027-190000.log",
              "LogsToExport: a month later, the three newest still");
        put(pick.parent_path() / "game.log", "the game's stderr\n", 60 * 9 + 30);   // between the 20:00 and 21:00 runs
        put(pick.parent_path() / "launcher.log", "", 0);   // empty: left out
        const std::vector<fs::path> withCaptures = tpw::LogsToExport(pick, now);
        Check(withCaptures.size() == tpw::kLogFilesKept + 1 && withCaptures[1] == pick.parent_path() / "game.log",
              "LogsToExport: game.log beside the logs folder, in its place by time; an empty launcher.log left out: " +
                  names(withCaptures));
        Check(tpw::LogsToExport(root / "no such folder" / "logs", now).empty(), "LogsToExport: no folder, nothing");

        // ExportLogs, on what a Mac leaves after the game crashed and the player reopened the launcher: the launcher's
        // run that pressed Play, the game's own run that crashed, and the new launcher run; on Windows, game.log too.
        // Every byte is kept (a CR, a NUL and a high byte), the originals stay as they were.
        const fs::path mac = root / "mac" / "logs";
        fs::create_directories(mac);
        const std::string header = "STP/TPW Launcher v0.1.1-dev (build: abc1234)\ndate: 2026-10-25T10:05:00-03:00 (ADT)\n"
                                   "os: macOS 15.1\nhardware: Mac16,5, arm64\n";
        const std::string pressed = header + "args: tpw_launcher\nlog: ~/logs/launcher-20261025-100500.log\n"
            "play: Sim Theme Park, the US edition (us), from the launcher window's Play, handed to ~/b/Sim Theme Park.app\n"
            "exit: code 0\n";
        const std::string crashed = header + "args: \"~/b/Sim Theme Park.app/Contents/MacOS/Sim Theme Park\" --play us --from-launcher ~/b\n"
            "log: ~/logs/launcher-20261025-100502.log\nplay: Sim Theme Park, the US edition (us), as the game's own application\n" +
            std::string("binary \r\n\0\xFF end\n", 15) + "launcher: crashed (signal 11)\nexit: crashed (signal 11)\n";
        const std::string reopened = header + "args: tpw_launcher\nlog: ~/logs/launcher-20261025-101100.log\nwindow: shown\n";
        put(mac / "launcher-20261025-100500.log", pressed, 7);
        put(mac / "launcher-20261025-100502.log", crashed, 6);
        put(mac / "launcher-20261025-101100.log", reopened, 1);
        put(mac.parent_path() / "game.log", "Assertion failed: x\n", 6);
        tpw::ExportProgram program;
        program.version = "0.1.1-dev";
        program.firstLine = "STP/TPW Launcher v0.1.1-dev (build: abc1234)";
        const tpw::LogExportResult first = tpw::ExportLogs(mac, out, when, program);
        Check(first.ok && first.message == "Saved to Downloads: STP-TPW Launcher v0.1.1-dev logs 2026-10-25 10-12.txt",
              "export: saved, with the message: " + first.message);
        Check(first.file == out / "STP-TPW Launcher v0.1.1-dev logs 2026-10-25 10-12.txt" && first.folder == out, "export: where it went");
        const auto slurp = [](const fs::path& file) {
            std::ifstream in(file, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        };
        const std::string text = slurp(first.file);
        Check(StartsWith(text, "STP/TPW Launcher v0.1.1-dev (build: abc1234): logs exported 2026-10-25 10:12:00\n"),
              "export: the first line names the launcher and the time: " + text.substr(0, 100));
        Check(text.find("This file holds 4 logs, newest first:\n"
                        "1. launcher-20261025-101100.log: the launcher; started 2026-10-25T10:05:00-03:00 (ADT); no exit line") !=
                  std::string::npos,
              "export: the list starts with the newest run");
        Check(text.find("2. launcher-20261025-100502.log: the game (Sim Theme Park), in its own process; started "
                        "2026-10-25T10:05:00-03:00 (ADT); exit: crashed (signal 11)\n") != std::string::npos,
              "export: the game's crashed run, named as the game's");
        Check(text.find("3. game.log: what the game printed (Windows: game.log, the latest game run); last written ") != std::string::npos,
              "export: game.log, named");
        Check(text.find("4. launcher-20261025-100500.log: the launcher (its Play started the game's own process, whose log is "
                        "another file); started 2026-10-25T10:05:00-03:00 (ADT); exit: code 0\n") != std::string::npos,
              "export: the run that pressed Play");
        const size_t crashAt = text.find("==================== 2 of 4: launcher-20261025-100502.log ====");
        Check(crashAt != std::string::npos && text.find("program: the game (Sim Theme Park), in its own process\n", crashAt) != std::string::npos &&
                  text.find("ended: exit: crashed (signal 11)\n", crashAt) != std::string::npos && text.find(crashed, crashAt) != std::string::npos,
              "export: the crashed run's section holds its header and every byte of the log");
        Check(text.find(pressed) != std::string::npos && text.find(reopened) != std::string::npos &&
                  text.find("Assertion failed: x\n") != std::string::npos && text.find("[...") == std::string::npos,
              "export: every log whole, nothing left out");
        Check(text.find("=== 1 of 4") < text.find("=== 2 of 4") && text.find("=== 2 of 4") < text.find("=== 3 of 4") &&
                  text.find("=== 3 of 4") < text.find("=== 4 of 4"),
              "export: the sections in the list's order");
        Check(slurp(mac / "launcher-20261025-100502.log") == crashed && fs::exists(mac / "launcher-20261025-100500.log") &&
                  slurp(mac.parent_path() / "game.log") == "Assertion failed: x\n",
              "export: the originals are left as they were");

        // The name is taken: " (2)", " (3)"; the earlier copies are not touched.
        std::ofstream(first.file, std::ios::binary) << "someone edited this\n";
        const tpw::LogExportResult second = tpw::ExportLogs(mac, out, when, program);
        Check(second.ok && second.file == out / "STP-TPW Launcher v0.1.1-dev logs 2026-10-25 10-12 (2).txt", "export: a taken name gets (2)");
        const tpw::LogExportResult third = tpw::ExportLogs(mac, out, when, program);
        Check(third.ok && third.file == out / "STP-TPW Launcher v0.1.1-dev logs 2026-10-25 10-12 (3).txt", "export: and then (3)");
        Check(slurp(first.file) == "someone edited this\n" && slurp(second.file) == text, "export: nothing was overwritten");
        size_t filesBefore = Names(out).size();
        Check(filesBefore == 3, "export: three files in Downloads");

        // The game in the launcher's process (Windows before the game had its own, or --play): named so.
        const fs::path one = root / "one" / "logs";
        fs::create_directories(one);
        put(one / "launcher-20261025-100500.log", header + "args: tpw_launcher\nlog: x\n"
            "play: Theme Park World, the European edition (eur), from the launcher window's Play; game folder x\nexit: code 0\n", 2);
        put(one / "launcher-20261025-100600.log", header + "args: tpw_launcher --play eur\nlog: y\nexit: abort\n", 1);
        const tpw::LogExportResult inProcess = tpw::ExportLogs(one, out, when + 60, program);
        const std::string inProcessText = slurp(inProcess.file);
        Check(inProcess.ok && inProcessText.find("1. launcher-20261025-100600.log: the launcher, playing Theme Park World; started ") != std::string::npos &&
                  inProcessText.find("; exit: abort\n") != std::string::npos &&
                  inProcessText.find("2. launcher-20261025-100500.log: the launcher, then Theme Park World in the same process; ") != std::string::npos,
              "export: the game in the launcher's own process");
        fs::remove(inProcess.file);

        // A log far over the limit (a run that printed a line a frame): its header lines and its end, never more than 8 MB.
        const fs::path big = root / "big" / "logs";
        fs::create_directories(big);
        {
            std::ofstream file(big / "launcher-20261025-100000.log", std::ios::binary);
            file << header << "args: tpw_launcher\nlog: z\n";
            const std::string line(99, 'x');
            for (int i = 0; i < 90000; ++i) file << line << '\n';
            file << "the last line\nexit: crashed (signal 6)\n";
        }
        stamp(big / "launcher-20261025-100000.log", 3);
        put(big / "launcher-20261025-090000.log", header + "args: tpw_launcher\nlog: w\nan older run\n", 60);
        const tpw::LogExportResult large = tpw::ExportLogs(big, out, when + 120, program);
        const std::string largeText = slurp(large.file);
        Check(large.ok && largeText.size() <= tpw::kExportLimitBytes && largeText.size() > tpw::kExportLimitBytes - 2000,
              "export: a big log fills the file to just under 8 MB: " + std::to_string(largeText.size()));
        Check(largeText.find("args: tpw_launcher\nlog: z\n[... ") != std::string::npos &&
                  largeText.find(" bytes of this log's earlier lines are left out, to keep this file under 8 MB ...]\nxxx") != std::string::npos &&
                  largeText.find("the last line\nexit: crashed (signal 6)\n") != std::string::npos,
              "export: the big log keeps its header lines and its end, and says what was left out");
        Check(largeText.find("1. launcher-20261025-100000.log: the launcher; started 2026-10-25T10:05:00-03:00 (ADT); exit: crashed "
                             "(signal 6); its beginning is left out (the 8 MB limit)\n") != std::string::npos &&
                  largeText.find("2. launcher-20261025-090000.log: the launcher; started 2026-10-25T10:05:00-03:00 (ADT); no exit line "
                                 "(it was stopped, or ended before it could write one); left out (the 8 MB limit)\n") != std::string::npos &&
                  largeText.find("an older run") == std::string::npos,
              "export: the list says what was cut; the older run is left out");
        fs::remove(large.file);

        // ExportText's limit: the oldest logs go whole, then the oldest kept log's beginning (its header lines stay); the
        // newest are whole, and the text is never longer than the limit.
        {
            std::vector<tpw::ExportedLog> pieces(3);
            for (size_t i = 0; i < pieces.size(); ++i) {
                pieces[i].name = "launcher-2026102" + std::to_string(5 - i) + "-100000.log";
                pieces[i].program = "the launcher";
                pieces[i].ended = "exit: code 0";
                pieces[i].head = "header of " + std::to_string(i) + "\n";
                for (int k = 0; k < 200; ++k) pieces[i].body += "line " + std::to_string(k) + " of log " + std::to_string(i) + "\n";
            }
            const std::string whole = tpw::ExportText("first", "now", pieces, 100000);
            Check(whole.find("line 0 of log 2\n") != std::string::npos && whole.find("[...") == std::string::npos,
                  "ExportText: under the limit, all of it");
            const size_t limit = whole.size() - pieces[2].body.size() - 300;
            const std::string cut = tpw::ExportText("first", "now", pieces, limit);
            Check(cut.size() <= limit, "ExportText: never over the limit: " + std::to_string(cut.size()) + " of " + std::to_string(limit));
            Check(cut.find("line 0 of log 0\n") != std::string::npos && cut.find("line 199 of log 1\n") != std::string::npos,
                  "ExportText: the newest logs' ends are kept");
            Check(cut.find("line 0 of log 2\n") == std::string::npos && cut.find("header of 2") == std::string::npos &&
                      cut.find("3. launcher-20261023-100000.log: the launcher; exit: code 0; left out (the " + std::to_string(limit) +
                               " bytes limit)\n") != std::string::npos,
                  "ExportText: the oldest log is left out, and the list says so");
            Check(cut.find("2. launcher-20261024-100000.log: the launcher; exit: code 0; its beginning is left out") != std::string::npos &&
                      cut.find("header of 1\n[... ") != std::string::npos && cut.find("line 0 of log 1\n") == std::string::npos &&
                      cut.find(" bytes of this log's earlier lines are left out, to keep this file under " + std::to_string(limit) +
                               " bytes ...]\nline ") != std::string::npos,
                  "ExportText: the next oldest keeps its header lines and its end, from a line's start");
            const std::string tiny = tpw::ExportText("first", "now", pieces, 100);
            Check(tiny.find("left out (the 100 bytes limit)") != std::string::npos && tiny.find("====") == std::string::npos,
                  "ExportText: a limit too small for any log lists them all as left out");
        }

        // Failures write nothing and say why.
        const tpw::LogExportResult noLogs = tpw::ExportLogs(root / "no such folder", out, when, program);
        Check(!noLogs.ok && noLogs.message == "There is no launcher log to export." && noLogs.file.empty() && Names(out).size() == filesBefore,
              "export: a missing logs folder: " + noLogs.message);
        fs::create_directories(root / "empty" / "logs");
        put(root / "empty" / "game.log", "a game.log alone is not enough\n", 0);
        Check(!tpw::ExportLogs(root / "empty" / "logs", out, when, program).ok && Names(out).size() == filesBefore,
              "export: a logs folder without launcher logs");
        const tpw::LogExportResult noDownloads = tpw::ExportLogs(mac, root / "no such Downloads", when, program);
        Check(!noDownloads.ok && noDownloads.message == "The Downloads folder was not found." &&
                  !fs::exists(root / "no such Downloads"),
              "export: a missing Downloads folder is not made: " + noDownloads.message);
        Check(!tpw::ExportLogs(mac, fs::path(), when, program).ok, "export: no Downloads folder at all");
        Touch(root / "downloads-is-a-file");
        Check(!tpw::ExportLogs(mac, root / "downloads-is-a-file", when, program).ok && fs::is_regular_file(root / "downloads-is-a-file"),
              "export: Downloads that is a file");
#ifndef _WIN32
        // A Downloads folder that cannot be written (not for root, which ignores the mode).
        const fs::path locked = root / "locked Downloads";
        fs::create_directories(locked);
        fs::permissions(locked, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
        if (geteuid() != 0) {
            const tpw::LogExportResult denied = tpw::ExportLogs(mac, locked, when, program);
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
