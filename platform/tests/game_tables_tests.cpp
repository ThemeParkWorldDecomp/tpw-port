// game_tables_tests.cpp: the run-time table loader (platform/game_tables.cpp) against the configure-time tables
// (devtools/gen_game_tables.py's output in the build folder, compiled into this program). For each program named on the
// command line (the data fork of a full Mac "SimTheme Park"), ReadGameTables runs twice, once by the program's SHA-1 and
// once by the content search, and every table it fills must equal the generated array byte for byte: the response table,
// the message table (record -1 and the spare included), the balance description rows, the 351 scorers (pointers to
// member: this program's own bodies, one per name, so a scorer named wrong points elsewhere), the seven legal texts, the
// 48 window scripts and the ride templates. A program that is absent is skipped with a message (none at all: the
// test is skipped).
// Prints counts and names only, never the tables.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "Advisor.h"
#include "AdvisorScoring.h"
#include "CBalanceTable.h"
#include "platform/game_tables.h"

// The configure-time tables: the four generated sources are linked in; the three included ones are expanded here.
#include "RideTemplates.inc"
#include "SaveLegalText.inc"
#include "WindowScripts.inc"

// One body per scorer, each answering its own 2.04b3 address, so that no two are folded into one.
#define TPW_GAME_TABLE_SCORER(address, name) \
    int32_t CAdvisorScoring::name() { return int32_t(address); }
#include "platform/game_table_lists.inc"
#undef TPW_GAME_TABLE_SCORER

namespace {

const uint32_t kExpectedRides[] = {TPW_RIDE_TEMPLATES};
const char16_t* const kExpectedLegal[] = {kSaveLegalText0, kSaveLegalText1, kSaveLegalText2, kSaveLegalText3,
                                          kSaveLegalText4, kSaveLegalText5, kSaveLegalText6};
size_t LegalLength(int i) {
    const size_t sizes[] = {sizeof kSaveLegalText0, sizeof kSaveLegalText1, sizeof kSaveLegalText2, sizeof kSaveLegalText3,
                            sizeof kSaveLegalText4, sizeof kSaveLegalText5, sizeof kSaveLegalText6};
    return sizes[i] / sizeof(char16_t) - 1;   // without the terminating 0
}

#define TPW_GAME_TABLE_WINDOW_SCRIPT(address, words, array, anchor, whole, ordinal, copies) \
    const int16_t kExpected_##array[] = {TPW_WINDOW_SCRIPT_##address};
#include "platform/game_table_lists.inc"
#undef TPW_GAME_TABLE_WINDOW_SCRIPT

struct ExpectedScript {
    const char* name;
    const int16_t* words;
    size_t count;
};
const ExpectedScript kExpectedScripts[] = {
#define TPW_GAME_TABLE_WINDOW_SCRIPT(address, words, array, anchor, whole, ordinal, copies) \
    {#array, kExpected_##array, sizeof kExpected_##array / sizeof(int16_t)},
#include "platform/game_table_lists.inc"
#undef TPW_GAME_TABLE_WINDOW_SCRIPT
};

int gFailures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("  FAIL %s\n", what.c_str());
        gFailures++;
    }
}

