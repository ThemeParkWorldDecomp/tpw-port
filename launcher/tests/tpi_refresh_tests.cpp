// tpi_refresh_tests.cpp: the Theme Park Inc worlds an older converter wrote (launcher/tpi_refresh.h). With no converted
// worlds nothing happens; worlds stamped with this build's converter are current; stale worlds are converted again from
// the remembered disc when it is still there and unchanged, or from the remembered installed folder while it is still
// there (and not otherwise, nor when the conversion fails: the disc is asked for again). The content folder, the stamp
// and the disc are the test's own files in a scratch folder; the stale cases pass a converter version above the stamp.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ps1_refresh.h"
#include "sha256.h"
#include "tpi_content.h"
#include "tpi_convert.h"
#include "tpi_refresh.h"

namespace fs = std::filesystem;
using namespace tpw::launcher;
using Result = TpiRefresh::Result;

namespace {

int gFailures = 0;

void Check(bool ok, const std::string& what) {
    if (ok) return;
    ++gFailures;
    std::printf("FAILED: %s\n", what.c_str());
}

void Write(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
}

fs::path Scratch() {
    if (const char* v = std::getenv("TPW_TEST_SCRATCH"); v != nullptr && *v != '\0') return fs::u8path(v) / "tpi_refresh";
    return fs::temp_directory_path() / "tpw-tpi-refresh-test";
}

// A data folder whose tpi content folder holds `worlds` converted worlds (0: none), with or without the stamp. The
// level folders are empty: the content's shape is platform/tpi_content.h's, which looks for the folder and the stamp.
fs::path DataRoot(const fs::path& scratch, const char* name, int worlds, int stamp) {
    const fs::path root = scratch / name;
    fs::remove_all(root);
    fs::create_directories(root / "assets" / "tpi");
    for (int i = 0; i < worlds && i < 3; ++i)
        fs::create_directories(root / "assets" / "tpi" / "data" / "levels" / tpw::kTpiWorlds[i].folder);
    if (stamp > 0) Write(root / "assets" / "tpi" / "converter.txt", std::to_string(stamp) + "\n");
    return root;
}

struct FakeConverter {
    int calls = 0;
    std::string lastPath;
    bool ok = true;
    fs::path dataRoot;
    TpiConverter Fn() {
        return [this](const std::string& path, std::string& reason) {
            ++calls;
            lastPath = path;
            if (!ok) {
                reason = "the fake converter says no";
                return false;
            }
            std::string why;
            return tpw::assets::WriteTpiConverterStamp(dataRoot, why);
        };
    }
};

}  // namespace

