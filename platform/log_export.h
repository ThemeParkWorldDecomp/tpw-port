// log_export.h: the About box's Export logs button. Saves the log file (log_file.h: logs/launcher.log, every recent run of
// the launcher and of the game's own process) into ONE text file in the player's Downloads folder, under a name that says
// what it is and never takes an existing file's place, so a report can attach it (Discord previews a .txt) without a hunt
// through hidden folders. Its runs come newest first, each under a header that says which program it was, when it
// started and how it ended, so the run that crashed is found at a glance. The log file is never moved, changed or removed.
//
// No game headers here, and no windows.h in the header (log_export.cpp includes it for the Downloads folder).
#pragma once
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

#include "log_scrub.h"

namespace tpw {

// What an export did. `message` is for the player: "Saved to Downloads: <name>" when ok, else a plain reason and nothing
// was written. `file` is the copy (when ok) and `folder` the Downloads folder it is in.
struct LogExportResult {
    bool ok = false;
    std::string message;
    std::filesystem::path file;
    std::filesystem::path folder;
};

// The Downloads folder by the system's rule, from the folders it was told (a pure function, so the rules are tested on
// every system). windows: `knownFolder` (the Known Folder FOLDERID_Downloads) when it is not empty, else
// `userProfile`\Downloads. Otherwise `home`/Downloads. An empty path when the rule has nothing to go on.
std::filesystem::path DownloadsFolderFor(bool windows, const std::filesystem::path& knownFolder,
                                         const std::filesystem::path& home, const std::filesystem::path& userProfile);
// This system's Downloads folder: Windows asks for the Known Folder (SHGetKnownFolderPath) and falls back to
// %USERPROFILE%\Downloads; the Mac and other systems use $HOME/Downloads. Empty when there is none.
std::filesystem::path DownloadsFolder();

// The command line that makes Windows' Explorer open a file's folder with the file selected: /select,"C:\a b\file.log"
// (the path in quotes, with backslashes, which Explorer needs). A pure function, so the quoting is tested on every system.
std::wstring ExplorerSelectArguments(const std::filesystem::path& file);
// Shows `file` selected in its folder: the Finder on a Mac (open -R), Explorer on Windows (explorer.exe /select). False,
// having shown nothing, when that could not be started or the system has no such call (the caller opens the folder).
bool RevealFile(const std::filesystem::path& file);

// "STP-TPW Launcher v0.2.0-dev logs 2026-10-25 10-12.txt" for that version and local time ("STP-TPW Launcher logs ..."
// without a version); copy 2 and up add " (2)" before ".txt". No colon, so Windows takes the name.
std::string ExportedLogName(const std::string& version, std::time_t when, int copy);

// The export's size limit: 8 MB (in bytes of 1,000), which Discord and a GitHub issue both take.
inline constexpr size_t kExportLimitBytes = 8000000;
// One run of the log file in the export, as read (and scrubbed): `head` its header lines (its run header line, up to and
// with the "log: " line when there is one), `body` the rest. `omitted` counts the bytes of the body's beginning left out
// already (a file far over the limit is read from its end).
struct ExportedLog {
    std::string name;      // "run 12", or "run 3 (from the old file launcher-20261025-101100.log)"
    std::string program;   // which program wrote it, in words
    std::string started;   // its date: line, or empty
    std::string written;   // an old file's last change (a folded launcher.log or game.log), or empty
    std::string ended;     // its "exit:" line, or what its end means
    std::string head;
    std::string body;
    size_t omitted = 0;
};

// The runs of a log file's (scrubbed) text, newest first: each run header line (log_file.h) starts one, and the lines
// before the first header, when there are any, are one more, the oldest. A run's exit line is the "exit: " line that ends
// with its tag (" [run 12]"), wherever it is (the launcher's comes after the game's process started, so in the game's
// run), else an untagged one of its own lines (an old file's). `runningRun` is this process's run (LogRunNumber):
// without an exit line it is still running. `omitted` is how much of the file's beginning was not read.
std::vector<ExportedLog> RunsOfLog(const std::string& text, int runningRun, size_t omitted = 0);

// The export's text: `firstLine` (the launcher's name, version and build) and when it was made, a list of the runs, then
// each run under its own header, newest first. When it would pass `limit` bytes the oldest content goes first: the
// oldest runs are left out whole, then the beginning of the oldest run still in (its header lines stay), and the file
// says what was left out.
std::string ExportText(const std::string& firstLine, const std::string& exported, const std::vector<ExportedLog>& logs,
                       size_t limit = kExportLimitBytes);

// Who exports: the launcher's version for the file's name and its first line (its name, version and build).
struct ExportProgram {
    std::string version;     // "0.2.0-dev"
    std::string firstLine;   // "STP/TPW Launcher v0.2.0-dev (build: 8ab6df1)"
};

// Writes ExportText of the RunsOfLog of `logFile` (at `when`) into `downloads` as ExportedLogName(program.version, when,
// ...), the first copy number whose name is free. The file is scrubbed again (log_scrub.h) with `identity` (null: this
// computer's), a safety net for lines an older launcher wrote; a file longer than four times the limit is read from its
// end. Fails, writing nothing, when there is no log file or it is empty, `downloads` is not a folder, the file cannot be
// read or the copy cannot be written (a partly written copy is removed again).
LogExportResult ExportLogs(const std::filesystem::path& logFile, const std::filesystem::path& downloads, std::time_t when,
                           const ExportProgram& program, const ScrubIdentity* identity = nullptr);

}  // namespace tpw
