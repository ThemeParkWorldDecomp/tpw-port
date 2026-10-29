// log_tests.cpp: the launcher's log file (platform/log_file.h), in a temporary folder: one file that every run adds to,
// its run headers, the cut to its newest 5 MB (at a run header, or at a line), the old one-file-per-run logs and Windows'
// launcher.log and game.log folded in, the lock that keeps a launch from cutting a file in use, and Export logs
// (platform/log_export.h) made from it. No window and no game.
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

#ifdef _WIN32
#include <process.h>   // _getpid
#else
#include <fcntl.h>
#include <sys/file.h>
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

std::string Slurp(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void Put(const fs::path& file, const std::string& text) { std::ofstream(file, std::ios::binary) << text; }

// How many lines of `lines` start with `start`.
int Count(const std::vector<std::string>& lines, const std::string& start) {
    int count = 0;
    for (const std::string& line : lines) count += StartsWith(line, start) ? 1 : 0;
    return count;
}

// The run header lines of `lines`, in order.
std::vector<std::string> Headers(const std::vector<std::string>& lines) {
    std::vector<std::string> headers;
    for (const std::string& line : lines)
        if (tpw::RunNumberOf(line) > 0) headers.push_back(line);
    return headers;
}

// `count` lines of 99 times `fill` (100 bytes each, with the newline).
std::string Filler(char fill, int count) {
    std::string text;
    const std::string line = std::string(99, fill) + "\n";
    text.reserve(line.size() * static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) text += line;
    return text;
}

long ProcessId() {
#ifdef _WIN32
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}

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
    const std::string firstLine = "STP/TPW Launcher 0.0.2 (build: test)";

    // ---- The run header line and its number (pure functions)
    {
        std::tm tmWhen{};
        tmWhen.tm_year = 126;
        tmWhen.tm_mon = 9;
        tmWhen.tm_mday = 28;
        tmWhen.tm_hour = 14;
        tmWhen.tm_min = 3;
        tmWhen.tm_sec = 22;
        tmWhen.tm_isdst = -1;
        const std::string line = tpw::RunHeaderLine(12, std::mktime(&tmWhen), firstLine, "macOS 26.1 (25B78)", "Sim Theme Park",
                                                    "the launcher", 4711);
        // "==== run 12 | 2026-10-28 14:03:22 -03:00 | ...": the local time and its zone's offset
        Check(StartsWith(line, "==== run 12 | 2026-10-28 14:03:22 ") && (line[34] == '+' || line[34] == '-') && line[37] == ':' &&
                  line.compare(40, std::string::npos, " | STP/TPW Launcher 0.0.2 (build: test) | macOS 26.1 (25B78) | Sim Theme "
                                                      "Park | the launcher | process 4711 ====") == 0,
              "RunHeaderLine: number, date, version, system, edition, program and process: " + line);
        Check(tpw::RunHeaderLine(3, std::mktime(&tmWhen), "x", "Windows 10.0 (build 26100)", "", "", 0).compare(39, std::string::npos,
                                                                                                         " | x | Windows 10.0 (build 26100) ====") == 0,
              "RunHeaderLine: an unknown edition and program are left out");
        Check(tpw::RunNumberOf(line) == 12 && tpw::RunNumberOf("==== run 7") == 7 && tpw::RunNumberOf("==== run 7 ====") == 7,
              "RunNumberOf: a header's number");
        Check(tpw::RunNumberOf("==== run x |") == 0 && tpw::RunNumberOf("==== run 12x |") == 0 && tpw::RunNumberOf(" ==== run 3 |") == 0 &&
                  tpw::RunNumberOf("==== run ") == 0 && tpw::RunNumberOf("platform: ==== run 3 |") == 0,
              "RunNumberOf: lines that are not headers");
        Check(tpw::HighestRunNumber("a\n==== run 3 | a\nb ==== run 99 |\n==== run 12 | c\n==== run 5 | d\n") == 12 &&
                  tpw::HighestRunNumber("") == 0 && tpw::HighestRunNumber("==== run 4 | at the start\n") == 4,
              "HighestRunNumber: of the header lines only");
    }

    // ---- A fresh file: the first run's header and the lines it takes
    const char* argv[] = {"/build/tpw_launcher", "--mute", "--data", "/Users/a b/STP-TPW Launcher", ""};
    std::string err;
    const bool opened = tpw::OpenLogFile(folder, firstLine, 5, argv, err);
    Check(opened, "OpenLogFile: " + err);
    if (!opened) return 1;
    const fs::path path = tpw::LogFilePath();
    Check(path == folder / "launcher.log" && std::string(tpw::kLogFileName) == "launcher.log", "the log file is logs/launcher.log");
    Check(tpw::LogRunNumber() == 1 && std::string(tpw::LogRunTag()) == " [run 1]", "the first run is run 1, tagged [run 1]");

    // Lines: the platform log (whatever logToStderr says), the raw lines and the file-only ones, each as it comes.
    tpw::config().logToStderr = false;
    tpw::Log("game folder: %s", "/games/tpw");
    tpw::config().logToStderr = true;
    tpw::LogRawF("launcher: %d of %d stand-ins were reached (%ld calls in all)\n", 0, 158, 0L);
    tpw::LogFileF("exit: code %d%s", 0, tpw::LogRunTag());

    std::vector<std::string> lines = Lines(path);
    Check(lines.size() == 9, "6 header lines and 3 logged lines, not " + std::to_string(lines.size()));
    if (lines.size() == 9) {
        Check(lines[0] == tpw::Scrub(tpw::gScrubIdentity.load(), lines[0]) && StartsWith(lines[0], "==== run 1 | 20") &&
                  lines[0].find(" | " + firstLine + " | " + lines[2].substr(4) + " | the launcher | process " +
                                std::to_string(ProcessId()) + " ====") != std::string::npos,
              "header 1: the run header line: " + lines[0]);
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
        Check(lines[8] == "exit: code 0 [run 1]", "a file-only line, with the run's tag");
    }

    // ---- A second run appends: run 2 under the first, the first run's lines kept; then a third, the game's own process.
    tpw::CloseLogFile();
    Check(tpw::LogFilePath().empty() && tpw::LogFileDescriptor() < 0 && tpw::LogRunNumber() == 0 && tpw::LogRunTag()[0] == '\0',
          "CloseLogFile");
    tpw::LogFileF("nothing: no file is open");   // must not crash
    const std::string afterFirst = Slurp(path);
    Check(tpw::OpenLogFile(folder, "second", 1, argv, err), "a second OpenLogFile: " + err);
    Check(tpw::LogFilePath() == path.string() && tpw::LogRunNumber() == 2 && std::string(tpw::LogRunTag()) == " [run 2]",
          "the second run writes the same file, as run 2");
    tpw::LogFileF("window: shown");
    const char* gameArgv[] = {"/b/Sim Theme Park", "--play", "us", "--from-launcher", "/b"};
    Check(tpw::OpenLogFile(folder, "third", 5, gameArgv, err), "the game's own process opens it too: " + err);
    tpw::LogFileF("play: Sim Theme Park, as the game's own application");
    tpw::CloseLogFile();
    const std::string afterThree = Slurp(path);
    lines = Lines(path);
    const std::vector<std::string> headers = Headers(lines);
    Check(StartsWith(afterThree, afterFirst), "the first run's lines are kept as they were");
    Check(headers.size() == 3 && StartsWith(headers[1], "==== run 2 | ") && headers[1].find(" | second | ") != std::string::npos &&
              headers[1].find(" | the launcher | process ") != std::string::npos,
          "run 2's header, after run 1's lines");
    Check(headers.size() == 3 && StartsWith(headers[2], "==== run 3 | ") &&
              headers[2].find(" | third | ") != std::string::npos &&
              headers[2].find(" | Sim Theme Park | the game's own process (from the launcher's Play) | ") != std::string::npos,
          "the game's own process: run 3, its edition and what it is");
    Check(Count(lines, "date: ") == 3 && Count(lines, "log: ") == 3 && lines.back() == "play: Sim Theme Park, as the game's own application",
          "each run has its header lines; the lines follow their run");
    Check(afterThree.find("log: cut ") == std::string::npos && afterThree.find("log: the old") == std::string::npos,
          "a small file with no old files: no note about the file");

    // A folder that cannot be made: false and why.
    Touch(root / "a-file");
    Check(!tpw::OpenLogFile(root / "a-file" / "logs", "x", 1, argv, err) && !err.empty(), "OpenLogFile on a file's path fails");

    // ---- TrimLog (a pure function)
    {
        const std::string run1 = "==== run 1 | a ====\n" + Filler('a', 10);
        const std::string run2 = "==== run 2 | b ====\n" + Filler('b', 10);
        const std::string run3 = "==== run 3 | c ====\n" + Filler('c', 10);
        const std::string three = run1 + run2 + run3;
        size_t cut = 99;
        Check(tpw::TrimLog(three, three.size(), &cut) == three && cut == 0, "TrimLog: a file that fits is kept whole");
        // The cut falls in run 2: runs 1 and 2 go whole, run 3 stays.
        Check(tpw::TrimLog(three, run3.size() + 50, &cut) == run3 && cut == run1.size() + run2.size(),
              "TrimLog: at the first run header in the newest bytes");
        Check(tpw::TrimLog(three, run2.size() + run3.size(), &cut) == run2 + run3 && cut == run1.size(),
              "TrimLog: a cut right at a header keeps that run");
        // The newest run alone is longer than what is kept: its header line, the cut line, its end from a line's start.
        const std::string big = "a line before any run\n" + run1 + Filler('z', 100);
        const size_t keep = 2000;
        const std::string kept = tpw::TrimLog(big, keep, &cut);
        const std::string cutLine = "bytes of this run's earlier lines were cut here: the log file keeps its newest 2000 bytes ...]\n";
        const size_t tailAt = kept.find(cutLine) + cutLine.size();
        Check(kept.size() <= keep && StartsWith(kept, "==== run 1 | a ====\n[... ") && kept.find(cutLine) != std::string::npos,
              "TrimLog: the newest run alone is longer: its header line and a line for the cut: " + kept.substr(0, 160));
        const std::string tail = kept.substr(tailAt);
        Check(!tail.empty() && big.compare(big.size() - tail.size(), tail.size(), tail) == 0 && big[big.size() - tail.size() - 1] == '\n' &&
                  kept.size() > keep - 300,
              "TrimLog: the run's end is kept, from a line's start, close to the limit");
        const size_t said = std::strtoul(kept.c_str() + kept.find("[... ") + 5, nullptr, 10);
        Check(said == big.size() - tail.size() - std::string("a line before any run\n").size() - std::string("==== run 1 | a ====\n").size(),
              "TrimLog: the cut line says how many of the run's bytes went: " + std::to_string(said));
        // No room for those two lines (a tiny limit): a line's start only.
        const std::string tiny = tpw::TrimLog(three, 100, &cut);
        Check(tiny == Filler('c', 1) && cut == three.size() - tiny.size() &&
                  tiny.find("[...") == std::string::npos,
              "TrimLog: a limit too small for the cut line cuts at a line");
        // A file with no header at all (a launcher before this one): the cut line, then the end.
        const std::string headless = Filler('h', 100);
        const std::string headlessKept = tpw::TrimLog(headless, 2000, &cut);
        Check(headlessKept.size() <= 2000 && StartsWith(headlessKept, "[... ") && cut > 0, "TrimLog: no header: the cut line and the end");
    }

    // ---- The old files: which names are folded in, and nothing else
    {
        Check(tpw::IsOldRunLogName("launcher-20261022-194629.log") && tpw::IsOldRunLogName("launcher-20261022-194629-2.log") &&
                  tpw::IsOldRunLogName("launcher-20261022-194629-99.log"),
              "IsOldRunLogName: launcher-YYYYMMDD-HHMMSS.log, and with -2 to -99");
        for (const char* name : {"launcher.log", "launcher-notes.log", "launcher-2026102-194629.log", "launcher-20261022-194629.txt",
                                 "launcher-20261022-194629.log.bak", "launcher-20261022-194629-0.log", "launcher-20261022-194629-02.log",
                                 "launcher-20261022-194629-100.log", "launcher-20261022-194629-.log", "launcher-2026102a-194629.log",
                                 "Launcher-20261022-194629.log", "xlauncher-20261022-194629.log", "launcher-20261022_194629.log"})
            Check(!tpw::IsOldRunLogName(name), std::string("IsOldRunLogName: not ") + name);
    }

    // ---- The first launch with this launcher: the old files folded in, oldest first, then removed
    {
        const fs::path data = root / "migrate";
        const fs::path logs = data / "logs";
        fs::create_directories(logs);
        const auto now = fs::file_time_type::clock::now();
        const auto put = [&](const fs::path& file, const std::string& text, int minutesAgo) {
            Put(file, text);
            fs::last_write_time(file, now - std::chrono::minutes(minutesAgo));
        };
        // A line the file already holds (a test's seed, or a file an older build wrote under the new name): kept first.
        put(logs / "launcher.log", "a log line\n", 0);
        const std::string oldHeader = "STP/TPW Launcher v0.1.0 (build: old)\ndate: 2026-10-20T08:00:00-03:00 (ADT)\nos: macOS 15.1\n"
                                      "hardware: Mac16,5\nargs: tpw_launcher\nlog: ~/logs/launcher-20261020-080000.log\n";
        put(logs / "launcher-20261020-080000.log", oldHeader + "the oldest run\nexit: code 0\n", 60);
        put(data / "game.log", "what the game printed\n", 50);
        put(logs / "launcher-20261021-080000.log", "STP/TPW Launcher v0.1.0 (build: old)\nthe second run\n", 40);
        put(logs / "launcher-20261021-080000-2.log", "no newline at the end", 30);
        put(data / "launcher.log", "what the launcher printed\n", 20);
        // What is not the launcher's own: left alone.
        for (const char* name : {"launcher-notes.log", "launcher-20261021-080000.log.bak", "notes.txt", "launcher-20261021-080000-0.log"})
            put(logs / name, "not a log of the launcher's\n", 10);
        put(data / "other.log", "not a log of the launcher's\n", 10);
        put(data / "launcher.ini", "[launcher]\n", 10);
        fs::create_directories(logs / "launcher-20270101-000000.log");   // a folder with a log's name
#ifndef _WIN32
        // A link with a log's name: the link is not followed, and the file it points to is not removed.
        put(root / "elsewhere.log", "a file somewhere else\n", 10);
        fs::create_symlink(root / "elsewhere.log", logs / "launcher-20261022-080000.log");
#endif
        const std::vector<fs::path> old = tpw::OldLogFiles(logs);
        std::string oldNames;
        for (const fs::path& file : old) oldNames += (oldNames.empty() ? "" : " ") + file.filename().string();
        Check(oldNames == "launcher-20261020-080000.log game.log launcher-20261021-080000.log launcher-20261021-080000-2.log launcher.log",
              "OldLogFiles: the five old files, oldest first: " + oldNames);
        Check(old.size() == 5 && old[1] == data / "game.log" && old[4] == data / "launcher.log",
              "OldLogFiles: Windows' two beside the logs folder, not the logs folder's own launcher.log");
        Check(tpw::OldLogFiles(root / "no such folder" / "logs").empty(), "OldLogFiles: no folder, nothing");

        const std::string folded = tpw::FoldedOldLog(logs / "launcher-20261020-080000.log", 4, tpw::kLogKeepBytes, nullptr);
        Check(folded == "==== run 4 | 2026-10-20T08:00:00-03:00 (ADT) | STP/TPW Launcher v0.1.0 (build: old) | from the old file "
                        "launcher-20261020-080000.log ====\n" + oldHeader + "the oldest run\nexit: code 0\n",
              "FoldedOldLog: a run header from the old file's own header, then every line: " + folded.substr(0, 200));
        const std::string game = tpw::FoldedOldLog(data / "game.log", 5, tpw::kLogKeepBytes, nullptr);
        Check(StartsWith(game, "==== run 5 | last written 20") &&
                  game.find(" | what the game's own process printed on Windows | from the old file game.log ====\nwhat the game printed\n") !=
                      std::string::npos,
              "FoldedOldLog: Windows' game.log, dated by its last change: " + game);
        const std::string ended = tpw::FoldedOldLog(logs / "launcher-20261021-080000-2.log", 6, tpw::kLogKeepBytes, nullptr);
        Check(ended.size() > 22 && ended.compare(ended.size() - 22, 22, "no newline at the end\n") == 0,
              "FoldedOldLog: a last line without a newline gets one");
        const std::string shortened = tpw::FoldedOldLog(logs / "launcher-20261020-080000.log", 7, 40, nullptr);
        Check(shortened.find(" | from the old file launcher-20261020-080000.log ====\n[... ") != std::string::npos &&
                  shortened.find(" bytes of this file's earlier lines were cut here: the log file keeps its newest 40 bytes ...]\n") !=
                      std::string::npos &&
                  shortened.find("STP/TPW Launcher v0.1.0") == std::string::npos &&
                  shortened.compare(shortened.size() - 13, 13, "exit: code 0\n") == 0,
              "FoldedOldLog: a file over the limit keeps its end, from a line's start: " + shortened);

        Check(tpw::OpenLogFile(logs, firstLine, 1, argv, err), "OpenLogFile with old files: " + err);
        Check(tpw::LogRunNumber() == 6, "the new run comes after the five folded in: run " + std::to_string(tpw::LogRunNumber()));
        tpw::CloseLogFile();
        const std::string text = Slurp(logs / "launcher.log");
        const std::vector<std::string> foldedHeaders = Headers(Lines(logs / "launcher.log"));
        Check(StartsWith(text, "a log line\n==== run 1 | 2026-10-20T08:00:00-03:00 (ADT) | STP/TPW Launcher v0.1.0 (build: old) | "
                               "from the old file launcher-20261020-080000.log ====\n" + oldHeader + "the oldest run\nexit: code 0\n"),
              "the file's own lines first, then the oldest old file: " + text.substr(0, 200));
        Check(foldedHeaders.size() == 6 && foldedHeaders[1].find("from the old file game.log ====") != std::string::npos &&
                  foldedHeaders[2].find("| STP/TPW Launcher v0.1.0 (build: old) | from the old file launcher-20261021-080000.log ====") !=
                      std::string::npos &&
                  foldedHeaders[3].find("from the old file launcher-20261021-080000-2.log ====") != std::string::npos &&
                  foldedHeaders[4].find("| what the launcher printed on Windows | from the old file launcher.log ====") != std::string::npos &&
                  StartsWith(foldedHeaders[5], "==== run 6 | "),
              "the old files in order, as runs 1 to 5, then the new run 6");
        Check(text.find("no newline at the end\n==== run 5 |") != std::string::npos &&
                  text.find("what the launcher printed\n==== run 6 |") != std::string::npos,
              "every old file ends in a newline before the next header");
        Check(text.find("\nlog: the old log files were put in this one, oldest first, and removed: launcher-20261020-080000.log, "
                        "game.log, launcher-20261021-080000.log, launcher-20261021-080000-2.log, launcher.log\n") != std::string::npos,
              "the new run's header says which old files went in");
        for (const fs::path& file : old) Check(!fs::exists(file), "the old file is removed: " + file.filename().string());
        const std::set<std::string> left = Names(logs);
        Check(left.count("launcher-notes.log") == 1 && left.count("launcher-20261021-080000.log.bak") == 1 && left.count("notes.txt") == 1 &&
                  left.count("launcher-20261021-080000-0.log") == 1 && left.count("launcher-20270101-000000.log") == 1 &&
                  fs::exists(data / "other.log") && fs::exists(data / "launcher.ini") && fs::exists(logs / "launcher.log"),
              "what is not the launcher's old logs is left alone");
#ifndef _WIN32
        Check(fs::is_symlink(logs / "launcher-20261022-080000.log") && Slurp(root / "elsewhere.log") == "a file somewhere else\n" &&
                  text.find("a file somewhere else") == std::string::npos,
              "a link with a log's name is not followed or removed");
#endif
        // The next launch has nothing to fold: no note, run 7.
        Check(tpw::OpenLogFile(logs, firstLine, 1, argv, err) && tpw::LogRunNumber() == 7, "the launch after the folding is run 7");
        tpw::CloseLogFile();
        const std::string again = Slurp(logs / "launcher.log");
        Check(StartsWith(again, text) && again.find("log: the old", text.size()) == std::string::npos,
              "the launch after it adds its run and nothing else");
    }

    // ---- The cut to 5 MB at a launch, at a run header
    {
        const fs::path logs = root / "big" / "logs";
        fs::create_directories(logs);
        const std::string run1 = "==== run 1 | first ====\n" + Filler('a', 30000);   // 3 MB each
        const std::string run2 = "==== run 2 | second ====\n" + Filler('b', 30000) + "the last line of run 2\n";
        Put(logs / "launcher.log", run1 + run2);
        Check(tpw::OpenLogFile(logs, firstLine, 1, argv, err) && tpw::LogRunNumber() == 3, "OpenLogFile on a 6 MB file: run 3");
        tpw::CloseLogFile();
        const std::string text = Slurp(logs / "launcher.log");
        Check(StartsWith(text, run2 + "==== run 3 | "), "the oldest run went whole; run 2 is kept whole, then run 3");
        Check(text.find("\nlog: cut " + std::to_string(run1.size()) + " bytes of the oldest lines: the file keeps its newest 5 MB\n") !=
                  std::string::npos,
              "run 3's header says how much was cut");
        Check(text.size() < tpw::kLogKeepBytes, "the file is under 5 MB: " + std::to_string(text.size()));
    }

    // ---- The cut at a line, when the newest run alone is over 5 MB
    {
        const fs::path logs = root / "huge" / "logs";
        fs::create_directories(logs);
        const std::string run1 = "==== run 1 | one run that wrote and wrote ====\n" + Filler('a', 70000) + "the last line of run 1\n";
        Put(logs / "launcher.log", run1);
        Check(tpw::OpenLogFile(logs, firstLine, 1, argv, err) && tpw::LogRunNumber() == 2, "OpenLogFile on one 7 MB run: run 2");
        tpw::CloseLogFile();
        const std::string text = Slurp(logs / "launcher.log");
        const size_t next = text.find("==== run 2 | ");
        Check(next != std::string::npos && next <= tpw::kLogKeepBytes && next > tpw::kLogKeepBytes - 300,
              "the old part is just under 5 MB: " + std::to_string(next));
        Check(StartsWith(text, "==== run 1 | one run that wrote and wrote ====\n[... ") &&
                  text.find(" bytes of this run's earlier lines were cut here: the log file keeps its newest 5 MB ...]\naaa") !=
                      std::string::npos &&
                  text.compare(next - 23, 23, "the last line of run 1\n") == 0,
              "the run keeps its header line, a line for the cut and its end, from a line's start");
    }

    // ---- The game's own process never cuts the file or folds the old files in
    {
        const fs::path logs = root / "game" / "logs";
        fs::create_directories(logs);
        const std::string big = "==== run 1 | first ====\n" + Filler('a', 30000) + "==== run 2 | second ====\n" + Filler('b', 30000);
        Put(logs / "launcher.log", big);
        Put(logs / "launcher-20261020-080000.log", "an old run\n");
        Check(tpw::OpenLogFile(logs, firstLine, 5, gameArgv, err) && tpw::LogRunNumber() == 3, "the game's own process: run 3");
        tpw::CloseLogFile();
        const std::string text = Slurp(logs / "launcher.log");
        Check(StartsWith(text, big + "==== run 3 | ") && fs::exists(logs / "launcher-20261020-080000.log") &&
                  text.find("\nlog: cut ") == std::string::npos && text.find("\nlog: not cut") == std::string::npos &&
                  text.find("\nlog: the old") == std::string::npos,
              "the game's process leaves the file and the old file as they are, and says nothing about it");
    }

#ifndef _WIN32
    // ---- Another process has the file open (it holds the shared lock): this launch neither cuts nor folds; the next does
    {
        const fs::path logs = root / "busy" / "logs";
        fs::create_directories(logs);
        const std::string big = "==== run 1 | first ====\n" + Filler('a', 30000) + "==== run 2 | second ====\n" + Filler('b', 30000);
        Put(logs / "launcher.log", big);
        Put(logs / "launcher-20261020-080000.log", "an old run\n");
        const int other = open((logs / "launcher.log").c_str(), O_RDONLY | O_CLOEXEC);
        Check(other >= 0 && flock(other, LOCK_SH) == 0, "a second holder of the shared lock");
        Check(tpw::OpenLogFile(logs, firstLine, 1, argv, err) && tpw::LogRunNumber() == 3, "OpenLogFile while the file is in use: run 3");
        tpw::LogFileF("a line while the other holds it");
        tpw::CloseLogFile();
        const std::string text = Slurp(logs / "launcher.log");
        Check(StartsWith(text, big + "==== run 3 | ") && fs::exists(logs / "launcher-20261020-080000.log") &&
                  text.find("\nlog: not cut this launch: another launcher or the game's own process has the file open (the next launch "
                            "cuts it)\n") != std::string::npos &&
                  text.find("a line while the other holds it\n") != std::string::npos,
              "a file in use is not cut, its old files stay, and the header says why");
        close(other);
        Check(tpw::OpenLogFile(logs, firstLine, 1, argv, err) && tpw::LogRunNumber() == 5, "the next launch: the old file is run 4, it is run 5");
        tpw::CloseLogFile();
        const std::string after = Slurp(logs / "launcher.log");
        Check(StartsWith(after, "==== run 2 | second ====\n") && !fs::exists(logs / "launcher-20261020-080000.log") &&
                  after.find("| from the old file launcher-20261020-080000.log ====\nan old run\n==== run 5 | ") != std::string::npos,
              "the next launch cuts it and folds the old file in");
        // While this process holds the file (a launcher still running), a second descriptor cannot take its exclusive lock.
        Check(tpw::OpenLogFile(logs, firstLine, 1, argv, err), "OpenLogFile, kept open");
        const int probe = open((logs / "launcher.log").c_str(), O_RDONLY | O_CLOEXEC);
        Check(probe >= 0 && flock(probe, LOCK_EX | LOCK_NB) != 0 && flock(probe, LOCK_SH | LOCK_NB) == 0,
              "a running launcher holds the shared lock: no exclusive lock for another, a shared one yes");
        if (probe >= 0) close(probe);
        tpw::CloseLogFile();
    }
#endif

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
        Check(tpw::ExportedLogName("0.2.0-dev", when, 1) == "STP-TPW Launcher v0.2.0-dev logs 2026-10-25 10-12.txt",
              "exported name: " + tpw::ExportedLogName("0.2.0-dev", when, 1));
        Check(tpw::ExportedLogName("0.2.0", when, 2) == "STP-TPW Launcher v0.2.0 logs 2026-10-25 10-12 (2).txt", "exported name, copy 2");
        Check(tpw::ExportedLogName("", when, 1) == "STP-TPW Launcher logs 2026-10-25 10-12.txt", "exported name without a version");

        // ExportLogs, on what a Mac's log file holds after the game crashed and the player reopened the launcher: the
        // launcher's run that pressed Play (its exit line comes after the game's run started), the game's own run that
        // crashed, and the new launcher run; before them two old files folded in, and a line from before any header.
        // Every byte is kept (a CR, a NUL and a high byte), and the log file stays as it was.
        const fs::path out = root / fs::u8path("Downloads \xC3\xA9 x");
        fs::create_directories(out);
        const fs::path mac = root / fs::u8path("export logs \xC3\xA9") / "logs";
        fs::create_directories(mac);
        const fs::path macLog = mac / "launcher.log";
        const auto header = [](int run, const std::string& time, const std::string& extra, const std::string& args) {
            return "==== run " + std::to_string(run) + " | 2026-10-25 " + time + " -03:00 | STP/TPW Launcher v0.2.0-dev (build: abc1234) | " +
                   "macOS 15.1 (24B83) | " + extra + " | process 4" + std::to_string(run) + " ====\ndate: 2026-10-25T" + time +
                   "-03:00 (ADT)\nos: macOS 15.1 (24B83)\nhardware: Mac16,5, arm64\nargs: " + args + "\nlog: ~/logs/launcher.log\n";
        };
        const std::string before = "a log line\n";
        const std::string oldRun = "==== run 3 | 2026-10-25T09:00:00-03:00 (ADT) | STP/TPW Launcher v0.1.0 (build: old) | from the old "
                                   "file launcher-20261025-090000.log ====\nSTP/TPW Launcher v0.1.0 (build: old)\n"
                                   "date: 2026-10-25T09:00:00-03:00 (ADT)\nos: macOS 15.1\nhardware: Mac16,5\nargs: tpw_launcher\n"
                                   "log: ~/logs/launcher-20261025-090000.log\nwindow: shown\nexit: code 0\n";
        const std::string oldGame = "==== run 4 | last written 2026-10-25 09:30:00 -03:00 | what the game's own process printed on "
                                    "Windows | from the old file game.log ====\nAssertion failed: x\n";
        const std::string pressed = header(7, "10:05:00", "the launcher", "tpw_launcher") +
            "play: Sim Theme Park, the US edition (us), from the launcher window's Play, handed to ~/b/Sim Theme Park.app\n";
        const std::string crashed =
            header(8, "10:05:02", "Sim Theme Park | the game's own process (from the launcher's Play)",
                   "\"~/b/Sim Theme Park.app/Contents/MacOS/Sim Theme Park\" --play us --from-launcher ~/b") +
            "play: Sim Theme Park, the US edition (us), as the game's own application\n" + std::string("binary \r\n\0\xFF end\n", 16) +
            "exit: code 0 [run 7]\nlauncher: crashed (signal 11)\nexit: crashed (signal 11) [run 8]\n";
        const std::string reopened = header(9, "10:11:00", "the launcher", "tpw_launcher") + "window: shown\n";
        const std::string whole = before + oldRun + oldGame + pressed + crashed + reopened;
        Put(macLog, whole);
        tpw::ExportProgram program;
        program.version = "0.2.0-dev";
        program.firstLine = "STP/TPW Launcher v0.2.0-dev (build: abc1234)";
        const tpw::LogExportResult first = tpw::ExportLogs(macLog, out, when, program);
        Check(first.ok && first.message == "Saved to Downloads: STP-TPW Launcher v0.2.0-dev logs 2026-10-25 10-12.txt",
              "export: saved, with the message: " + first.message);
        Check(first.file == out / "STP-TPW Launcher v0.2.0-dev logs 2026-10-25 10-12.txt" && first.folder == out, "export: where it went");
        const std::string text = Slurp(first.file);
        Check(StartsWith(text, "STP/TPW Launcher v0.2.0-dev (build: abc1234): logs exported 2026-10-25 10:12:00\n"),
              "export: the first line names the launcher and the time: " + text.substr(0, 100));
        const std::string noExit = "no exit line (it was stopped, or ended before it could write one)";
        Check(text.find("This file holds 6 runs of the launcher's log, newest first:\n"
                        "1. run 9: the launcher; started 2026-10-25T10:11:00-03:00 (ADT); " + noExit + "\n") != std::string::npos,
              "export: the list starts with the newest run");
        Check(text.find("2. run 8: the game (Sim Theme Park), in its own process; started 2026-10-25T10:05:02-03:00 (ADT); "
                        "exit: crashed (signal 11) [run 8]\n") != std::string::npos,
              "export: the game's crashed run, named as the game's, with its own exit line");
        Check(text.find("3. run 7: the launcher (its Play started the game's own process, a run of its own after this one); started "
                        "2026-10-25T10:05:00-03:00 (ADT); exit: code 0 [run 7]\n") != std::string::npos,
              "export: the run that pressed Play, with its exit line from among the game's lines: " + text.substr(0, 1500));
        Check(text.find("4. run 4 (from the old file game.log): what the game's own process printed on Windows (the old game.log); "
                        "last written 2026-10-25 09:30:00 -03:00; " + noExit + "\n") != std::string::npos,
              "export: an old game.log folded in");
        Check(text.find("5. run 3 (from the old file launcher-20261025-090000.log): the launcher; started 2026-10-25T09:00:00-03:00 "
                        "(ADT); exit: code 0\n") != std::string::npos,
              "export: an old run's file folded in, its untagged exit line its own");
        Check(text.find("6. before the first run: lines from before the first run header; " + noExit + "\n") != std::string::npos,
              "export: the lines before the first header");
        const size_t crashAt = text.find("==================== 2 of 6: run 8 ====");
        Check(crashAt != std::string::npos && text.find("program: the game (Sim Theme Park), in its own process\n", crashAt) != std::string::npos &&
                  text.find("ended: exit: crashed (signal 11) [run 8]\n", crashAt) != std::string::npos && text.find(crashed, crashAt) != std::string::npos,
              "export: the crashed run's section holds its header and every byte of its lines");
        for (const std::string* part : {&before, &oldRun, &oldGame, &pressed, &reopened})
            Check(text.find(*part) != std::string::npos, "export: every run whole: " + part->substr(0, 40));
        Check(text.find("[...") == std::string::npos, "export: nothing left out");
        Check(text.find("=== 1 of 6") < text.find("=== 2 of 6") && text.find("=== 2 of 6") < text.find("=== 3 of 6") &&
                  text.find("=== 5 of 6") < text.find("=== 6 of 6"),
              "export: the sections in the list's order");
        Check(Slurp(macLog) == whole, "export: the log file is left as it was");

        // RunsOfLog: the running launcher's own run without an exit line is still running; another run is not.
        const std::vector<tpw::ExportedLog> runs = tpw::RunsOfLog(whole, 9);
        Check(runs.size() == 6 && runs[0].ended == "still running (the launcher that exported this file)" && runs[1].ended ==
                  "exit: crashed (signal 11) [run 8]" && runs[5].name == "before the first run" && runs[0].head == header(9, "10:11:00",
                  "the launcher", "tpw_launcher") && runs[0].body == "window: shown\n",
              "RunsOfLog: the runs, newest first, each with its head and body");
        Check(tpw::RunsOfLog("", 0).empty() && tpw::RunsOfLog("==== run 2 | x ====\n", 0).size() == 1, "RunsOfLog: nothing, and one bare header");

        // The name is taken: " (2)", " (3)"; the earlier copies are not touched.
        std::ofstream(first.file, std::ios::binary) << "someone edited this\n";
        const tpw::LogExportResult second = tpw::ExportLogs(macLog, out, when, program);
        Check(second.ok && second.file == out / "STP-TPW Launcher v0.2.0-dev logs 2026-10-25 10-12 (2).txt", "export: a taken name gets (2)");
        const tpw::LogExportResult third = tpw::ExportLogs(macLog, out, when, program);
        Check(third.ok && third.file == out / "STP-TPW Launcher v0.2.0-dev logs 2026-10-25 10-12 (3).txt", "export: and then (3)");
        Check(Slurp(first.file) == "someone edited this\n" && Slurp(second.file) == text, "export: nothing was overwritten");
        size_t filesBefore = Names(out).size();
        Check(filesBefore == 3, "export: three files in Downloads");

        // The game in the launcher's process (Windows before the game had its own, or --play): named so.
        const fs::path one = root / "one" / "logs";
        fs::create_directories(one);
        Put(one / "launcher.log", header(1, "10:05:00", "the launcher", "tpw_launcher") +
                                      "play: Theme Park World, the European edition (eur), from the launcher window's Play; game "
                                      "folder x\nexit: code 0 [run 1]\n" +
                                      header(2, "10:06:00", "Theme Park World | the launcher", "tpw_launcher --play eur") +
                                      "exit: abort [run 2]\n");
        const tpw::LogExportResult inProcess = tpw::ExportLogs(one / "launcher.log", out, when + 60, program);
        const std::string inProcessText = Slurp(inProcess.file);
        Check(inProcess.ok && inProcessText.find("1. run 2: the launcher, playing Theme Park World; started ") != std::string::npos &&
                  inProcessText.find("; exit: abort [run 2]\n") != std::string::npos &&
                  inProcessText.find("2. run 1: the launcher, then Theme Park World in the same process; ") != std::string::npos,
              "export: the game in the launcher's own process");
        fs::remove(inProcess.file);

        // The running launcher exports its own file: its run is still running.
        {
            const fs::path live = root / "live" / "logs";
            Check(tpw::OpenLogFile(live, program.firstLine, 1, argv, err), "OpenLogFile for a live export: " + err);
            tpw::LogFileF("window: shown");
            const tpw::LogExportResult running = tpw::ExportLogs(live / "launcher.log", out, when + 90, program);
            tpw::CloseLogFile();
            const std::string runningText = Slurp(running.file);
            Check(running.ok && runningText.find("1. run 1: the launcher; started ") != std::string::npos &&
                      runningText.find("; still running (the launcher that exported this file)\n") != std::string::npos &&
                      runningText.find("window: shown\n") != std::string::npos,
                  "export: the launcher's own run, still running");
            fs::remove(running.file);
        }

        // A run far over the limit (one that printed a line a frame): its header lines and its end, never more than 8 MB;
        // the older run is left out.
        const fs::path big = root / "big export" / "logs";
        fs::create_directories(big);
        {
            std::ofstream file(big / "launcher.log", std::ios::binary);
            file << header(1, "09:00:00", "the launcher", "tpw_launcher") << "an older run\n";
            file << header(2, "10:05:00", "the launcher", "tpw_launcher") << Filler('x', 90000);
            file << "the last line\nexit: crashed (signal 6) [run 2]\n";
        }
        const tpw::LogExportResult large = tpw::ExportLogs(big / "launcher.log", out, when + 120, program);
        const std::string largeText = Slurp(large.file);
        Check(large.ok && largeText.size() <= tpw::kExportLimitBytes && largeText.size() > tpw::kExportLimitBytes - 2000,
              "export: a big log fills the file to just under 8 MB: " + std::to_string(largeText.size()));
        Check(largeText.find("args: tpw_launcher\nlog: ~/logs/launcher.log\n[... ") != std::string::npos &&
                  largeText.find(" bytes of this run's earlier lines are left out, to keep this file under 8 MB ...]\nxxx") != std::string::npos &&
                  largeText.find("the last line\nexit: crashed (signal 6) [run 2]\n") != std::string::npos,
              "export: the big run keeps its header lines and its end, and says what was left out");
        Check(largeText.find("1. run 2: the launcher; started 2026-10-25T10:05:00-03:00 (ADT); exit: crashed (signal 6) [run 2]; its "
                             "beginning is left out (the 8 MB limit)\n") != std::string::npos &&
                  largeText.find("2. run 1: the launcher; started 2026-10-25T09:00:00-03:00 (ADT); " + noExit +
                                 "; left out (the 8 MB limit)\n") != std::string::npos &&
                  largeText.find("an older run") == std::string::npos,
              "export: the list says what was cut; the older run is left out");
        fs::remove(large.file);

        // A file over four times the limit is read from its end: the runs before that are not in the list at all.
        {
            std::ofstream file(big / "launcher.log", std::ios::binary);
            file << header(1, "08:00:00", "the launcher", "tpw_launcher") << Filler('o', 330000);
            file << header(2, "10:05:00", "the launcher", "tpw_launcher") << "the newest run\n";
        }
        const tpw::LogExportResult huge = tpw::ExportLogs(big / "launcher.log", out, when + 180, program);
        const std::string hugeText = Slurp(huge.file);
        Check(huge.ok && hugeText.size() <= tpw::kExportLimitBytes && hugeText.find("1. run 2: the launcher; ") != std::string::npos &&
                  hugeText.find("2. before the first run: ") != std::string::npos && hugeText.find("the newest run\n") != std::string::npos &&
                  hugeText.find("run 1:") == std::string::npos,
              "export: a 33 MB file is read from its last 32 MB");
        fs::remove(huge.file);

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
                      cut.find(" bytes of this run's earlier lines are left out, to keep this file under " + std::to_string(limit) +
                               " bytes ...]\nline ") != std::string::npos,
                  "ExportText: the next oldest keeps its header lines and its end, from a line's start");
            const std::string tiny = tpw::ExportText("first", "now", pieces, 100);
            Check(tiny.find("left out (the 100 bytes limit)") != std::string::npos && tiny.find("====") == std::string::npos,
                  "ExportText: a limit too small for any log lists them all as left out");
        }

        // Failures write nothing and say why.
        const tpw::LogExportResult noLogs = tpw::ExportLogs(root / "no such folder" / "launcher.log", out, when, program);
        Check(!noLogs.ok && noLogs.message == "There is no launcher log to export." && noLogs.file.empty() && Names(out).size() == filesBefore,
              "export: a missing log file: " + noLogs.message);
        fs::create_directories(root / "empty" / "logs");
        Put(root / "empty" / "logs" / "launcher.log", "");
        Check(!tpw::ExportLogs(root / "empty" / "logs" / "launcher.log", out, when, program).ok && Names(out).size() == filesBefore,
              "export: an empty log file");
        const tpw::LogExportResult noDownloads = tpw::ExportLogs(macLog, root / "no such Downloads", when, program);
        Check(!noDownloads.ok && noDownloads.message == "The Downloads folder was not found." &&
                  !fs::exists(root / "no such Downloads"),
              "export: a missing Downloads folder is not made: " + noDownloads.message);
        Check(!tpw::ExportLogs(macLog, fs::path(), when, program).ok, "export: no Downloads folder at all");
        Touch(root / "downloads-is-a-file");
        Check(!tpw::ExportLogs(macLog, root / "downloads-is-a-file", when, program).ok && fs::is_regular_file(root / "downloads-is-a-file"),
              "export: Downloads that is a file");
#ifndef _WIN32
        // A Downloads folder that cannot be written (not for root, which ignores the mode).
        const fs::path locked = root / "locked Downloads";
        fs::create_directories(locked);
        fs::permissions(locked, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
        if (geteuid() != 0) {
            const tpw::LogExportResult denied = tpw::ExportLogs(macLog, locked, when, program);
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
