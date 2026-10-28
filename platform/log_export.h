// log_export.h: the About box's Export logs button. Saves everything a bug report needs into ONE text file in the player's
// Downloads folder, under a name that says what it is and never takes an existing file's place, so a report can attach it
// (Discord previews a .txt) without a hunt through hidden folders: the newest launcher-*.log files (log_file.h; on a Mac
// the game's own process writes one there too) and, beside the logs folder, launcher.log and game.log, what the launcher's
// and the game's processes print on Windows. Each comes under a header that says which file it is, which program wrote
// it, when it started and how it ended, so the run that crashed is found at a glance. The originals are never moved,
// changed or removed.
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

// "STP-TPW Launcher v0.1.1-dev logs 2026-10-25 10-12.txt" for that version and local time ("STP-TPW Launcher logs ..."
// without a version); copy 2 and up add " (2)" before ".txt". No colon, so Windows takes the name.
std::string ExportedLogName(const std::string& version, std::time_t when, int copy);

// The export's size limit: 8 MB (in bytes of 1,000), which Discord and a GitHub issue both take.
inline constexpr size_t kExportLimitBytes = 8000000;
// How many launcher-*.log files go in at least (the newest by time), whatever their age: after a crash the player
// reopens the launcher, so the run that crashed is the second newest, or the third when the game ran in its own process.
inline constexpr size_t kExportNewestLogs = 3;
// Older launcher-*.log files go in too while they were written in the last this many hours (up to all the folder keeps).
inline constexpr int kExportRecentHours = 24;

// Which files an export takes, newest first (by modification time, then by name as NewestLog): the kExportNewestLogs
// newest launcher-*.log files of `logsFolder`, the others written since `now` less kExportRecentHours, and launcher.log
// and game.log in the folder above `logsFolder` when they are there and not empty. Never more than kLogFilesKept + 2.
std::vector<std::filesystem::path> LogsToExport(const std::filesystem::path& logsFolder,
                                                std::filesystem::file_time_type now);

// One log in the export, as read (and scrubbed): `head` its header lines (up to and with the "log: " line of a
// launcher-*.log; empty for the others), `body` the rest. `omitted` counts the bytes of the body's beginning left out
// already (a file far over the limit is read from its end).
struct ExportedLog {
    std::string name;      // the file's name: launcher-20261025-101100.log, game.log
    std::string program;   // which program wrote it, in words
    std::string started;   // the header's date, or empty
    std::string written;   // the file's last change, local time, or empty
    std::string ended;     // its last "exit:" line, or what its end means
    std::string head;
    std::string body;
    size_t omitted = 0;
};

// The export's text: `firstLine` (the launcher's name, version and build) and when it was made, a list of the logs, then
// each log under its own header, newest first. When it would pass `limit` bytes the oldest content goes first: the
// oldest logs are left out whole, then the beginning of the oldest log still in (its header lines stay), and the file
// says what was left out.
std::string ExportText(const std::string& firstLine, const std::string& exported, const std::vector<ExportedLog>& logs,
                       size_t limit = kExportLimitBytes);

// Who exports: the launcher's version for the file's name and its first line (its name, version and build).
struct ExportProgram {
    std::string version;     // "0.1.1-dev"
    std::string firstLine;   // "STP/TPW Launcher v0.1.1-dev (build: 8ab6df1)"
};

// Writes ExportText of the LogsToExport of `logsFolder` (at `when`) into `downloads` as ExportedLogName(program.version,
// when, ...), the first copy number whose name is free. Every log is scrubbed again (log_scrub.h) with `identity` (null:
// this computer's), a safety net for a log an older launcher wrote. Fails, writing nothing, when the logs folder has no
// launcher log, `downloads` is not a folder, no log can be read or the file cannot be written (a partly written file is
// removed again).
LogExportResult ExportLogs(const std::filesystem::path& logsFolder, const std::filesystem::path& downloads,
                           std::time_t when, const ExportProgram& program, const ScrubIdentity* identity = nullptr);

}  // namespace tpw
