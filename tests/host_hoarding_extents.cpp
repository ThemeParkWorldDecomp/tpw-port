// host_hoarding_extents.cpp: the park fence's extents a loaded park gets (include/HoardingExtents.h, the port fix
// flyzoneload) on the host, without game data. A made-up 128 x 128 track grid gets fences laid as LLCC_BuildHoardingDB
// leaves them (posts two cells apart, type 0x19, linked in the hoarding mask, the gate a gap between the walk's two ends),
// and the read-only walk (tpw::HoardingWalkExtents) is checked against a model of the original walk that edits the cells
// as it goes (src/game/park/LLCellRide.cpp): a park's start fence, the fence after land is bought beyond it (before and
// after the original walk has cut its crossroads), and a fence with a hole, which must not close, the saved fly zone's
// extents (tpw::HoardingExtentsFromFlyZone) standing in. A tour based on the bought land is inside the fly zone made from
// the walk's extents and outside the one made from the start fence's, which is what a load kept without the fix. Exits
// nonzero on the failed checks' count.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "HoardingExtents.h"

static int gFailures = 0;

static void Check(bool ok, const std::string& what) {
    if (!ok) {
        ++gFailures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

namespace {

using tpw::HoardingExtents;
constexpr int32_t kSide = tpw::kHoardingSide;

struct Point {
    int32_t x, y;
};

struct Cell {
    int32_t type = 7;
    uint8_t links = 0;
};

struct Grid {
    std::vector<Cell> cells = std::vector<Cell>(kSide * kSide);
    Cell& At(int32_t x, int32_t y) { return cells[x + (y << 7)]; }
};

uint8_t HeadingTo(Point from, Point to) {
    if (to.x > from.x) return 4;
    if (to.x < from.x) return 0x40;
    if (to.y > from.y) return 0x10;
    return 1;
}

// Lays a run of fence through the waypoints, two cells a step, each post linked to the ones before and after it on the run
// (the links of posts other runs share are added up, so two runs that cross or meet make a crossroad).
void LayRun(Grid* grid, const std::vector<Point>& waypoints) {
    std::vector<Point> posts{waypoints[0]};
    for (size_t i = 1; i < waypoints.size(); ++i) {
        Point p = posts.back();
        const Point to = waypoints[i];
        while (p.x != to.x || p.y != to.y) {
            p.x += (to.x > p.x) ? 2 : (to.x < p.x) ? -2 : 0;
            p.y += (to.y > p.y) ? 2 : (to.y < p.y) ? -2 : 0;
            posts.push_back(p);
        }
    }
    for (size_t i = 0; i < posts.size(); ++i) {
        Cell& post = grid->At(posts[i].x, posts[i].y);
        post.type = 0x19;
        if (i > 0) post.links |= HeadingTo(posts[i], posts[i - 1]);
        if (i + 1 < posts.size()) post.links |= HeadingTo(posts[i], posts[i + 1]);
    }
}

// The original walk (LLCC_BuildHoardingDB with gLLCC4818 set) on the grid itself: a crossroad's links are cut as it leaves
// it (RemoveNeighbour), and a cell that is no post gives heading 0. False when it has not ended in `cap` steps (the hang).
bool OriginalWalk(Grid* grid, Point start, Point end, int32_t cap, HoardingExtents* out) {
    int32_t x = start.x, y = start.y;
    uint8_t dir = 4;
    HoardingExtents e{0x400, 0x400, 0, 0};
    bool done = false;
    for (int32_t steps = 0; !done; ++steps) {
        if (steps >= cap) return false;
        Cell& cell = grid->At(x, y);
        if (cell.type == 0x19 || cell.links != 0) {
            if (tpw::HoardingIsCorner(cell.links)) {
                uint8_t other = tpw::HoardingOpposite(dir);
                if ((other & cell.links) == 0) other = dir;
                dir = static_cast<uint8_t>(other ^ cell.links);
            }
            if (tpw::HoardingIsCrossRoad(cell.links)) {
                uint8_t found = 0;
                uint8_t probe = tpw::HoardingAddDirection(dir, 0x40);
                do {
                    if (probe & cell.links) found = probe;
                    probe = tpw::HoardingAddDirection(probe, 4);
                } while (found == 0);
                if (dir == found) {
                    cell.links &= static_cast<uint8_t>(dir | tpw::HoardingOpposite(dir));
                } else {
                    cell.links &= static_cast<uint8_t>(~dir);
                    cell.links &= static_cast<uint8_t>(~tpw::HoardingOpposite(found));
                }
                dir = found;
            }
        } else {
            dir = 0;
        }
        if (x == end.x && y == end.y) done = true;
        switch (dir) {
            case 1: y -= 2; break;
            case 4: x += 2; break;
            case 0x10: y += 2; break;
            case 0x40: x -= 2; break;
            default: break;
        }
        if (x < e.minX) e.minX = x;
        if (y < e.minY) e.minY = y;
        if (x > e.maxX) e.maxX = x;
        if (y > e.maxY) e.maxY = y;
    }
    *out = e;
    return true;
}

bool ReadOnlyWalk(const Grid& grid, Point start, Point end, HoardingExtents* out) {
    const auto post = [&grid](int32_t x, int32_t y) {
        const Cell& cell = grid.cells[x + (y << 7)];
        return tpw::HoardingPost{cell.type, cell.links};
    };
    return tpw::HoardingWalkExtents(start.x, start.y, end.x, end.y, post, out);
}

bool Same(const HoardingExtents& a, const HoardingExtents& b) {
    return a.minX == b.minX && a.minY == b.minY && a.maxX == b.maxX && a.maxY == b.maxY;
}

std::string Text(const HoardingExtents& e) {
    char text[64];
    std::snprintf(text, sizeof(text), "%d,%d..%d,%d", e.minX, e.minY, e.maxX, e.maxY);
    return text;
}

bool SameCells(const Grid& a, const Grid& b) {
    for (size_t i = 0; i < a.cells.size(); ++i)
        if (a.cells[i].type != b.cells[i].type || a.cells[i].links != b.cells[i].links) return false;
    return true;
}

// The fly zone FLY_SetFlyZone makes from the extents, and whether a fly base's cell lies in it (FLY_Tick works in cell
// units of 3000: ten of 300 a cell).
struct Zone {
    int32_t b, c, d, a;
};

Zone ZoneOf(const HoardingExtents& e) {
    return {tpw::FlyZoneLow(e.minX), tpw::FlyZoneLow(e.minY), tpw::FlyZoneHigh(e.maxX), tpw::FlyZoneHigh(e.maxY)};
}

bool InZone(const Zone& z, Point cell) {
    const int64_t x = static_cast<int64_t>(cell.x) * 3000, y = static_cast<int64_t>(cell.y) * 3000;
    return x >= z.b && x <= z.d && y >= z.c && y <= z.a;
}

// The park's start: the gate's two ends on row 17 (the walk starts at the right one heading east and ends on the left one).
const Point kStart{51, 17};
const Point kEnd{43, 17};
const std::vector<Point> kStartFence{{51, 17}, {71, 17}, {71, 81}, {11, 81}, {11, 17}, {43, 17}};
const HoardingExtents kStartExtents{11, 17, 71, 81};   // the start post is not counted, the step past the end is (45,17)

// Land bought east of the park, x 71 to 101 and y 41 to 61: the outer fence once the original walk has cut it.
const std::vector<Point> kBoughtFence{{51, 17}, {71, 17}, {71, 41}, {101, 41}, {101, 61}, {71, 61}, {71, 81}, {11, 81}, {11, 17}, {43, 17}};
const HoardingExtents kBoughtExtents{11, 17, 101, 81};
const Point kTourBase{90, 50};   // a tour built on the bought land

void TestStartFence() {
    Grid grid;
    LayRun(&grid, kStartFence);
    const Grid before = grid;
    HoardingExtents walked{}, original{};
    Check(ReadOnlyWalk(grid, kStart, kEnd, &walked), "start fence: the read-only walk closes");
    Check(Same(walked, kStartExtents), "start fence: extents " + Text(walked) + ", want " + Text(kStartExtents));
    Check(SameCells(grid, before), "start fence: the read-only walk changes no cell");
    Check(OriginalWalk(&grid, kStart, kEnd, 100000, &original), "start fence: the original walk closes");
    Check(Same(walked, original), "start fence: read-only " + Text(walked) + " is the original's " + Text(original));
}

void TestBoughtLand() {
    // As a save holds it: the fence after the purchase's walk has cut its crossroads.
    Grid grid;
    LayRun(&grid, kBoughtFence);
    HoardingExtents walked{};
    Check(ReadOnlyWalk(grid, kStart, kEnd, &walked), "bought land: the read-only walk closes");
    Check(Same(walked, kBoughtExtents), "bought land: extents " + Text(walked) + ", want " + Text(kBoughtExtents));
    Check(InZone(ZoneOf(walked), kTourBase), "bought land: the tour's base is inside the fly zone of the loaded fence");
    Check(!InZone(ZoneOf(kStartExtents), kTourBase),
          "bought land: the tour's base is outside the fly zone of the start fence (what a load kept without the fix)");
    Check(InZone(ZoneOf(walked), {20, 25}) && InZone(ZoneOf(walked), {62, 74}), "bought land: the start park is still in the zone");

    // As the purchase leaves it before its walk: the old fence's east side between the new corners is still there, so the
    // two posts where the new fence meets it are crossroads, which the original turns left at and cuts.
    Grid uncut;
    LayRun(&uncut, kStartFence);
    LayRun(&uncut, {{71, 41}, {101, 41}, {101, 61}, {71, 61}});
    Check(tpw::HoardingIsCrossRoad(uncut.At(71, 41).links) && tpw::HoardingIsCrossRoad(uncut.At(71, 61).links),
          "bought land: the uncut fence has its two crossroads");
    const Grid before = uncut;
    HoardingExtents readOnly{}, original{};
    Check(ReadOnlyWalk(uncut, kStart, kEnd, &readOnly), "uncut fence: the read-only walk closes");
    Check(SameCells(uncut, before), "uncut fence: the read-only walk changes no cell");
    Check(OriginalWalk(&uncut, kStart, kEnd, 100000, &original), "uncut fence: the original walk closes");
    Check(Same(readOnly, original), "uncut fence: read-only " + Text(readOnly) + " is the original's " + Text(original));
    Check(Same(readOnly, kBoughtExtents), "uncut fence: extents " + Text(readOnly) + ", want " + Text(kBoughtExtents));
    HoardingExtents again{};
    Check(ReadOnlyWalk(uncut, kStart, kEnd, &again) && Same(again, kBoughtExtents), "uncut fence: the cut fence walks the same");

    // Land bought west and south of the park too: the walk passes the start's other corners.
    Grid wide;
    LayRun(&wide, {{51, 17}, {71, 17}, {71, 101}, {3, 101}, {3, 17}, {43, 17}});
    HoardingExtents w{};
    Check(ReadOnlyWalk(wide, kStart, kEnd, &w) && Same(w, HoardingExtents{3, 17, 71, 101}), "wide land: extents " + Text(w));
}

void TestHole() {
    Grid grid;
    LayRun(&grid, kBoughtFence);
    grid.At(101, 51) = Cell{};   // a lost post on the bought land's east side
    HoardingExtents walked{0, 0, 0, 0};
    Check(!ReadOnlyWalk(grid, kStart, kEnd, &walked), "hole: the read-only walk does not close");
    Check(Same(walked, HoardingExtents{0, 0, 0, 0}), "hole: the failed walk leaves the extents alone");
    Grid copy = grid;
    HoardingExtents original{};
    Check(!OriginalWalk(&copy, kStart, kEnd, 100000, &original), "hole: the original walk stands still at the hole");

    // The fallback: the extents the saved fly zone was made from.
    const Zone saved = ZoneOf(kBoughtExtents);
    HoardingExtents back{};
    Check(tpw::HoardingExtentsFromFlyZone(saved.b, saved.c, saved.d, saved.a, &back) && Same(back, kBoughtExtents),
          "hole: the saved zone gives back " + Text(back));
    Check(InZone(ZoneOf(back), kTourBase), "hole: the tour's base is inside the saved zone's extents");

    // A walk that runs off the grid's edge does not close either.
    Grid edge;
    LayRun(&edge, {{51, 17}, {127, 17}});
    Check(!ReadOnlyWalk(edge, kStart, kEnd, &walked), "edge: a walk off the grid does not close");
}

void TestFlyZoneInverse() {
    for (int32_t v = -2; v <= 130; ++v) {
        HoardingExtents e{};
        const HoardingExtents want{v, v + 1, v + 2, v + 3};
        const Zone z = ZoneOf(want);
        Check(tpw::HoardingExtentsFromFlyZone(z.b, z.c, z.d, z.a, &e) && Same(e, want), "inverse of " + Text(want) + ": " + Text(e));
    }
    HoardingExtents e{1, 2, 3, 4};
    // FLY_SetFlyZone never made the fly zone the game starts with (0x11170, 0x7530, 0x2e630, 0x222e0), so it gives no extents.
    Check(!tpw::HoardingExtentsFromFlyZone(0x11170, 0x7530, 0x2e630, 0x222e0, &e), "the initial fly zone gives no extents");
    Check(Same(e, HoardingExtents{1, 2, 3, 4}), "a zone with no extents leaves them alone");
}

}  // namespace

int main() {
    TestStartFence();
    TestBoughtLand();
    TestHole();
    TestFlyZoneInverse();
    if (gFailures == 0) std::printf("host_hoarding_extents: all checks passed\n");
    return gFailures == 0 ? 0 : 1;
}
