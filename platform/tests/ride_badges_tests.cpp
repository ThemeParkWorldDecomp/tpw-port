// ride_badges_tests.cpp: the ride badges (include/RideBadges.h, platform/ride_badges.h): which rides get the "GOLD" and
// the "PS1" tag on the Buy Ride panel, that with "Show Ride Badges" off no ride gets one (so nothing is drawn), and where
// the tag goes in the picture. The game folder is a temporary one; no window and no game.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "RideBadges.h"
#include "ride_badges.h"

namespace fs = std::filesystem;

namespace {

int gFailures = 0;
int gChecks = 0;

void Check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::fprintf(stderr, "FAILED: %s\n", what.c_str());
    }
}

void Touch(const fs::path& file) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << "x";
}

tpw::GoldRide Gold(const std::string& folder, const std::string& name) {
    tpw::GoldRide ride;
    ride.folder = folder;
    ride.name = name;
    return ride;
}

tpw::ContentRide Content(const std::string& folder, const fs::path& host) {
    tpw::ContentRide ride;
    ride.folder = folder;
    ride.host = host;
    return ride;
}

bool Has(const std::vector<std::string>& keys, const std::string& key) {
    for (const std::string& k : keys)
        if (k == key) return true;
    return false;
}

// The keys: the game's own folder (RideInfo::mFolder), a Gold ride's place and name and a content ride's folder name the
// same ride the same way.
void TestKeys() {
    Check(RideBadgeKey("data:levels:space:rides:_gforce_35:") == "data/levels/space/rides/_gforce_35", "mFolder key");
    Check(RideBadgeKey("data/levels/space/rides/_gforce_35.wad") == "data/levels/space/rides/_gforce_35", "Gold place key");
    Check(RideBadgeKey("DATA\\Levels\\Space\\Rides\\_GFORCE_35.WAD") == "data/levels/space/rides/_gforce_35",
          "backslashes and upper case");
    Check(RideBadgeKey("::data::levels:jungle:rides:aztec::") == "data/levels/jungle/rides/aztec", "repeated separators");
    Check(RideBadgeKey("data/levels/jungle/rides/aztec") == RideBadgeKey("data:levels:jungle:rides:aztec:"),
          "content folder key");
    Check(RideBadgeKey("").empty(), "empty key");
}

// The Gold rides that get the tag: those the game folder has no copy of, names matched ignoring case.
void TestGoldExtras(const fs::path& scratch) {
    const fs::path game = scratch / "game";
    Touch(game / "data" / "levels" / "jungle" / "rides" / "_snake_1.wad");       // a base ride the Gold CD has again
    Touch(game / "Data" / "Levels" / "Space" / "Rides" / "_WORMHOL_33.WAD");      // the same, in another case
    Touch(game / "data" / "levels" / "space" / "features" / "_pulsar_29.wad");
    const std::vector<tpw::GoldRide> rides = {
        Gold("data/levels/jungle/rides", "_snake_1.wad"),
        Gold("data/levels/jungle/rides", "_king_7.wad"),      // not in the base game
        Gold("data/levels/space/rides", "_wormhol_33.wad"),
        Gold("data/levels/space/features", "_pulsar_29.wad"),
        Gold("data/levels/space/features", "_obelisk_28.wad"),   // 28, the number of the base game's _morpher_27
        Gold("data/levels/space/rides", "_gforce_35.wad"),
        Gold("data/levels/space/rides", ""),                  // no name: never a ride
    };
    const std::vector<std::string> keys = tpw::GoldExtraRideKeys(rides, game);
    Check(keys.size() == 3, "3 Gold extras, got " + std::to_string(keys.size()));
    Check(Has(keys, "data/levels/jungle/rides/_king_7"), "_king_7 is a Gold extra");
    Check(Has(keys, "data/levels/space/features/_obelisk_28"), "_obelisk_28 is a Gold extra");
    Check(Has(keys, "data/levels/space/rides/_gforce_35"), "_gforce_35 is a Gold extra");
    Check(!Has(keys, "data/levels/jungle/rides/_snake_1"), "_snake_1 is a base copy");
    Check(!Has(keys, "data/levels/space/rides/_wormhol_33"), "_wormhol_33 (other case) is a base copy");
    Check(!Has(keys, "data/levels/space/features/_pulsar_29"), "_pulsar_29 is a base copy");
    Check(tpw::GoldExtraRideKeys(rides, fs::path()).empty(), "no game folder: no Gold extras");
    Check(tpw::GoldExtraRideKeys({}, game).empty(), "no Gold rides: no Gold extras");

    // The built-in table against an empty game folder: all 35 rows; each a key of its own.
    const std::vector<std::string> all = tpw::GoldExtraRideKeys(tpw::BuiltInGoldRides(), scratch / "empty");
    Check(all.size() == 35, "the built-in table's 35 rows, got " + std::to_string(all.size()));
}

