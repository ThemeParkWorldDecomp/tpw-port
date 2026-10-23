// log_tests.cpp: the launcher's log file (platform/log_file.h), in a temporary folder: the file's name, its header,
// the lines it takes and the ten-file rotation. No window and no game.
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

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

    std::error_code ec;
    fs::remove_all(root, ec);
    std::printf("%d of %d log file checks passed\n", gChecks - gFailures, gChecks);
    return gFailures == 0 ? 0 : 1;
}
