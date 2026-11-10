// texts.cpp: the launcher screen's wording (texts.h).
#include "texts.h"

#include <cstdio>
#include <cstring>

namespace tpw::ui {

using assets::EditionStatus;
using assets::ImportProgress;
using assets::ImportReport;

std::string ContentModLockedText(const std::string& content) {
    return "Drop your own " + std::string(content == "ps1" ? "PlayStation disc" : "disc") +
           " on the launcher to unlock.";
}

std::string FileUrl(const std::string& path, bool windows) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string url = "file://";
    size_t start = 0;
    if (windows) {
        const auto slash = [](char c) { return c == '\\' || c == '/'; };
        const bool drive = path.size() >= 2 && path[1] == ':' &&
                           ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'));
        if (drive) {
            url += '/';
            url += path.substr(0, 2);
            start = 2;
        } else if (path.size() >= 2 && slash(path[0]) && slash(path[1])) {
            start = 2;   // a UNC path: the server is the URL's host
        }
    }
    for (size_t i = start; i < path.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(path[i]);
        if (windows && c == '\\') c = '/';
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/' ||
                           c == '-' || c == '.' || c == '_' || c == '~' || (windows && c >= 0x80);
        if (plain) {
            url += static_cast<char>(c);
        } else {
            url += '%';
            url += kHex[c >> 4];
            url += kHex[c & 15];
        }
    }
    return url;
}

std::string FileUrl(const std::string& path) {
#ifdef _WIN32
    return FileUrl(path, true);
#else
    return FileUrl(path, false);
#endif
}

std::string DropKinds(bool sentence) {
    std::string kinds = kDropKinds;
    if (sentence && !kinds.empty() && kinds[0] >= 'a' && kinds[0] <= 'z')
        kinds[0] = static_cast<char>(kinds[0] - 'a' + 'A');
    return kinds;
}

std::string FormatCount(uint64_t n) {
    std::string digits = std::to_string(n), out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return out;
}

std::string FormatBytes(uint64_t bytes) {
    static const char* const units[] = {"bytes", "KB", "MB", "GB", "TB"};
    if (bytes < 1024) return FormatCount(bytes) + (bytes == 1 ? " byte" : " bytes");
    double v = static_cast<double>(bytes);
    int unit = 0;
    while (v >= 1024 && unit < 4) {
        v /= 1024;
        ++unit;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, v < 10 ? "%.1f %s" : "%.0f %s", v, units[unit]);
    return buf;
}

std::string Files(uint64_t n) {
    return FormatCount(n) + (n == 1 ? " file" : " files");
}

int EditionIndex(assets::Edition edition) {
    switch (edition) {
        case assets::kEditionUs:
        case assets::kEditionUsPc: return 0;
        case assets::kEditionEur:
        case assets::kEditionEurPc: return 1;
        case assets::kEditionGold: return 2;
        case assets::kEditionLanguages: break;
    }
    return 0;
}

assets::Edition WindowsEdition(int slot) {
    return slot == 0 ? assets::kEditionUsPc : slot == 1 ? assets::kEditionEurPc : assets::kEditionGold;
}

int StatusIndex(assets::Edition edition) {
    switch (edition) {
        case assets::kEditionUs: return 0;
        case assets::kEditionEur: return 1;
        case assets::kEditionGold: return 2;
        case assets::kEditionLanguages: return 3;
        case assets::kEditionUsPc: return 4;
        case assets::kEditionEurPc: return 5;
    }
    return 0;
}

const EditionStatus& SlotStatus(const EditionStatus all[6], int slot) {
    const EditionStatus& mac = all[StatusIndex(kEditions[slot])];
    if (slot > 1) return mac;
    const EditionStatus& windows = all[StatusIndex(WindowsEdition(slot))];
    if (mac.unlocked) return mac;
    if (windows.unlocked) return windows;
    return windows.filesPresent > mac.filesPresent ? windows : mac;
}

const char* EditionTitle(assets::Edition edition) {
    switch (edition) {
        case assets::kEditionUs:
        case assets::kEditionUsPc: return "US edition";
        case assets::kEditionEur:
        case assets::kEditionEurPc: return "European edition";
        case assets::kEditionGold: return "Gold bonus rides";
        case assets::kEditionLanguages: break;
    }
    return "?";
}

const char* EditionGame(assets::Edition edition) {
    switch (edition) {
        case assets::kEditionUs: return "Sim Theme Park, Mac CD";
        case assets::kEditionEur: return "Theme Park World, PC CD";
        case assets::kEditionGold: return "PC Gold bonus CD";
        case assets::kEditionUsPc: return "Sim Theme Park, Windows disc";
        case assets::kEditionEurPc: return "Theme Park World, Windows disc";
        case assets::kEditionLanguages: break;
    }
    return "?";
}