// The PlayStation rides: only those of the content folder of the mod that switched ps1_badge on.
void TestContentKeys(const fs::path& scratch) {
    const fs::path ps1 = scratch / "assets" / "ps1";
    const fs::path other = scratch / "assets" / "other";
    const std::vector<tpw::ContentRide> rides = {
        Content("data/levels/jungle/rides/aztec", ps1 / "data" / "levels" / "jungle" / "rides" / "aztec"),
        Content("data/levels/space/rides/zap", other / "data" / "levels" / "space" / "rides" / "zap"),
        Content("", ps1 / "data" / "levels" / "space" / "rides" / "nameless"),
        Content("data/levels/hallow/rides/bats", scratch / "assets" / "ps1x" / "bats"),   // a folder that only starts the same
    };
    const std::vector<std::string> keys = tpw::ContentRideKeys(rides, {ps1});
    Check(keys.size() == 1 && keys[0] == "data/levels/jungle/rides/aztec", "only the ps1 folder's ride");
    Check(tpw::ContentRideKeys(rides, {}).empty(), "no ps1_badge mod: no PS1 rides");
    Check(tpw::ContentRideKeys(rides, {ps1 / ""}).size() == 1, "a root with a trailing separator");
}

// Which badge each ride has, and none with the switch off.
void TestBadgeFor() {
    gRideBadgeGold = {"data/levels/space/rides/_gforce_35"};
    gRideBadgePs1 = {"data/levels/jungle/rides/aztec"};
    gModPs1Badge = true;

    gPortRideBadges = false;
    Check(RideBadgeFor("data:levels:space:rides:_gforce_35:") == RideBadge::kNone, "off: Gold ride has none");
    Check(RideBadgeFor("data:levels:jungle:rides:aztec:") == RideBadge::kNone, "off: PS1 ride has none");
    Check(RideBadgeText(RideBadgeFor("data:levels:space:rides:_gforce_35:")) == nullptr, "off: no text, nothing drawn");

    gPortRideBadges = true;
    Check(RideBadgeFor("data:levels:space:rides:_gforce_35:") == RideBadge::kGold, "Gold ride");
    Check(RideBadgeFor("DATA:LEVELS:SPACE:RIDES:_GFORCE_35:") == RideBadge::kGold, "Gold ride, other case");
    Check(RideBadgeFor("data:levels:jungle:rides:aztec:") == RideBadge::kPs1, "PS1 ride");
    Check(RideBadgeFor("data:levels:space:rides:_wormhol_33:") == RideBadge::kNone, "base ride");
    Check(RideBadgeFor("data:levels:space:rides:_gforce_3:") == RideBadge::kNone, "no partial match");
    Check(RideBadgeFor("") == RideBadge::kNone && RideBadgeFor(nullptr) == RideBadge::kNone, "no folder");

    gModPs1Badge = false;   // the ps1-rides mod off: its rides lose the tag, the Gold ones keep theirs
    Check(RideBadgeFor("data:levels:jungle:rides:aztec:") == RideBadge::kNone, "PS1 ride without the mod's hook");
    Check(RideBadgeFor("data:levels:space:rides:_gforce_35:") == RideBadge::kGold, "Gold ride without the mod's hook");

    const char16_t* gold = RideBadgeText(RideBadge::kGold);
    const char16_t* ps1 = RideBadgeText(RideBadge::kPs1);
    Check(gold != nullptr && std::u16string(gold) == u"GOLD", "GOLD text");
    Check(ps1 != nullptr && std::u16string(ps1) == u"PS1", "PS1 text");
    Check(RideBadgeText(RideBadge::kNone) == nullptr, "none: no text");
}

