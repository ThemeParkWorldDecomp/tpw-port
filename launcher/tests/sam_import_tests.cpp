// Importer plumbing with a small table inferred from the generated original.
// Parser grammar/bounds are tested separately with explicit synthetic tables.
#include "importer.h"
#include "sam_defaults.h"
#include "sam_originals.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace tpw::assets;
namespace fs = std::filesystem;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; ++failures; } } while (0)
static void Write(const std::string& path, const std::string& text) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream out(path, std::ios::binary); out << text;
}
static std::string Read(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}
static SamRow Row(int type, const std::string& name) {
    SamRow row{}; row.type = type;
    name.copy(row.name, sizeof row.name - 1);
    return row;
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    // A fresh folder of its own under the given one, removed at the end; a failed run's stays for a look.
    const std::string root = argv[1] + std::string("/sam-import-") +
                             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    if (fs::exists(root)) { std::cerr << "test directory already exists\n"; return 2; }
    tpw::launcher::RegisterSamOriginals();
    const auto* d = FindSamDefault("us-pc", "data/Online.sam");
    CHECK(d);
    if (!d) return 1;
    std::vector<SamRow> rows{Row(0, "")};
    std::string group, firstKey;
    int firstType = 0;
    std::istringstream lines(std::string(d->text, d->size));
    for (std::string line; std::getline(lines, line);) {
        size_t start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos || line[start] == '#') continue;
        size_t dot = line.find('.', start), space = line.find_first_of(" \t", dot);
        CHECK(dot != std::string::npos && space != std::string::npos);
        if (dot == std::string::npos || space == std::string::npos) continue;
        group = line.substr(start, dot - start);
        size_t value = line.find_first_not_of(" \t", space);
        int type = value != std::string::npos && line[value] == '"' ? 10 : 4;
        rows.push_back(Row(type, line.substr(dot + 1, space - dot - 1)));
        if (firstKey.empty()) { firstKey = line.substr(start, space - start); firstType = type; }
    }
    rows.push_back(Row(1, group)); rows.push_back(Row(12, ""));
    SetSamTable(d->dest, rows.data(), rows.size());
    CHECK(SamUnreadableLine(rows, std::string(d->text, d->size)) == 0);
    ImporterConfig cfg; cfg.dataRoot = root; cfg.editions = kEditionUsPc;
    Importer layout(cfg);
    const auto* record = FindByDest(kEditionUsPc, d->dest);
    CHECK(record);
    if (!record) return 1;
    const std::string path = layout.DestinationPath(kEditionUsPc, *record);
    const std::string edit = firstKey + (firstType == 10 ? " \"my edit\"\n" : " 987\n");
    Write(path, edit);
    std::string error;
    {
        Importer imp(cfg); CHECK(imp.Open(error));
        CHECK(Read(path) == edit && imp.Status(kEditionUsPc).edited == 1);
        CHECK(imp.LastOpenCheck().edited.size() == 1);
        Write(path, edit + "damaged line\n" + edit);
        CHECK(imp.VerifyEdition(kEditionUsPc, error) == 0);
        CHECK(imp.Status(kEditionUsPc).repaired == 1);
        CHECK(Read(path).find("damaged line") == std::string::npos);
        CHECK(Read(path).find(edit) != std::string::npos);
        CHECK(imp.EditedFiles(kEditionUsPc).size() == 1 && imp.EditedFiles(kEditionUsPc)[0].repaired);
    }
    {
        Importer imp(cfg); CHECK(imp.Open(error));
        CHECK(imp.Status(kEditionUsPc).repaired == 1 && imp.LastOpenCheck().repaired.size() == 1);
        Write(path, std::string(d->text, d->size));
        CHECK(imp.VerifyEdition(kEditionUsPc, error) == 0);
        CHECK(imp.Status(kEditionUsPc).repaired == 0 && imp.Status(kEditionUsPc).edited == 0);
    }
    // An edited settings file by itself cannot supply a missing disc.
    const std::string missing = root + "/missing-disc";
    Write(root + "/drop/" + d->dest, edit);
    cfg.dataRoot = missing;
    {
        Importer imp(cfg); CHECK(imp.Open(error));
        const auto report = imp.Import({root + "/drop"});
        CHECK(report.errors.empty());
        CHECK(!fs::exists(imp.DestinationPath(kEditionUsPc, *record)));
        CHECK(imp.Status(kEditionUsPc).edited == 0);
    }
    if (!failures) fs::remove_all(root);
    std::cout << "sam import: " << failures << " failures\n";
    return failures ? 1 : 0;
}