const char* DiscName(assets::Edition edition, assets::DiscSource source) {
    if (edition == assets::kEditionUsPc && source == assets::kSourcePcCd) return "the US Windows CD";
    return assets::SourceDiscName(source);   // the European Windows editions' disc is the European PC CD
}

namespace {
// The discs in the order the texts name them: an edition's own disc before the one it borrows from.
constexpr assets::DiscSource kSourceOrder[3] = {assets::kSourcePcCd, assets::kSourceMacCd, assets::kSourceGoldCd};

std::string JoinAnd(const std::vector<std::string>& parts) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += i + 1 == parts.size() ? " and " : ", ";
        out += parts[i];
    }
    return out;
}

const char* EditionPhrase(assets::Edition edition) {
    switch (edition) {
        case assets::kEditionUs: return "the US edition";
        case assets::kEditionEur: return "the European edition";
        case assets::kEditionGold: return "the Gold bonus rides";
        case assets::kEditionUsPc: return "the US edition (Windows disc)";
        case assets::kEditionEurPc: return "the European edition (Windows disc)";
        case assets::kEditionLanguages: break;
    }
    return "?";
}

// "US edition", "US edition (Windows disc)": an edition in the result's lines.
std::string EditionLabel(assets::Edition edition) {
    return std::string(EditionTitle(edition)) + (assets::IsWindowsEdition(edition) ? " (Windows disc)" : "");
}

// The editions a result goes through, the Mac ones first.
constexpr assets::Edition kResultEditions[5] = {assets::kEditionUs, assets::kEditionEur, assets::kEditionGold,
                                                assets::kEditionUsPc, assets::kEditionEurPc};
}  // namespace

std::string DiscsNeeded(const EditionStatus& s) {
    std::vector<std::string> discs;
    for (assets::DiscSource source : kSourceOrder)
        if (s.missing[source]) discs.push_back(DiscName(s.edition, source));
    return JoinAnd(discs);
}

std::string MissingText(const EditionStatus& s) {
    uint64_t total = 0;
    std::vector<assets::DiscSource> from;
    for (assets::DiscSource source : kSourceOrder) {
        if (!s.missingFiles[source]) continue;
        total += s.missingFiles[source];
        from.push_back(source);
    }
    if (total == 0) return {};
    std::string head = Files(total) + " still missing: ";
    if (from.size() == 1) return head + (total == 1 ? "it is on " : "they are on ") + DiscName(s.edition, from[0]);
    std::vector<std::string> parts;
    for (assets::DiscSource source : from)
        parts.push_back(FormatCount(s.missingFiles[source]) + " on " + DiscName(s.edition, source));
    return head + JoinAnd(parts);
}

std::string WindowsEditionNote(assets::Edition edition) {
    if (!assets::IsWindowsEdition(edition)) return {};
    // The Mac CD's bonus rides, sideshows and features: its "_" wads under data/levels, which the Windows disc lacks.
    const assets::Edition mac = edition == assets::kEditionUsPc ? assets::kEditionUs : assets::kEditionEur;
    size_t bonus = 0;
    const assets::ManifestRecord* records = assets::ManifestRecords();
    for (size_t i = 0; i < assets::ManifestRecordCount(); ++i) {
        const assets::ManifestRecord& r = records[i];
        if (!(r.editions & mac) || std::strncmp(r.dest, "data/levels/", 12) != 0) continue;
        const char* name = std::strrchr(r.dest, '/') + 1;
        const size_t n = std::strlen(name);
        if (name[0] != '_' || n < 4 || std::strcmp(name + n - 4, ".wad") != 0) continue;
        bonus += assets::FindByDest(edition, r.dest) == nullptr;
    }
    if (!bonus) return {};
    return "The Mac CD's " + FormatCount(bonus) +
           " bonus rides, sideshows and features were never on the PC (the Gold CD has them)";
}

