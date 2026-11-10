// ps1_refresh.cpp: see ps1_refresh.h.
#include "ps1_refresh.h"

#include <fstream>
#include <vector>

#include "ps1_convert.h"
#include "sha256.h"
#include "content_overlay.h"

namespace tpw::launcher {

std::string Sha256OfFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    tpw::assets::Sha256 sha;
    std::vector<char> buf(1 << 20);
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize n = in.gcount();
        if (n > 0) sha.Update(buf.data(), static_cast<size_t>(n));
    }
    if (in.bad()) return std::string();
    return sha.Final().Hex();
}

bool Ps1RidesStale(const std::filesystem::path& contentFolder, const std::filesystem::path& dataRoot) {
    if (tpw::FindContentRides(contentFolder).empty()) return false;
    return tpw::assets::ReadPs1ConverterStamp(dataRoot) < tpw::assets::kPs1ConverterVersion;
}

Ps1Refresh RefreshPs1Rides(const std::filesystem::path& contentFolder, const std::filesystem::path& dataRoot,
                           const std::string& discPath, const std::string& discSha256, const Ps1Converter& convert) {
    Ps1Refresh r;
    if (tpw::FindContentRides(contentFolder).empty()) return r;
    r.stamp = tpw::assets::ReadPs1ConverterStamp(dataRoot);
    if (r.stamp >= tpw::assets::kPs1ConverterVersion) {
        r.result = Ps1Refresh::Result::Current;
        return r;
    }
    const std::string versions = "(converter " + std::to_string(r.stamp) + " -> " +
                                 std::to_string(tpw::assets::kPs1ConverterVersion) + ")";
    std::error_code ec;
    const std::filesystem::path disc = std::filesystem::u8path(discPath);
    if (discPath.empty() || discSha256.empty() || !std::filesystem::is_regular_file(disc, ec)) {
        r.result = Ps1Refresh::Result::NeedsDisc;
        r.line = "PlayStation rides were converted by an older version " + versions +
                 (discPath.empty() ? "; no disc remembered" : "; the remembered disc is gone") +
                 ": the disc is asked for again";
        return r;
    }
    if (Sha256OfFile(disc) != discSha256) {
        r.result = Ps1Refresh::Result::NeedsDisc;
        r.line = "PlayStation rides were converted by an older version " + versions +
                 "; the remembered disc has changed: the disc is asked for again";
        return r;
    }
    std::string reason;
    if (!convert || !convert(discPath, reason)) {
        r.result = Ps1Refresh::Result::Failed;
        r.line = "PlayStation rides were converted by an older version " + versions + "; converting " +
                 disc.filename().u8string() + " again failed: " + (reason.empty() ? "no converter" : reason) +
                 ": the disc is asked for again";
        return r;
    }
    r.result = Ps1Refresh::Result::Converted;
    r.line = "PlayStation rides converted again " + versions + " from " + disc.filename().u8string();
    return r;
}

}  // namespace tpw::launcher
