// log_export.h: the About box's Export logs button. Copies the newest launcher log (log_file.h) into the player's
// Downloads folder under a name that says what it is and never takes an existing file's place, so a bug report can attach
// it without a hunt through hidden folders. The original log is never moved, changed or removed.
//
// No game headers here, and no windows.h in the header (log_export.cpp includes it for the Downloads folder).
#pragma once
#include <ctime>
#include <filesystem>
#include <string>

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

// "STP-TPW Launcher log 2026-10-25 10-12.txt" for that local time; copy 2 and up add " (2)" before ".txt". No colon, so
// Windows takes the name.
std::string ExportedLogName(std::time_t when, int copy);

// Copies the newest launcher-*.log of `logsFolder` into `downloads` as ExportedLogName(when, ...), the first copy number
// whose name is free. The copy is scrubbed again (log_scrub.h) with `identity` (null: this computer's), a safety net for a
// log an older launcher wrote. Fails, writing nothing, when the logs folder has no log, `downloads` is not a folder, the log cannot
// be read or the copy cannot be written (a partly written copy is removed again).
LogExportResult ExportLog(const std::filesystem::path& logsFolder, const std::filesystem::path& downloads,
                          std::time_t when, const ScrubIdentity* identity = nullptr);

}  // namespace tpw