TileText DescribeTile(const EditionStatus& s) {
    TileText t;
    if (s.required == 0) {
        t.state = "Not available";
        t.detail = "This build has no files listed for it";
    } else if (s.unlocked) {
        t.state = "Ready to play";
        t.detail = Files(s.filesPresent) + " confirmed";
        // The tile is small: WindowsEditionNote is on the Play screen and the all-confirmed home.
        if (assets::IsWindowsEdition(s.edition) && !WindowsEditionNote(s.edition).empty())
            t.detail += "; no Mac bonus rides";
    } else if (s.present == 0) {
        t.state = Files(s.files) + " to find";
        t.detail = "Drag in " + DiscsNeeded(s) + " to unlock";
    } else {
        t.state = FormatCount(s.filesPresent) + " of " + FormatCount(s.files) + " confirmed";
        t.detail = "Drag in " + DiscsNeeded(s) + " to unlock";
    }
    return t;
}

TileText DescribeSlot(const EditionStatus all[6], int slot) {
    const EditionStatus& shown = SlotStatus(all, slot);
    TileText t = DescribeTile(shown);
    if (slot > 1) return t;
    const EditionStatus& mac = all[StatusIndex(kEditions[slot])];
    const EditionStatus& windows = all[StatusIndex(WindowsEdition(slot))];
    if (mac.present || windows.present || !mac.required || !windows.required) return t;
    // Nothing yet: either disc unlocks the tile. The European PC CD is the Mac edition's disc too, so it alone is named.
    const std::string macDiscs = DiscsNeeded(mac), windowsDiscs = DiscsNeeded(windows);
    t.detail = "Drag in " +
               (macDiscs.find(windowsDiscs) != std::string::npos ? windowsDiscs : macDiscs + " or " + windowsDiscs) +
               " to unlock";
    return t;
}

bool LooksLikeFirstRelease(const ImportReport& report) {
    const EditionStatus& us = report.status[StatusIndex(assets::kEditionUsPc)];
    const EditionStatus& eur = report.status[StatusIndex(assets::kEditionEurPc)];
    return !report.cancelled && !report.aborted && !us.unlocked && us.filesPresent > eur.filesPresent &&
           !report.differentVersions.empty() && us.missingFiles[assets::kSourcePcCd] > 0;
}

bool LooksLikeOlderPressing(const ImportReport& report) {
    const EditionStatus& eur = report.status[1];
    return !report.cancelled && !report.aborted && !eur.unlocked && eur.filesPresent > 0 &&
           !report.differentVersions.empty() && eur.missingFiles[assets::kSourcePcCd] > 0 &&
           !report.status[StatusIndex(assets::kEditionEurPc)].unlocked && !LooksLikeFirstRelease(report);
}

