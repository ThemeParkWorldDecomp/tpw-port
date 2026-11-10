// ps1_refresh.h: the PlayStation rides converted by an older converter (tpw-decomp assets/ps1_convert.h
// kPs1ConverterVersion) are converted again at the launcher's start, from the disc the importer remembered
// (assets/importer.h kPs1DiscPathKey), or the player is asked for the disc again (launcher/ui/texts.h kPs1StaleNotice).
#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace tpw::launcher {

struct Ps1Refresh {
    enum class Result {
        NoRides,     // the content folder holds no converted rides: nothing to do
        Current,     // the rides' stamp is the converter's version (or newer)
        Converted,   // stale rides, and the remembered disc converted them again
        NeedsDisc,   // stale rides, and no remembered disc, or it is gone or changed: ask for the disc again
        Failed       // stale rides, and the remembered disc's conversion failed: ask for the disc again
    };
    Result result = Result::NoRides;
    int stamp = 1;        // the version the rides were converted by (ReadPs1ConverterStamp)
    std::string line;     // one line for the log (empty for NoRides and Current)
};

// Converts the disc image `path` again into dataRoot (the launcher's: recognise it, ConvertPs1). False with a reason.
using Ps1Converter = std::function<bool(const std::string& path, std::string& reason)>;

// `contentFolder` is the PlayStation rides mod's content folder (<dataRoot>/assets/ps1); `discPath` and `discSha256`
// are launcher.ini's remembered disc (either may be empty). The disc is hashed before `convert` runs on it, so a
// replaced file is not converted.
Ps1Refresh RefreshPs1Rides(const std::filesystem::path& contentFolder, const std::filesystem::path& dataRoot,
                           const std::string& discPath, const std::string& discSha256, const Ps1Converter& convert);

// Whether the rides in `contentFolder` (when there are any) were converted by an older converter than this build's.
bool Ps1RidesStale(const std::filesystem::path& contentFolder, const std::filesystem::path& dataRoot);

// The lower-case hexadecimal SHA-256 of a file, or empty when it cannot be read.
std::string Sha256OfFile(const std::filesystem::path& path);

}  // namespace tpw::launcher
