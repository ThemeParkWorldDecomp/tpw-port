// texts.h: what the launcher screen says about editions, imports and their results, made from the importer's own
// numbers (assets/importer.h): every count shown comes from an EditionStatus or an ImportReport, never a literal. Pure
// functions, no SDL, so the screenshot test checks them directly.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "importer.h"

namespace tpw::ui {

// What the launcher takes in a drop, in one place: the drop field's heading ("Drop your game discs, ... here") and the
// card shown while a drag is over the window ("Game discs, ...") both say it (DropKinds). A new kind of file the
// importer opens is added here.
inline constexpr const char* kDropKinds = "game discs, images, zips, 7z, rar or folders";
// kDropKinds, with a capital letter when `sentence` (it starts a line).
std::string DropKinds(bool sentence);

// "1,683"
std::string FormatCount(uint64_t n);
// "812 KB", "1.4 GB"
std::string FormatBytes(uint64_t bytes);
// "1 file", "12 files"
std::string Files(uint64_t n);
// The Mods box's locked content line. A content mod unlocks when the player's disc supplies its content: the
// PlayStation rides ("ps1") or the Theme Park Inc worlds ("tpi").
std::string ContentModLockedText(const std::string& content);
// The home's notice, and the Game data box's PlayStation line, while the converted rides are an older converter's
// and the launcher could not convert the remembered disc again (launcher/ps1_refresh.h).
inline constexpr const char* kPs1StaleNotice =
    "Your PlayStation rides were converted by an older version. Drop your PlayStation disc on the launcher again to update them.";
inline constexpr const char* kPs1StaleLine = "Converted by an older version: drag in your PlayStation disc again to update them";
// The same two for the Theme Park Inc worlds (launcher/tpi_refresh.h). The disc or its installed folder both do.
inline constexpr const char* kTpiStaleNotice =
    "Your Theme Park Inc worlds are from an older version. Drop the disc again to update them.";
inline constexpr const char* kTpiStaleLine = "Converted by an older version: drag in your Theme Park Inc disc again to update them";
// The home's stale notice for a content mod's folder ("ps1", "tpi"): kPs1StaleNotice or kTpiStaleNotice.
const char* ContentStaleNotice(const std::string& content);

// The three tiles, in order: the US game, the European game, the Gold rides. A tile's settings keys and saves are its
// game's; the edition it shows is its Mac one or its Windows one (SlotStatus).
inline constexpr assets::Edition kEditions[3] = {assets::kEditionUs, assets::kEditionEur, assets::kEditionGold};
int EditionIndex(assets::Edition edition);   // the tile: 0 (us, us-pc), 1 (eur, eur-pc), 2 (gold)
// The Windows edition that shares tile `slot` with its Mac one (us-pc for 0, eur-pc for 1); gold for 2.
assets::Edition WindowsEdition(int slot);
// The index of an edition in ImportReport::status: us 0, eur 1, gold 2, us-pc 4, eur-pc 5.
int StatusIndex(assets::Edition edition);
// What tile `slot` shows, from all six statuses (StatusIndex order): its Mac edition when that is ready, else its
// Windows edition when that is, else the one with more files confirmed (the Mac one when even).
const assets::EditionStatus& SlotStatus(const assets::EditionStatus all[6], int slot);

const char* EditionTitle(assets::Edition edition);   // "US edition", "European edition", "Gold bonus rides"
const char* EditionGame(assets::Edition edition);    // "Sim Theme Park, Mac CD", "Theme Park World, Windows disc", ...
// The disc a missing file of `edition` comes from: SourceDiscName, but a Windows edition names its own disc ("the US
// Windows CD").
const char* DiscName(assets::Edition edition, assets::DiscSource source);

// The discs an edition still needs, from its missing counts: "the European PC CD and the US Mac CD". Empty when none.
std::string DiscsNeeded(const assets::EditionStatus& status);
// "12 files still missing: they are on the US Mac CD", "130 files still missing: 83 on the European PC CD and 47 on
// the US Mac CD". Empty when nothing required is missing.
std::string MissingText(const assets::EditionStatus& status);
// What a game from the Windows discs alone does not have: "The Mac CD's 17 bonus rides, sideshows and features were
// never on the PC (the Gold CD has them)". The count comes from the manifest. Empty for a Mac edition.
std::string WindowsEditionNote(assets::Edition edition);

struct TileText {
    std::string state;   // "Ready to play", "715 of 845 confirmed", "Locked"
    std::string detail;  // "1,683 files confirmed", "Drag in the US Mac CD to unlock", ...
};
TileText DescribeTile(const assets::EditionStatus& status);
// Tile `slot` (all six statuses, StatusIndex order): DescribeTile of what it shows, but a tile with nothing yet names
// both its discs ("Drag in the US Mac CD or the US Windows CD to unlock").
TileText DescribeSlot(const assets::EditionStatus all[6], int slot);

// A European folder that a drop left partial, with files of another content where the manifest expects its own: the
// older pressing of the PC CD. Reported as "Older pressing, <n> of <files> confirmed" with the Status counts.
// Not the US Windows CD's first release (LooksLikeFirstRelease), which leaves the European folders partial too.
bool LooksLikeOlderPressing(const assets::ImportReport& report);
// The US Windows CD's first release (1.0): the US Windows folder is left partial, with files of another version, and
// more of its files were found than of the European Windows folder's. The game is played from the 2.0 files: the
// result says to install it on Windows, apply EA's official v2.0 patch and drop the installed folder with the CD.
bool LooksLikeFirstRelease(const assets::ImportReport& report);

struct ResultText {
    std::string headline;
    std::vector<std::string> lines;
    bool good = false;   // a check mark rather than a warning sign
};
// `before` holds the six statuses (StatusIndex order) from before the drop; the report's own are after it.
ResultText DescribeResult(const assets::ImportReport& report, const assets::EditionStatus before[6]);

// One row of the checking log, or false for events that only move the counter (Scanning, Hashing).
struct LogRow {
    std::string path;
    std::string status;
    enum Tone { Good, Same, Odd, Bad } tone = Good;
};
bool DescribeProgress(const assets::ImportProgress& progress, LogRow& row);

// A folder's file:// URL, for SDL_OpenURL (the About box's Show logs): "/Users/a b/x" becomes "file:///Users/a%20b/x".
// Bytes other than letters, digits, '/' and "-._~" are percent-encoded, so a space or a non-ASCII name survives.
// Windows paths (windows true): '\' becomes '/', the drive's colon stays and a third slash leads ("C:\Users\a b" becomes
// "file:///C:/Users/a%20b"), "\\server\share" becomes "file://server/share", and non-ASCII bytes are left as they are:
// SDL hands ShellExecuteW the URL as UTF-16, and Windows may read a percent-encoded byte in the ANSI code page.
std::string FileUrl(const std::string& path, bool windows);
// The host's: Windows paths on Windows, Unix paths elsewhere.
std::string FileUrl(const std::string& path);

}  // namespace tpw::ui