ResultText DescribeResult(const ImportReport& report, const EditionStatus before[6]) {
    ResultText r;
    const EditionStatus* after = report.status;
    if (report.aborted) {
        r.headline = "The check stopped before the end";
        if (!report.errors.empty()) r.lines.push_back(report.errors.front());
        r.lines.push_back("Nothing half-written was kept. " + Files(report.written) +
                          " were put in place before it stopped.");
        return r;
    }
    if (report.cancelled) {
        r.headline = "Check cancelled";
        r.lines.push_back(Files(report.written) + " were put in place before you cancelled.");
        return r;
    }

    bool errorShown = false;
    std::vector<std::string> unlockedNow;
    for (assets::Edition e : kResultEditions) {
        const int i = StatusIndex(e);
        // A Windows edition is not named when its tile's Mac edition is ready: the tile shows that one.
        if (assets::IsWindowsEdition(e) && after[StatusIndex(kEditions[EditionIndex(e)])].unlocked) continue;
        if (!before[i].unlocked && after[i].unlocked) unlockedNow.push_back(EditionPhrase(e));
    }

    if (LooksLikeFirstRelease(report)) {
        const EditionStatus& us = after[StatusIndex(assets::kEditionUsPc)];
        r.headline = "First release, " + FormatCount(us.filesPresent) + " of " + FormatCount(us.files) + " confirmed";
        r.lines.push_back("This US Windows CD is the first release of the game (1.0). " +
                          Files(report.differentVersions.size()) + " on it are another version (not used).");
        r.lines.push_back("Install it on Windows, apply EA's official v2.0 patch, then drag in the installed game folder "
                          "together with the CD (the music and films stay on the CD).");
        std::string missing = MissingText(us);
        if (!missing.empty()) r.lines.push_back(missing + ".");
    } else if (LooksLikeOlderPressing(report)) {
        const EditionStatus& eur = after[1];
        r.headline = "Older pressing, " + FormatCount(eur.filesPresent) + " of " + FormatCount(eur.files) + " confirmed";
        r.lines.push_back("This European PC CD is an older pressing. " + Files(report.differentVersions.size()) +
                          " on it are a different version (not used).");
        std::string missing = MissingText(eur);
        if (!missing.empty()) r.lines.push_back(missing + ".");
    } else if (!unlockedNow.empty()) {
        r.headline = "All assets confirmed for " + JoinAnd(unlockedNow);
        r.good = true;
    } else if (!report.playStation.empty()) {
        r.headline = report.playStationConverted ? "PlayStation rides ready" : "PlayStation disc recognised";
        r.good = report.playStationConverted;
    } else if (report.written == 0 && !report.errors.empty()) {
        // A damaged or unreadable drop must not read as "nothing new": name the file and the reason first.
        r.headline = report.errors.size() == 1 ? "A file could not be read" : "Some files could not be read";
        r.lines.push_back(report.errors.front());
        errorShown = true;
    } else if (report.written == 0 && report.alreadyThere > 0) {
        r.headline = "Nothing new: these files were already in place";
        r.good = true;
    } else if (report.written == 0) {
        r.headline = "No game files found";
        r.lines.push_back(report.unknownFiles ? Files(report.unknownFiles) +
                                                    " looked at; none of them belongs to either game."
                                              : "The drop held no files.");
    } else {
        // Progress without an unlock: name what the edition that moved most still needs.
        int best = -1;
        size_t gain = 0;
        for (assets::Edition e : kResultEditions) {
            const int i = StatusIndex(e);
            if (after[i].unlocked || after[i].filesPresent < before[i].filesPresent) continue;
            size_t g = after[i].filesPresent - before[i].filesPresent;
            if (best < 0 || g > gain) {
                best = i;
                gain = g;
            }
        }
        std::string missing = best >= 0 ? MissingText(after[best]) : std::string();
        r.headline = missing.empty() ? Files(report.written) + " put in place" : missing;
    }

    // One line a tile, for the edition it shows (SlotStatus), when either of its editions moved.
    for (int slot = 0; slot < 3; ++slot) {
        const EditionStatus& shown = SlotStatus(after, slot);
        bool changed = false;
        for (assets::Edition e : {kEditions[slot], WindowsEdition(slot)}) {
            const int i = StatusIndex(e);
            changed = changed || after[i].filesPresent != before[i].filesPresent || after[i].unlocked != before[i].unlocked;
        }
        if (!changed && !(report.written == 0 && report.alreadyThere > 0 && shown.filesPresent > 0)) continue;
        if (shown.unlocked)
            r.lines.push_back(EditionLabel(shown.edition) + ": ready to play");
        else
            r.lines.push_back(EditionLabel(shown.edition) + ": " + FormatCount(shown.filesPresent) + " of " +
                              FormatCount(shown.files) + " files confirmed");
    }
    for (const std::string& line : report.playStation) r.lines.push_back(line);
    if (report.written) r.lines.push_back(Files(report.written) + " put in place");
    if (report.alreadyThere) r.lines.push_back(Files(report.alreadyThere) + " already in place");
    if (!report.differentVersions.empty() && !LooksLikeOlderPressing(report) && !LooksLikeFirstRelease(report))
        r.lines.push_back(Files(report.differentVersions.size()) + " of a different version (not used)");
    if (report.unknownFiles)
        r.lines.push_back(Files(report.unknownFiles) + " not part of either game (not used)");
    if (errorShown) {
        if (report.errors.size() > 1) r.lines.push_back(FormatCount(report.errors.size()) + " problems in all");
    } else if (!report.errors.empty()) {
        const char* joint = report.errors.size() == 1 ? " problem: " : " problems, the first: ";
        r.lines.push_back(FormatCount(report.errors.size()) + joint + report.errors.front());
    }
    return r;
}

bool DescribeProgress(const ImportProgress& p, LogRow& row) {
    switch (p.kind) {
        case ImportProgress::Kind::Written:
            row = {p.path, "confirmed", LogRow::Good};
            return true;
        case ImportProgress::Kind::AlreadyThere:
            row = {p.path, "already in place", LogRow::Same};
            return true;
        case ImportProgress::Kind::DifferentVersion:
            row = {p.path, "different version (not used)", LogRow::Odd};
            return true;
        case ImportProgress::Kind::Error:
            row = {p.message.empty() ? p.path : p.message, "problem", LogRow::Bad};
            return true;
        case ImportProgress::Kind::Content:
            row = {p.message, "", LogRow::Same};
            return true;
        case ImportProgress::Kind::Finished:
            row = {p.message, "done", LogRow::Same};
            return true;
        case ImportProgress::Kind::Scanning:
        case ImportProgress::Kind::Hashing:
            return false;
    }
    return false;
}

}  // namespace tpw::ui
