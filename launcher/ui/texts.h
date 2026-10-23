// texts.h: what the launcher screen says about editions, imports and their results, made from the importer's own
// numbers (assets/importer.h): every count shown comes from an EditionStatus or an ImportReport, never a literal. Pure
// functions, no SDL, so the screenshot test checks them directly.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "importer.h"

namespace tpw::ui {

// "1,683"
std::string FormatCount(uint64_t n);
// "812 KB", "1.4 GB"
std::string FormatBytes(uint64_t bytes);
// "1 file", "12 files"
std::string Files(uint64_t n);

// The three editions, in tile order.
inline constexpr assets::Edition kEditions[3] = {assets::kEditionUs, assets::kEditionEur, assets::kEditionGold};
int EditionIndex(assets::Edition edition);   // 0, 1, 2

const char* EditionTitle(assets::Edition edition);   // "US edition", "European edition", "Gold bonus rides"
const char* EditionGame(assets::Edition edition);    // "Sim Theme Park, Mac CD", "Theme Park World, PC CD", ...

// The discs an edition still needs, from its missing counts: "the European PC CD and the US Mac CD". Empty when none.
std::string DiscsNeeded(const assets::EditionStatus& status);
// "12 files still missing: they are on the US Mac CD", "130 files still missing: 83 on the European PC CD and 47 on
// the US Mac CD". Empty when nothing required is missing.
std::string MissingText(const assets::EditionStatus& status);

struct TileText {
    std::string state;   // "Ready to play", "715 of 845 confirmed", "Locked"
    std::string detail;  // "1,683 files confirmed", "Drag in the US Mac CD to unlock", ...
};
TileText DescribeTile(const assets::EditionStatus& status);

// A European folder that a drop left partial, with files of another content where the manifest expects its own: the
// older pressing of the PC CD. Reported as "Older pressing, <n> of <files> confirmed" with the Status counts.
bool LooksLikeOlderPressing(const assets::ImportReport& report);

struct ResultText {
    std::string headline;
    std::vector<std::string> lines;
    bool good = false;   // a check mark rather than a warning sign
};
// `before` holds the three statuses (kEditions order) from before the drop; the report's own are after it.
ResultText DescribeResult(const assets::ImportReport& report, const assets::EditionStatus before[3]);

// One row of the checking log, or false for events that only move the counter (Scanning, Hashing).
struct LogRow {
    std::string path;
    std::string status;
    enum Tone { Good, Same, Odd, Bad } tone = Good;
};
bool DescribeProgress(const assets::ImportProgress& progress, LogRow& row);

// A folder's file:// URL, for SDL_OpenURL (the About box's Show logs): "/Users/a b/x" becomes "file:///Users/a%20b/x".
// Bytes other than letters, digits, '/' and "-._~" are percent-encoded, so a space or a non-ASCII name survives.
std::string FileUrl(const std::string& path);

}  // namespace tpw::ui
