// tpi_refresh.cpp: see tpi_refresh.h.
#include "tpi_refresh.h"

#include "ps1_refresh.h"
#include "tpi_content.h"

namespace tpw::launcher {

bool TpiWorldsStale(const std::filesystem::path& contentFolder, const std::filesystem::path& dataRoot, int version) {
    if (tpw::FindTpiWorlds(contentFolder).empty()) return false;
    return tpw::assets::ReadTpiConverterStamp(dataRoot) < version;
}

TpiRefresh RefreshTpiWorlds(const std::filesystem::path& contentFolder, const std::filesystem::path& dataRoot,
                            const std::string& discPath, const std::string& discSha256, const TpiConverter& convert,
                            int version) {
    TpiRefresh r;
    if (tpw::FindTpiWorlds(contentFolder).empty()) return r;
    r.stamp = tpw::assets::ReadTpiConverterStamp(dataRoot);
    if (r.stamp >= version) {
        r.result = TpiRefresh::Result::Current;
        return r;
    }
    const std::string versions = "(converter " + std::to_string(r.stamp) + " -> " + std::to_string(version) + ")";
    const std::string stale = "Theme Park Inc worlds were converted by an older version " + versions;
    std::error_code ec;
    const std::filesystem::path disc = std::filesystem::u8path(discPath);
    if (discPath.empty()) {
        r.result = TpiRefresh::Result::NeedsDisc;
        r.line = stale + "; no disc remembered: the disc is asked for again";
        return r;
    }
    // An installed folder is remembered without a hash (assets/tpi_disc.h): converted again while it is still there.
    const bool folder = discSha256.empty();
    if (folder ? !std::filesystem::is_directory(disc, ec) : !std::filesystem::is_regular_file(disc, ec)) {
        r.result = TpiRefresh::Result::NeedsDisc;
        r.line = stale + (folder ? "; the remembered folder is gone" : "; the remembered disc is gone") +
                 ": the disc is asked for again";
        return r;
    }
    if (!folder && Sha256OfFile(disc) != discSha256) {
        r.result = TpiRefresh::Result::NeedsDisc;
        r.line = stale + "; the remembered disc has changed: the disc is asked for again";
        return r;
    }
    std::string reason;
    if (!convert || !convert(discPath, reason)) {
        r.result = TpiRefresh::Result::Failed;
        r.line = stale + "; converting " + disc.filename().u8string() + " again failed: " +
                 (reason.empty() ? "no converter" : reason) + ": the disc is asked for again";
        return r;
    }
    r.result = TpiRefresh::Result::Converted;
    r.line = "Theme Park Inc worlds converted again " + versions + " from " + disc.filename().u8string();
    return r;
}

}  // namespace tpw::launcher
