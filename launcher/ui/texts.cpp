// texts.cpp: the launcher screen's wording (texts.h).
#include "texts.h"

#include <cstdio>

namespace tpw::ui {

using assets::EditionStatus;
using assets::ImportProgress;
using assets::ImportReport;

std::string FileUrl(const std::string& path) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string url = "file://";
    for (unsigned char c : path) {
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/' ||
                           c == '-' || c == '.' || c == '_' || c == '~';
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
        case assets::kEditionUs: return 0;
        case assets::kEditionEur: return 1;
        case assets::kEditionGold: return 2;
    }
    return 0;
}

const char* EditionTitle(assets::Edition edition) {
    switch (edition) {
        case assets::kEditionUs: return "US edition";
        case assets::kEditionEur: return "European edition";
        case assets::kEditionGold: return "Gold bonus rides";
    }
    return "?";
}

const char* EditionGame(assets::Edition edition) {
    switch (edition) {
        case assets::kEditionUs: return "Sim Theme Park, Mac CD";
        case assets::kEditionEur: return "Theme Park World, PC CD";
        case assets::kEditionGold: return "PC Gold bonus CD";
    }
    return "?";
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
    }
    return "?";
}
}  // namespace

std::string DiscsNeeded(const EditionStatus& s) {
    std::vector<std::string> discs;
    for (assets::DiscSource source : kSourceOrder)
        if (s.missing[source]) discs.push_back(assets::SourceDiscName(source));
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
    if (from.size() == 1) return head + (total == 1 ? "it is on " : "they are on ") + assets::SourceDiscName(from[0]);
    std::vector<std::string> parts;
    for (assets::DiscSource source : from)
        parts.push_back(FormatCount(s.missingFiles[source]) + " on " + assets::SourceDiscName(source));
    return head + JoinAnd(parts);
}

TileText DescribeTile(const EditionStatus& s) {
    TileText t;
    if (s.required == 0) {
        t.state = "Not available";
        t.detail = "This build has no files listed for it";
    } else if (s.unlocked) {
        t.state = "Ready to play";
        t.detail = Files(s.filesPresent) + " confirmed";
    } else if (s.present == 0) {
        t.state = Files(s.files) + " to find";
        t.detail = "Drag in " + DiscsNeeded(s) + " to unlock";
    } else {
        t.state = FormatCount(s.filesPresent) + " of " + FormatCount(s.files) + " confirmed";
        t.detail = "Drag in " + DiscsNeeded(s) + " to unlock";
    }
    return t;
}

bool LooksLikeOlderPressing(const ImportReport& report) {
    const EditionStatus& eur = report.status[1];
    return !report.cancelled && !report.aborted && !eur.unlocked && eur.filesPresent > 0 &&
           !report.differentVersions.empty() && eur.missingFiles[assets::kSourcePcCd] > 0;
}

ResultText DescribeResult(const ImportReport& report, const EditionStatus before[3]) {
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
    for (int i = 0; i < 3; ++i)
        if (!before[i].unlocked && after[i].unlocked) unlockedNow.push_back(EditionPhrase(kEditions[i]));

    if (LooksLikeOlderPressing(report)) {
        const EditionStatus& eur = after[1];
        r.headline = "Older pressing, " + FormatCount(eur.filesPresent) + " of " + FormatCount(eur.files) + " confirmed";
        r.lines.push_back("This European PC CD is an older pressing. " + Files(report.differentVersions.size()) +
                          " on it are a different version (not used).");
        std::string missing = MissingText(eur);
        if (!missing.empty()) r.lines.push_back(missing + ".");
    } else if (!unlockedNow.empty()) {
        r.headline = "All assets confirmed for " + JoinAnd(unlockedNow);
        r.good = true;
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
        for (int i = 0; i < 3; ++i) {
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

    for (int i = 0; i < 3; ++i) {
        bool changed = after[i].filesPresent != before[i].filesPresent || after[i].unlocked != before[i].unlocked;
        if (!changed && !(report.written == 0 && report.alreadyThere > 0 && after[i].filesPresent > 0)) continue;
        if (after[i].unlocked)
            r.lines.push_back(std::string(EditionTitle(kEditions[i])) + ": ready to play");
        else
            r.lines.push_back(std::string(EditionTitle(kEditions[i])) + ": " + FormatCount(after[i].filesPresent) +
                              " of " + FormatCount(after[i].files) + " files confirmed");
    }
    if (report.written) r.lines.push_back(Files(report.written) + " put in place");
    if (report.alreadyThere) r.lines.push_back(Files(report.alreadyThere) + " already in place");
    if (!report.differentVersions.empty() && !LooksLikeOlderPressing(report))
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
