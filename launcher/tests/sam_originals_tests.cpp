// The built-in original .sam files (launcher/sam_originals.h) and the line-by-line repair (assets/sam_defaults.h).
#include "sam_defaults.h"
#include "sam_originals.h"
#include "manifest.h"
#include "sha256.h"
#include <cstring>
#include <iostream>

using namespace tpw::assets;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; ++failures; } } while (0)

int main() {
    CHECK(SamDefaultCount() == 0 && !FindSamDefault("us-pc", "data/high.sam"));   // none until the launcher registers them
    tpw::launcher::RegisterSamOriginals();
    CHECK(SamDefaultCount() == tpw::launcher::SamOriginalCount() && SamDefaultCount() > 0);
    // Generated data must be accepted original content, including every alias.
    size_t pcUs = 0, pcEur = 0, gold = 0;
    for (size_t i = 0; i < SamDefaultCount(); ++i) {
        const auto& d = SamDefaults()[i];
        const std::string edition = d.edition;
        Edition ed = kEditionUsPc;
        if (edition == "us-pc") ++pcUs;
        else if (edition == "eur-pc") { ed = kEditionEurPc; ++pcEur; }
        else if (edition == "gold") ++gold;
        else if (edition == "us") ed = kEditionUs;
        else if (edition == "eur") ed = kEditionEur;
        else CHECK(false);
        const auto* record = FindByDest(ed, d.dest);
        CHECK(record != nullptr);
        bool accepted = false;
        const auto hash = Sha256::Of(d.text, d.size);
        if (record) for (unsigned h = 0; h < record->hash_count; ++h) {
            const auto& expected = RecordHash(*record, h);
            if (expected.size == d.size && std::memcmp(expected.sha256, hash.bytes, 32) == 0) accepted = true;
        }
        CHECK(accepted);
        CHECK(FindSamDefault(d.edition, d.dest) == &d);
    }
    CHECK(pcUs == 56 && pcEur == 56 && gold == 56);
    CHECK(!FindSamDefault("tpi", "data/high.sam"));
    CHECK(FindSamDefault("us", "data/med.sam")); // supplied by the original Mac CD
    CHECK(FindSamDefault("US-PC", "DATA\\HIGH.SAM"));
    for (size_t i = 0; i < ManifestRecordCount(); ++i) {
        const auto& r = ManifestRecords()[i];
        std::string dest = r.dest;
        for (char& c : dest) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (dest.size() < 4 || dest.substr(dest.size() - 4) != ".sam") continue;
        for (Edition ed : {kEditionUs, kEditionEur, kEditionUsPc, kEditionEurPc}) {
            if ((r.editions & ed) && !FindSamDefault(EditionName(ed), r.dest)) {
                std::cerr << "missing original: " << EditionName(ed) << "/" << r.dest << "\n";
                CHECK(false);
            }
        }
    }

    const std::vector<SamRow> table = {
        {0, "", 0, 0, 0, 0, 0, 0},
        {4, "Value", 0, 0, 0, 0, 0, 0},
        {6, "Bounded", 1, 10, 0, 0, 0, 0},
        {10, "Name", 0, 0, 0, 0, 0, 0},
        {11, "Map", 0, 0, 0, 0, 0, 1},
        {1, "Test", 0, 0, 0, 0, 0, 0},
        {12, "", 0, 0, 0, 0, 0, 0}
    };
    const std::string original = "Test.Value 1\nTest.Bounded 2\nTest.Name \"original\"\n";
    SamRepairResult result;
    std::string out, error;
    std::string edit = "# player\r\nTest.Value 987\r\nTest.Name \"my edit\"\r\n";
    CHECK(RepairSam(table, original, edit, out, result, error));
    CHECK(out == edit && !result.repaired);
    edit = "Test.Value 987\nthis is damaged\nTest.Name \"my edit\"\nTest.Bounded 999\n";
    CHECK(RepairSam(table, original, edit, out, result, error));
    CHECK(result.repaired && result.kept == 2 && result.discarded == 2);
    CHECK(out.find(original) == 0);
    CHECK(out.find("Test.Value 987") > out.find("Test.Value 1"));
    CHECK(out.find("Test.Name \"my edit\"") != std::string::npos);
    CHECK(out.find("999") == std::string::npos);
    CHECK(SamUnreadableLine(table, out) == 0);
    std::string again;
    CHECK(RepairSam(table, original, out, again, result, error) && out == again && !result.repaired);
    // Valid maps and combined fields survive, invalid/unclosed maps do not eat later edits.
    edit = "bad\nTest.Value.Bounded 77 3\nTest.Map\n---\n.Q\n---\nTest.Value 88\n";
    CHECK(RepairSam(table, original, edit, out, result, error));
    CHECK(result.kept == 3 && out.find(".Q\n---") != std::string::npos);
    edit = "Test.Map\n---\ninvalid!\nTest.Value 88\n";
    CHECK(RepairSam(table, original, edit, out, result, error));
    CHECK(out.find("Test.Value 88") != std::string::npos && out.find("invalid!") == std::string::npos);
    edit = std::string("bad\0line\n", 9) + "Test.Value 66\n";
    CHECK(RepairSam(table, original, edit, out, result, error));
    CHECK(out.find("Test.Value 66") != std::string::npos && out.find('\0') == std::string::npos);
    CHECK(RepairSam(table, original, "garbage", out, result, error));
    CHECK(result.repaired && result.kept == 0 && out.find(original) == 0);
    out = "untouched";
    CHECK(!RepairSam(std::vector<SamRow>{}, original, edit, out, result, error) && out == "untouched");
    CHECK(!RepairSam(table, "broken", edit, out, result, error) && out == "untouched");
    std::cout << "sam defaults: " << SamDefaultCount() << " generated records, " << failures << " failures\n";
    return failures ? 1 : 0;
}
