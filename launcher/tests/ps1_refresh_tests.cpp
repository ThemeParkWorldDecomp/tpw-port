// ps1_refresh_tests.cpp: the PlayStation rides an older converter wrote (launcher/ps1_refresh.h). With no converted rides
// nothing happens; rides stamped with this build's converter are current; stale rides are converted again from the
// remembered disc when it is still there and unchanged (and not otherwise, nor when its conversion fails: the disc is
// asked for again). The content folder, the stamp and the disc are the test's own files in a scratch folder.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ps1_convert.h"
#include "ps1_refresh.h"
#include "sha256.h"

namespace fs = std::filesystem;
using namespace tpw::launcher;
using Result = Ps1Refresh::Result;

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
    if (const char* v = std::getenv("TPW_TEST_SCRATCH"); v != nullptr && *v != '\0') return fs::u8path(v) / "ps1_refresh";
    return fs::temp_directory_path() / "tpw-ps1-refresh-test";
}

// A data folder whose ps1 content folder holds `rides` converted rides (0: none), with or without the stamp.
fs::path DataRoot(const fs::path& scratch, const char* name, int rides, int stamp) {
    const fs::path root = scratch / name;
    fs::remove_all(root);
    fs::create_directories(root / "assets" / "ps1");
    const char* const folders[] = {"jungle/rides/_jack_201", "hallow/rides/_obver_202", "space/features/_tower_203"};
    for (int i = 0; i < rides && i < 3; ++i) {
        const fs::path dir = root / "assets" / "ps1" / "data" / "levels" / fs::u8path(folders[i]);
        const std::string sam = dir.filename().u8string() + ".sam";
        Write(dir / sam, "#\r\nInfo.Id\t\t\t1187\r\nInfo.Name\t\t\"Ride " + std::to_string(201 + i) +
                             "\"\r\nInfo.DownloadedRideNumber\t" + std::to_string(201 + i) + "\r\n");
    }
    if (stamp > 0) Write(root / "assets" / "ps1" / "converter.txt", std::to_string(stamp) + "\n");
    return root;
}

struct FakeConverter {
    int calls = 0;
    std::string lastPath;
    bool ok = true;
    fs::path dataRoot;
    Ps1Converter Fn() {
        return [this](const std::string& path, std::string& reason) {
            ++calls;
            lastPath = path;
            if (!ok) {
                reason = "the fake converter says no";
                return false;
            }
            std::string why;
            return tpw::assets::WritePs1ConverterStamp(dataRoot, why);
        };
    }
};

}  // namespace

