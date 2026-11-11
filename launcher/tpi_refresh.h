// tpi_refresh.h: the Theme Park Inc worlds converted by an older converter (tpw-decomp assets/tpi_convert.h
// kTpiConverterVersion) are converted again at the launcher's start, from the disc or installed folder the importer
// remembered (assets/importer.h kTpiDiscPathKey), or the player is asked for the disc again (launcher/ui/texts.h
// kTpiStaleNotice). The twin of ps1_refresh.h for the TPI Worlds and Rides mod's content (platform/tpi_content.h).
#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "tpi_convert.h"

namespace tpw::launcher {

struct TpiRefresh {
    enum class Result {
        NoWorlds,    // the content folder holds no converted worlds: nothing to do
        Current,     // the worlds' stamp is the converter's version (or newer)
        Converted,   // stale worlds, and the remembered disc or folder converted them again
        NeedsDisc,   // stale worlds, and no remembered source, or it is gone or changed: ask for the disc again
        Failed       // stale worlds, and the remembered source's conversion failed: ask for the disc again
    };
    Result result = Result::NoWorlds;
    int stamp = 1;        // the version the worlds were converted by (ReadTpiConverterStamp)
    std::string line;     // one line for the log (empty for NoWorlds and Current)
};

// Converts the disc image or installed folder `path` again into dataRoot (the launcher's: ConvertTpiFromPath). False
// with a reason.
using TpiConverter = std::function<bool(const std::string& path, std::string& reason)>;

// `contentFolder` is the TPI Worlds and Rides mod's content folder (<dataRoot>/assets/tpi); `discPath` and `discSha256`
// are launcher.ini's remembered source (either may be empty). A disc image is hashed before `convert` runs on it, so a
// replaced file is not converted; an installed folder (remembered with no hash, assets/tpi_disc.h) is converted again
// while it is still there. `version` is this build's converter version (the tests pass another).
TpiRefresh RefreshTpiWorlds(const std::filesystem::path& contentFolder, const std::filesystem::path& dataRoot,
                            const std::string& discPath, const std::string& discSha256, const TpiConverter& convert,
                            int version = tpw::assets::kTpiConverterVersion);

// Whether the worlds in `contentFolder` (when there are any) were converted by an older converter than this build's.
bool TpiWorldsStale(const std::filesystem::path& contentFolder, const std::filesystem::path& dataRoot,
                    int version = tpw::assets::kTpiConverterVersion);

}  // namespace tpw::launcher