// The loader's output for one program, compared table by table with the generated arrays.
void CompareOne(const char* path, bool searchOnly) {
    using namespace tpw;
    std::vector<AdvisorLine> lines(kGameTableAdvisorLines);
    std::vector<AdvisorMessageRecord> messages(kGameTableMessageRecords + 2);
    std::vector<BalanceDescription> balance(kGameTableBalanceRows);
    std::vector<AdvisorScorer> scorers(kGameTableMessageRecords);
    std::vector<std::u16string> legal(kGameTableLegalTexts);
    std::vector<uint32_t> rides(kGameTableRideTemplateWords);
    std::vector<std::vector<int16_t>> scripts(kGameTableWindowScripts);
    // Junk in every destination first, so that a byte the loader leaves alone shows.
    std::memset(static_cast<void*>(lines.data()), 0xA5, sizeof(AdvisorLine) * lines.size());
    std::memset(static_cast<void*>(messages.data()), 0xA5, sizeof(AdvisorMessageRecord) * messages.size());
    std::memset(static_cast<void*>(balance.data()), 0xA5, sizeof(BalanceDescription) * balance.size());
    std::memset(static_cast<void*>(scorers.data()), 0xA5, sizeof(AdvisorScorer) * scorers.size());
    std::memset(rides.data(), 0xA5, sizeof(uint32_t) * rides.size());
    GameTableDestinations to = {lines.data(), messages.data(), balance.data(), scorers.data(), legal.data(), {}, rides.data()};
    for (int i = 0; i < kGameTableWindowScripts; i++) {
        scripts[i].assign(size_t(GameTableWindowScriptWords(i)), int16_t(0x5A5A));
        to.windowScripts[i] = scripts[i].data();
    }
    std::string build, why;
    const bool ok = ReadGameTables(path, to, searchOnly, &build, &why);
    std::printf("  %s: %s\n", searchOnly ? "content search" : "by SHA-1", ok ? build.c_str() : ("refused: " + why).c_str());
    Check(ok, "the tables are read");
    if (!ok) return;
    Check(searchOnly ? build == "found by content" : build != "found by content", "the build is named as expected");
    Check(std::memcmp(lines.data(), gAdvisorLines, sizeof gAdvisorLines) == 0, "gAdvisorLines (611 rows)");
    Check(std::memcmp(messages.data(), gAdvisorMessageBlock, sizeof gAdvisorMessageBlock) == 0,
          "gAdvisorMessageBlock (351 records, record -1 and the spare)");
    Check(std::memcmp(balance.data(), gAdvisorBalanceDescriptions, sizeof gAdvisorBalanceDescriptions) == 0,
          "gAdvisorBalanceDescriptions (775 rows)");
    Check(std::memcmp(scorers.data(), gAdvisorScorerSources, sizeof gAdvisorScorerSources) == 0,
          "gAdvisorScorerSources (351 pointers to member)");
    int sameScorers = 0;
    for (int k = 0; k < kGameTableMessageRecords; k++) sameScorers += scorers[k] == gAdvisorScorerSources[k];
    Check(sameScorers == kGameTableMessageRecords, "the scorers compare equal as pointers to member (" +
                                                       std::to_string(sameScorers) + " of 351)");
    for (int i = 0; i < kGameTableLegalTexts; i++)
        Check(legal[i].size() == LegalLength(i) &&
                  std::memcmp(legal[i].c_str(), kExpectedLegal[i], sizeof(char16_t) * (LegalLength(i) + 1)) == 0,
              "legal text " + std::to_string(i));
    for (int i = 0; i < kGameTableWindowScripts; i++) {
        const ExpectedScript& e = kExpectedScripts[i];
        Check(std::strcmp(e.name, GameTableWindowScriptName(i)) == 0 && scripts[i].size() == e.count &&
                  std::memcmp(scripts[i].data(), e.words, sizeof(int16_t) * e.count) == 0,
              std::string("window script ") + e.name);
    }
    Check(sizeof kExpectedRides == sizeof(uint32_t) * rides.size() &&
              std::memcmp(rides.data(), kExpectedRides, sizeof kExpectedRides) == 0,
          "gRideTemplates (728 words)");
}

}  // namespace

int main(int argc, char** argv) {
    int compared = 0;
    for (int a = 1; a < argc; a++) {
        std::printf("%s\n", argv[a]);
        if (!std::ifstream(argv[a], std::ios::binary)) {
            std::printf("  skipped: the program is absent (the tests read it in place, from your own copy of the game)\n");
            continue;
        }
        CompareOne(argv[a], false);
        CompareOne(argv[a], true);
        compared++;
    }
    // A file that is not a program is refused with a reason, never read past its end.
    {
        std::string why;
        tpw::GameTableDestinations to = {};
        Check(!tpw::ReadGameTables(argv[0], to, false, nullptr, &why) && !why.empty(), "a file that is not a program is refused");
        std::printf("not a program: refused (%s)\n", why.c_str());
    }
    if (compared == 0) {
        std::printf("SKIPPED: none of the programs is here\n");
        return gFailures ? 1 : 77;   // ctest's SKIP_RETURN_CODE
    }
    std::printf("%s: %d program(s), %d failure(s)\n", gFailures ? "FAIL" : "PASS", compared, gFailures);
    return gFailures ? 1 : 0;
}