int main() {
    const fs::path scratch = Scratch();
    fs::create_directories(scratch);
    const fs::path disc = scratch / "disc.bin";
    Write(disc, std::string(5000, 'P') + "PLAYSTATION");
    const std::string hash = Sha256OfFile(disc);
    Check(hash.size() == 64, "the disc's hash is 64 hexadecimal characters");
    Check(hash == tpw::assets::Sha256::Of((std::string(5000, 'P') + "PLAYSTATION").data(), 5011).Hex(),
          "Sha256OfFile hashes the whole file");
    Check(Sha256OfFile(scratch / "missing.bin").empty(), "a missing file has no hash");
    const std::string discPath = disc.u8string();

    {   // no converted rides: nothing to do, whatever launcher.ini remembers
        const fs::path root = DataRoot(scratch, "none", 0, 0);
        FakeConverter fake;
        fake.dataRoot = root;
        const Ps1Refresh r = RefreshPs1Rides(root / "assets" / "ps1", root, discPath, hash, fake.Fn());
        Check(r.result == Result::NoRides, "no rides: NoRides");
        Check(fake.calls == 0, "no rides: the converter is not called");
        Check(r.line.empty(), "no rides: no log line");
        Check(!Ps1RidesStale(root / "assets" / "ps1", root), "no rides: not stale");
    }
    {   // rides with this build's stamp: current
        const fs::path root = DataRoot(scratch, "current", 3, tpw::assets::kPs1ConverterVersion);
        FakeConverter fake;
        fake.dataRoot = root;
        const Ps1Refresh r = RefreshPs1Rides(root / "assets" / "ps1", root, discPath, hash, fake.Fn());
        Check(r.result == Result::Current, "current stamp: Current");
        Check(r.stamp == tpw::assets::kPs1ConverterVersion, "current stamp: the stamp is read");
        Check(fake.calls == 0, "current stamp: the converter is not called");
        Check(!Ps1RidesStale(root / "assets" / "ps1", root), "current stamp: not stale");
    }
    {   // a newer stamp (a later launcher's rides): left alone
        const fs::path root = DataRoot(scratch, "newer", 3, tpw::assets::kPs1ConverterVersion + 5);
        FakeConverter fake;
        fake.dataRoot = root;
        const Ps1Refresh r = RefreshPs1Rides(root / "assets" / "ps1", root, discPath, hash, fake.Fn());
        Check(r.result == Result::Current, "newer stamp: Current");
        Check(fake.calls == 0, "newer stamp: the converter is not called");
    }
    {   // 0.4.0's unstamped rides and the remembered disc, unchanged: converted again, and current afterwards
        const fs::path root = DataRoot(scratch, "stale", 3, 0);
        Check(Ps1RidesStale(root / "assets" / "ps1", root), "unstamped rides: stale");
        FakeConverter fake;
        fake.dataRoot = root;
        const Ps1Refresh r = RefreshPs1Rides(root / "assets" / "ps1", root, discPath, hash, fake.Fn());
        Check(r.result == Result::Converted, "unstamped + disc: Converted");
        Check(r.stamp == 1, "unstamped + disc: the old stamp reads as 1");
        Check(fake.calls == 1 && fake.lastPath == discPath, "unstamped + disc: the converter ran once on the disc");
        Check(r.line.find("converted again (converter 1 -> " + std::to_string(tpw::assets::kPs1ConverterVersion) + ")") !=
                  std::string::npos,
              "unstamped + disc: the log line says converted again: " + r.line);
        Check(!Ps1RidesStale(root / "assets" / "ps1", root), "after converting again: not stale");
        const Ps1Refresh again = RefreshPs1Rides(root / "assets" / "ps1", root, discPath, hash, fake.Fn());
        Check(again.result == Result::Current && fake.calls == 1, "after converting again: Current, no second run");
    }
    {   // an older numbered stamp works the same
        const fs::path root = DataRoot(scratch, "older", 1, 1);
        FakeConverter fake;
        fake.dataRoot = root;
        const Ps1Refresh r = RefreshPs1Rides(root / "assets" / "ps1", root, discPath, hash, fake.Fn());
        Check(r.result == Result::Converted && fake.calls == 1, "stamp 1 + disc: Converted");
    }
    {   // stale rides and nothing remembered (a 0.4.0 or 0.4.1 import): the disc is asked for again
        const fs::path root = DataRoot(scratch, "nodisc", 3, 0);
        FakeConverter fake;
        fake.dataRoot = root;
        const Ps1Refresh r = RefreshPs1Rides(root / "assets" / "ps1", root, "", "", fake.Fn());
        Check(r.result == Result::NeedsDisc, "stale, nothing remembered: NeedsDisc");
        Check(fake.calls == 0, "stale, nothing remembered: the converter is not called");
        Check(r.line.find("no disc remembered") != std::string::npos, "stale, nothing remembered: the line says so");
        Check(Ps1RidesStale(root / "assets" / "ps1", root), "stale, nothing remembered: still stale");
    }
    {   // the remembered disc is gone
        const fs::path root = DataRoot(scratch, "gone", 3, 0);
        FakeConverter fake;
        fake.dataRoot = root;
        const Ps1Refresh r = RefreshPs1Rides(root / "assets" / "ps1", root, (scratch / "gone.bin").u8string(), hash, fake.Fn());
        Check(r.result == Result::NeedsDisc && fake.calls == 0, "stale, disc gone: NeedsDisc, no conversion");
        Check(r.line.find("gone") != std::string::npos, "stale, disc gone: the line says so");
    }
    {   // the remembered disc is another file now (its hash differs): not converted
        const fs::path root = DataRoot(scratch, "changed", 3, 0);
        FakeConverter fake;
        fake.dataRoot = root;
        const Ps1Refresh r = RefreshPs1Rides(root / "assets" / "ps1", root, discPath, std::string(64, '0'), fake.Fn());
        Check(r.result == Result::NeedsDisc && fake.calls == 0, "stale, disc changed: NeedsDisc, no conversion");
        Check(r.line.find("changed") != std::string::npos, "stale, disc changed: the line says so");
        Check(Ps1RidesStale(root / "assets" / "ps1", root), "stale, disc changed: still stale");
    }
    {   // a remembered path without a hash is not trusted
        const fs::path root = DataRoot(scratch, "nohash", 3, 0);
        FakeConverter fake;
        fake.dataRoot = root;
        const Ps1Refresh r = RefreshPs1Rides(root / "assets" / "ps1", root, discPath, "", fake.Fn());
        Check(r.result == Result::NeedsDisc && fake.calls == 0, "stale, no hash: NeedsDisc, no conversion");
    }
    {   // the conversion fails: the rides stay as they were, stale, and the disc is asked for again
        const fs::path root = DataRoot(scratch, "failed", 3, 0);
        FakeConverter fake;
        fake.dataRoot = root;
        fake.ok = false;
        const Ps1Refresh r = RefreshPs1Rides(root / "assets" / "ps1", root, discPath, hash, fake.Fn());
        Check(r.result == Result::Failed && fake.calls == 1, "stale, conversion fails: Failed after one try");
        Check(r.line.find("the fake converter says no") != std::string::npos, "stale, conversion fails: the reason is logged");
        Check(Ps1RidesStale(root / "assets" / "ps1", root), "stale, conversion fails: still stale");
    }
    {   // no converter at all
        const fs::path root = DataRoot(scratch, "noconv", 3, 0);
        const Ps1Refresh r = RefreshPs1Rides(root / "assets" / "ps1", root, discPath, hash, Ps1Converter());
        Check(r.result == Result::Failed, "stale, no converter: Failed");
    }
    fs::remove_all(scratch);
    if (gFailures == 0) std::printf("ps1_refresh: all checks passed\n");
    else std::printf("ps1_refresh: %d check(s) failed\n", gFailures);
    return gFailures == 0 ? 0 : 1;
}