int main() {
    const fs::path scratch = Scratch();
    fs::create_directories(scratch);
    const fs::path disc = scratch / "disc.iso";
    Write(disc, std::string(5000, 'T') + "THEMEPARKINC");
    const std::string hash = Sha256OfFile(disc);
    Check(hash.size() == 64, "the disc's hash is 64 hexadecimal characters");
    const std::string discPath = disc.u8string();
    const fs::path installed = scratch / "installed";
    Write(installed / "data" / "levels" / "arabian" / "arabian.sam", "#\r\n");
    const std::string installedPath = installed.u8string();
    const int current = tpw::assets::kTpiConverterVersion;   // the build's version: a stamp of it is current
    const int next = current + 1;                            // a later converter: a stamp of `current` is stale to it
    const fs::path content = fs::path("assets") / "tpi";

    {   // no converted worlds: nothing to do, whatever launcher.ini remembers
        const fs::path root = DataRoot(scratch, "none", 0, 0);
        FakeConverter fake;
        fake.dataRoot = root;
        const TpiRefresh r = RefreshTpiWorlds(root / content, root, discPath, hash, fake.Fn());
        Check(r.result == Result::NoWorlds, "no worlds: NoWorlds");
        Check(fake.calls == 0, "no worlds: the converter is not called");
        Check(r.line.empty(), "no worlds: no log line");
        Check(!TpiWorldsStale(root / content, root), "no worlds: not stale");
    }
    {   // world folders without the stamp (a conversion that did not finish): not content yet, nothing to do
        const fs::path root = DataRoot(scratch, "unstamped", 3, 0);
        FakeConverter fake;
        fake.dataRoot = root;
        const TpiRefresh r = RefreshTpiWorlds(root / content, root, discPath, hash, fake.Fn(), next);
        Check(r.result == Result::NoWorlds && fake.calls == 0, "unstamped folders: NoWorlds, the converter is not called");
        Check(!TpiWorldsStale(root / content, root, next), "unstamped folders: not stale");
    }
    {   // worlds with this build's stamp: current
        const fs::path root = DataRoot(scratch, "current", 3, current);
        FakeConverter fake;
        fake.dataRoot = root;
        const TpiRefresh r = RefreshTpiWorlds(root / content, root, discPath, hash, fake.Fn());
        Check(r.result == Result::Current, "current stamp: Current");
        Check(r.stamp == current, "current stamp: the stamp is read");
        Check(fake.calls == 0, "current stamp: the converter is not called");
        Check(!TpiWorldsStale(root / content, root), "current stamp: not stale");
    }
    {   // a newer stamp (a later launcher's worlds): left alone
        const fs::path root = DataRoot(scratch, "newer", 3, current + 5);
        FakeConverter fake;
        fake.dataRoot = root;
        const TpiRefresh r = RefreshTpiWorlds(root / content, root, discPath, hash, fake.Fn());
        Check(r.result == Result::Current && fake.calls == 0, "newer stamp: Current, the converter is not called");
    }
    {   // an older stamp and the remembered disc, unchanged: converted again, and current to that build afterwards
        const fs::path root = DataRoot(scratch, "stale", 3, current);
        Check(TpiWorldsStale(root / content, root, next), "older stamp: stale to the next converter");
        FakeConverter fake;
        fake.dataRoot = root;
        const TpiRefresh r = RefreshTpiWorlds(root / content, root, discPath, hash, fake.Fn(), next);
        Check(r.result == Result::Converted, "older stamp + disc: Converted");
        Check(r.stamp == current, "older stamp + disc: the old stamp is read");
        Check(fake.calls == 1 && fake.lastPath == discPath, "older stamp + disc: the converter ran once on the disc");
        Check(r.line.find("converted again (converter " + std::to_string(current) + " -> " + std::to_string(next) + ")") !=
                  std::string::npos,
              "older stamp + disc: the log line says converted again: " + r.line);
        Check(r.line.find("disc.iso") != std::string::npos, "older stamp + disc: the log line names the disc: " + r.line);
        // The fake converter writes this build's stamp, so to this build the worlds are current again.
        Check(!TpiWorldsStale(root / content, root), "after converting again: not stale");
        const TpiRefresh again = RefreshTpiWorlds(root / content, root, discPath, hash, fake.Fn());
        Check(again.result == Result::Current && fake.calls == 1, "after converting again: Current, no second run");
    }
    {   // a single world is content too (a disc with one world folder left): the same path
        const fs::path root = DataRoot(scratch, "one", 1, current);
        FakeConverter fake;
        fake.dataRoot = root;
        const TpiRefresh r = RefreshTpiWorlds(root / content, root, discPath, hash, fake.Fn(), next);
        Check(r.result == Result::Converted && fake.calls == 1, "one world + disc: Converted");
    }
    {   // the remembered source is an installed folder (no hash): converted again while the folder is there
        const fs::path root = DataRoot(scratch, "folder", 3, current);
        FakeConverter fake;
        fake.dataRoot = root;
        const TpiRefresh r = RefreshTpiWorlds(root / content, root, installedPath, "", fake.Fn(), next);
        Check(r.result == Result::Converted, "older stamp + installed folder: Converted");
        Check(fake.calls == 1 && fake.lastPath == installedPath, "older stamp + installed folder: the converter ran on the folder");
        Check(r.line.find("installed") != std::string::npos, "older stamp + installed folder: the log line names it: " + r.line);
    }
    {   // the remembered installed folder is gone
        const fs::path root = DataRoot(scratch, "foldergone", 3, current);
        FakeConverter fake;
        fake.dataRoot = root;
        const TpiRefresh r = RefreshTpiWorlds(root / content, root, (scratch / "gone-folder").u8string(), "", fake.Fn(), next);
        Check(r.result == Result::NeedsDisc && fake.calls == 0, "stale, folder gone: NeedsDisc, no conversion");
        Check(r.line.find("folder is gone") != std::string::npos, "stale, folder gone: the line says so: " + r.line);
        Check(TpiWorldsStale(root / content, root, next), "stale, folder gone: still stale");
    }
    {   // a remembered disc file without a hash is a folder path to the launcher: a file is not a folder
        const fs::path root = DataRoot(scratch, "nohash", 3, current);
        FakeConverter fake;
        fake.dataRoot = root;
        const TpiRefresh r = RefreshTpiWorlds(root / content, root, discPath, "", fake.Fn(), next);
        Check(r.result == Result::NeedsDisc && fake.calls == 0, "stale, disc without hash: NeedsDisc, no conversion");
    }
    {   // stale worlds and nothing remembered: the disc is asked for again
        const fs::path root = DataRoot(scratch, "nodisc", 3, current);
        FakeConverter fake;
        fake.dataRoot = root;
        const TpiRefresh r = RefreshTpiWorlds(root / content, root, "", "", fake.Fn(), next);
        Check(r.result == Result::NeedsDisc, "stale, nothing remembered: NeedsDisc");
        Check(fake.calls == 0, "stale, nothing remembered: the converter is not called");
        Check(r.line.find("no disc remembered") != std::string::npos, "stale, nothing remembered: the line says so");
        Check(TpiWorldsStale(root / content, root, next), "stale, nothing remembered: still stale");
    }
    {   // the remembered disc is gone
        const fs::path root = DataRoot(scratch, "gone", 3, current);
        FakeConverter fake;
        fake.dataRoot = root;
        const TpiRefresh r = RefreshTpiWorlds(root / content, root, (scratch / "gone.iso").u8string(), hash, fake.Fn(), next);
        Check(r.result == Result::NeedsDisc && fake.calls == 0, "stale, disc gone: NeedsDisc, no conversion");
        Check(r.line.find("disc is gone") != std::string::npos, "stale, disc gone: the line says so: " + r.line);
    }
    {   // the remembered disc is another file now (its hash differs): not converted
        const fs::path root = DataRoot(scratch, "changed", 3, current);
        FakeConverter fake;
        fake.dataRoot = root;
        const TpiRefresh r = RefreshTpiWorlds(root / content, root, discPath, std::string(64, '0'), fake.Fn(), next);
        Check(r.result == Result::NeedsDisc && fake.calls == 0, "stale, disc changed: NeedsDisc, no conversion");
        Check(r.line.find("changed") != std::string::npos, "stale, disc changed: the line says so");
        Check(TpiWorldsStale(root / content, root, next), "stale, disc changed: still stale");
    }
    {   // the conversion fails: the worlds stay as they were, stale, and the disc is asked for again
        const fs::path root = DataRoot(scratch, "failed", 3, current);
        FakeConverter fake;
        fake.dataRoot = root;
        fake.ok = false;
        const TpiRefresh r = RefreshTpiWorlds(root / content, root, discPath, hash, fake.Fn(), next);
        Check(r.result == Result::Failed && fake.calls == 1, "stale, conversion fails: Failed after one try");
        Check(r.line.find("the fake converter says no") != std::string::npos, "stale, conversion fails: the reason is logged");
        Check(TpiWorldsStale(root / content, root, next), "stale, conversion fails: still stale");
    }
    {   // no converter at all
        const fs::path root = DataRoot(scratch, "noconv", 3, current);
        const TpiRefresh r = RefreshTpiWorlds(root / content, root, discPath, hash, TpiConverter(), next);
        Check(r.result == Result::Failed, "stale, no converter: Failed");
        Check(r.line.find("no converter") != std::string::npos, "stale, no converter: the line says so");
    }
    fs::remove_all(scratch);
    if (gFailures == 0) std::printf("tpi_refresh: all checks passed\n");
    else std::printf("tpi_refresh: %d check(s) failed\n", gFailures);
    return gFailures == 0 ? 0 : 1;
}