// The launcher's call: on by default (no ride_badges key), off only with "0"; off empties the lists and leaves the switch
// off, so nothing is drawn.
void TestUse(const fs::path& scratch) {
    Check(tpw::RideBadgesSetting(""), "no ride_badges key: on (the default)");
    Check(tpw::RideBadgesSetting("1"), "ride_badges=1: on");
    Check(!tpw::RideBadgesSetting("0"), "ride_badges=0: off");
    gRideBadgeGold = {"a"};
    gRideBadgePs1 = {"b"};
    gPortRideBadges = true;
    tpw::UseRideBadges(false, scratch / "game", {scratch / "assets" / "ps1"});
    Check(!gPortRideBadges && gRideBadgeGold.empty() && gRideBadgePs1.empty(), "off empties the lists");
    Check(RideBadgeFor("a") == RideBadge::kNone, "off: nothing tagged");
    tpw::UseRideBadges(true, scratch / "game", {});
    Check(gPortRideBadges, "on sets the switch");
    Check(gRideBadgeGold.empty() && gRideBadgePs1.empty(), "on with no Gold rides and no content rides: no tags");
    gRideBadgeGold = {"data/levels/space/rides/_gforce_35"};
    gModPs1Badge = true;
    gRideBadgePs1 = {"data/levels/jungle/rides/aztec"};
    tpw::UseRideBadges(tpw::RideBadgesSetting("0"), scratch / "game", {});
    Check(RideBadgeFor("data:levels:space:rides:_gforce_35:") == RideBadge::kNone &&
              RideBadgeFor("data:levels:jungle:rides:aztec:") == RideBadge::kNone,
          "the switch off: neither tag, with the ps1_badge hook on");
    gModPs1Badge = false;
    gPortRideBadges = false;
}

// The tag's place: the top left corner, inside the picture, its text inside the box, nothing when it does not fit.
void TestPlace() {
    for (const int32_t scale : {1, 2, 3}) {   // the picture at 640x480 and at larger windows
        const int32_t w = 150 * scale, h = 110 * scale, tw = 28 * scale, th = 10 * scale;
        const RideBadgeBox box = PlaceRideBadge(w, h, tw, th);
        const std::string at = " at x" + std::to_string(scale);
        Check(box.right > box.left && box.bottom > box.top, "a box" + at);
        Check(box.left >= 0 && box.top >= 0 && box.right <= w && box.bottom <= h, "inside the picture" + at);
        Check(box.left == box.top && box.top >= 2, "a margin, the same left and top" + at);
        Check(box.right < w / 2 && box.bottom < h / 2, "the top left corner" + at);
        Check(box.textLeft > box.left && box.textTop > box.top && box.textLeft + tw < box.right &&
                  box.textTop + th < box.bottom,
              "the text inside the box" + at);
    }
    const RideBadgeBox none = PlaceRideBadge(20, 10, 28, 10);
    Check(none.left == 0 && none.right == 0 && none.top == 0 && none.bottom == 0, "too small: no box");
    const RideBadgeBox empty = PlaceRideBadge(150, 110, 0, 10);
    Check(empty.right == 0 && empty.bottom == 0, "no text: no box");
}

}  // namespace

int main() {
    std::error_code err;
    const fs::path scratch = fs::temp_directory_path(err) / ("tpw_ride_badges_tests_" + std::to_string(std::rand()));
    fs::remove_all(scratch, err);
    fs::create_directories(scratch, err);
    TestKeys();
    TestGoldExtras(scratch);
    TestContentKeys(scratch);
    TestBadgeFor();
    TestUse(scratch);
    TestPlace();
    fs::remove_all(scratch, err);
    std::printf("ride badges: %d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
