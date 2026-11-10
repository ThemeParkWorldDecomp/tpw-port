// Ps1SaveRepair.h: the ps1-rides mod's repair of parks saved with its 0.4.0 rides (TPW_MOD_HOOK ps1_save_repair). The
// mod's converter of 0.4.1 turns a PlayStation ride's footprint half a turn where 0.4.0 mirrored it, and gives the
// entrance and the exit their right roles, so a ride re-imported with 0.4.1 has its doors on other cells than a park
// saved before keeps for it: the park keeps the entry and exit cells (CObject's mF32 and mF34) and the map's door,
// queue and path cells as they were placed. On loading such a park (CSaveLoad::PostLoadOperations) each ride of the
// mod whose saved doors are not the ones its record now gives has them moved: the old door cells become the ride's
// body, the new ones doors, the old queue a path, the new exit joined to a path and the new entrance given a queue
// joined to one. Where no path can be reached that way, the door is left unjoined: the game's own state of a ride
// that needs its queue or its exit path built. A ride whose doors match is left alone, so a second load changes
// nothing. Only with the port's fixes: off (the default) or in a build without them, a park loads as it was saved.
//
// The planning works on a grid behind the small interface of RepairPs1RideDoors (the game's map cells in
// src/game/save/SaveLoad.cpp, a stand-in in tests/host_ps1_save_repair.cpp).
#pragma once


#include <cstdint>
#include <string>
#include <vector>

// TPW_PORT_FIX mods gModPs1SaveRepair | Mac: no such flag | Port: TPW_MOD_HOOK ps1_save_repair, set by the launcher while a tpw-mods mod that switches it on is on (platform/mods.h); set, CSaveLoad::PostLoadOperations moves the doors of that mod's rides whose saved entry and exit cells are not their record's (RepairPs1Rides) | Why: tpw-mods ps1-rides 0.4.1 moved its rides' doors, and parks saved with 0.4.0 keep the old ones, which leaves the queue and the exit path on the wrong cells
inline bool gModPs1SaveRepair = false;

// The rides the repair may touch, by RideBadgeKey of their folder (include/RideBadges.h): the content rides of the mod
// that switched the hook on, filled by the launcher with the hook. Empty, no ride is touched.
inline std::vector<std::string> gPs1SaveRepairRides;

namespace ps1repair {

// The map cell types the repair reads and writes (CMapCell::mType).
enum CellType : int32_t {
    kGrass = 0,
    kPath = 1,
    kQueue = 3,
    kRide = 4,
    kEntrance = 9,
    kExit = 10,
};

// The four straight directions (the cells' direction and neighbour bits), in the order the repair tries them.
constexpr uint8_t kStraight[4] = {1, 4, 0x10, 0x40};   // -y, +x, +y, -x
constexpr uint8_t kAround[8] = {1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80};   // the straight ones and the diagonals between them

// OPPOSITE and ADD_DIRECTION of src/game/park/Map.cpp, for the planning (no game code in the tests).
inline uint8_t Opposite(uint8_t d) {
    if (d == 0) return 0;
    if (d < 0x10) return static_cast<uint8_t>((d << 4) & 0xf0);
    return static_cast<uint8_t>(d >> 4);
}
inline uint8_t AddDirection(uint8_t a, uint8_t b) {
    int32_t n = 0;
    for (int32_t first = a; static_cast<uint8_t>(first) > 1; first >>= 1) ++n;
    const int32_t v = static_cast<int32_t>(static_cast<uint32_t>(b) << n);
    return static_cast<uint8_t>(v > 0x80 ? (v >> 8) & 0xff : v & 0xff);
}
inline bool IsStraight(uint8_t d) { return d == 1 || d == 4 || d == 0x10 || d == 0x40; }

// A ride's door: its cell (the 1 based id x + (y << 7) + 1) and the side it opens on (a straight direction).
struct Door {
    uint16_t cell = 0;
    uint8_t out = 0;
};
struct Doors {
    Door entrance;
    Door exit;
};

// The doors a ride whose offset cell is `offsetCell` (CObject::mF30) and whose rotation is `rotation` has from its
// record's entry and exit (x, y and the shape's direction byte): the cells as CObject's constructor works them out
// (MapDelta::Rotate), the sides as LLCR_PlaceRideBlock sets the doors' directions (the entrance's turned outward).
// False for a rotation other than 0, 90, 180 and 270 or a direction byte that is not a straight one.
inline bool DoorsFor(uint16_t offsetCell, int32_t rotation, int32_t entryX, int32_t entryY, uint8_t entryFlag, int32_t exitX,
                     int32_t exitY, uint8_t exitFlag, Doors* doors) {
    const int32_t r = rotation % 360;
    uint8_t base;
    if (r == 0) base = 1;
    else if (r == 90 || r == -270) base = 0x40;
    else if (r == 180 || r == -180) base = 0x10;
    else if (r == 270 || r == -90) base = 4;
    else return false;
    if (!IsStraight(entryFlag) || !IsStraight(exitFlag)) return false;
    auto cell = [&](int32_t x, int32_t y) {
        int32_t rx = x, ry = y;
        if (base == 0x10) { rx = -x; ry = -y; }
        else if (base == 0x40) { rx = y; ry = -x; }
        else if (base == 4) { rx = -y; ry = x; }
        return static_cast<uint16_t>(static_cast<uint32_t>(rx) + (static_cast<uint32_t>(ry) << 7) + offsetCell);
    };
    doors->entrance.cell = cell(entryX, entryY);
    doors->entrance.out = Opposite(AddDirection(base, entryFlag));
    doors->exit.cell = cell(exitX, exitY);
    doors->exit.out = AddDirection(base, exitFlag);
    return true;
}

// What RepairPs1RideDoors did.
enum class Outcome : uint8_t {
    kMatches,    // the saved doors are the record's: nothing changed
    kSkipped,    // the cells are not a ride of this shape as the repair knows it: nothing changed
    kRepaired,   // the doors moved
};
struct Result {
    Outcome outcome = Outcome::kMatches;
    bool exitJoined = false;    // the new exit opens on a path
    bool queueJoined = false;   // the new entrance has a queue joined to a path
    int32_t oldQueueCells = 0;  // the cells of the old queue, now paths
    int32_t laidCells = 0;      // the cells laid to reach a path (paths for the exit, queue cells for the entrance)
};

// How far (in cells) the repair lays a path or a queue to reach a path: a door moves along the side it is on by at
// most the ride's width, so a few cells reach the old queue's or exit path's cells.
constexpr int32_t kMaxReach = 6;

// The grid the repair works on. Cells are 1 based ids; 0 is none.
//   uint16_t Step(uint16_t cell, uint8_t dir) const       the neighbour in a direction bit (straight or diagonal), 0 off the map
//   int32_t Type(uint16_t) const; uint8_t Links(uint16_t) const; uint8_t Direction(uint16_t) const; uint16_t Parent(uint16_t) const
//   bool Free(uint16_t) const                             grass a path or a queue may be laid on
//   bool Plain(uint16_t) const                            no track over or under the cell (a path there may become a queue)
//   void SetDoor(uint16_t, int32_t type, uint8_t out)     a door of the ride: the type, its side, no links
//   void SetBody(uint16_t)                                the ride's body: type 4, no links, no direction
//   void Unlink(uint16_t, uint8_t mask) / Link(uint16_t, uint8_t mask)   clears / sets neighbour bits
//   void SetQueue(uint16_t, uint8_t dir, uint16_t parent, bool front)   a queue cell pointing `dir` (towards the door), no links
//   void MakePath(uint16_t)                               a path, joined to its path neighbours and an exit facing it (the
//                                                          game's CalculateNeighbours of mode 1)
//   void SetEdgeFlag(uint16_t, bool on)                    the 0x20 flag the game gives the cells beside a ride's doors

namespace detail {

template <class Grid>
bool IsRidePart(const Grid& g, uint16_t cell, uint16_t parent) {
    const int32_t t = g.Type(cell);
    return (t == kRide || t == kEntrance || t == kExit) && g.Parent(cell) == parent;
}

// The queue cell behind `cell`, as CObject::GetQCellBehind finds it: a straight neighbour that is a queue (not an
// entrance) pointing at it, tried -y, +y, +x, -x.
template <class Grid>
uint16_t QueueCellBehind(const Grid& g, uint16_t cell) {
    const uint8_t sides[4] = {1, 0x10, 4, 0x40};
    for (uint8_t side : sides) {
        const uint16_t n = g.Step(cell, side);
        if (n != 0 && g.Type(n) == kQueue && g.Direction(n) == Opposite(side)) return n;
    }
    return 0;
}

// The straight neighbours of `cell` that are paths it is linked with.
template <class Grid>
std::vector<uint16_t> LinkedPaths(const Grid& g, uint16_t cell) {
    std::vector<uint16_t> out;
    for (uint8_t d : kStraight) {
        const uint16_t n = g.Step(cell, d);
        if (n != 0 && g.Type(n) == kPath && (g.Links(cell) & d) != 0) out.push_back(n);
    }
    return out;
}

// Whether the path cells `ends` stay joined to each other along linked paths without `cut`.
template <class Grid>
bool JoinedWithout(const Grid& g, const std::vector<uint16_t>& ends, uint16_t cut) {
    if (ends.size() < 2) return true;
    std::vector<uint8_t> seen(0x4000 + 2, 0);
    std::vector<uint16_t> todo = {ends[0]};
    seen[ends[0]] = 1;
    seen[cut] = 1;
    while (!todo.empty()) {
        const uint16_t c = todo.back();
        todo.pop_back();
        for (uint8_t d : kStraight) {
            if ((g.Links(c) & d) == 0) continue;
            const uint16_t n = g.Step(c, d);
            if (n == 0 || n >= seen.size() || seen[n] != 0 || g.Type(n) != kPath) continue;
            seen[n] = 1;
            todo.push_back(n);
        }
    }
    for (uint16_t e : ends)
        if (seen[e] == 0) return false;
    return true;
}

// The shortest way from `start` over free grass to a cell beside a path that is not the ride's: the cells from
// `start`, at most kMaxReach of them, and in `path` the path cell the last one stands beside (with `side`, the last
// cell's side it is on). `start` itself must be free. Empty when there is none.
template <class Grid>
std::vector<uint16_t> WayToPath(const Grid& g, uint16_t start, uint16_t avoid, uint16_t* path, uint8_t* side) {
    struct Step { uint16_t cell; int32_t from; int32_t depth; };
    std::vector<Step> steps = {{start, -1, 1}};
    std::vector<uint16_t> seen = {start, avoid};
    for (size_t i = 0; i < steps.size(); ++i) {
        const uint16_t c = steps[i].cell;
        for (uint8_t d : kStraight) {
            const uint16_t n = g.Step(c, d);
            if (n == 0 || n == avoid || g.Type(n) != kPath) continue;
            std::vector<uint16_t> way;
            for (int32_t j = static_cast<int32_t>(i); j >= 0; j = steps[static_cast<size_t>(j)].from)
                way.insert(way.begin(), steps[static_cast<size_t>(j)].cell);
            *path = n;
            *side = d;
            return way;
        }
        if (steps[i].depth >= kMaxReach) continue;
        for (uint8_t d : kStraight) {
            const uint16_t n = g.Step(c, d);
            if (n == 0) continue;
            bool known = false;
            for (uint16_t s : seen) known = known || s == n;
            if (known || !g.Free(n)) continue;
            seen.push_back(n);
            steps.push_back({n, static_cast<int32_t>(i), steps[i].depth + 1});
        }
    }
    return {};
}

// The direction from `a` to its straight neighbour `b`, 0 when they are not neighbours.
template <class Grid>
uint8_t Towards(const Grid& g, uint16_t a, uint16_t b) {
    for (uint8_t d : kStraight)
        if (g.Step(a, d) == b) return d;
    return 0;
}

}  // namespace detail

// Moves the doors of the ride whose cells have the parent `parent` from the saved ones (`savedEntry`, `savedExit`:
// CObject's mF32 and mF34) to `want` (DoorsFor of its record), as the comment at the top says.
template <class Grid>
Result RepairPs1RideDoors(Grid& g, uint16_t parent, uint16_t savedEntry, uint16_t savedExit, const Doors& want) {
    using namespace detail;
    Result result;
    if (savedEntry == want.entrance.cell && savedExit == want.exit.cell) return result;
    result.outcome = Outcome::kSkipped;

    // Only a ride as the 0.4.0 converter placed it: an entrance and an exit cell of the ride, the new door cells in
    // its footprint, each new door opening onto a cell outside it.
    const uint16_t e1 = want.entrance.cell, x1 = want.exit.cell;
    if (savedEntry == 0 || savedExit == 0 || savedEntry == savedExit || e1 == x1 || e1 == 0 || x1 == 0) return result;
    if (g.Type(savedEntry) != kEntrance || g.Parent(savedEntry) != parent) return result;
    if (g.Type(savedExit) != kExit || g.Parent(savedExit) != parent) return result;
    if (!IsRidePart(g, e1, parent) || !IsRidePart(g, x1, parent)) return result;
    const uint16_t q1 = g.Step(e1, want.entrance.out);
    const uint16_t o1 = g.Step(x1, want.exit.out);
    if (q1 == 0 || o1 == 0 || IsRidePart(g, q1, parent) || IsRidePart(g, o1, parent)) return result;
    result.outcome = Outcome::kRepaired;

    // The old queue, front first, as the game walks it (GetFrontOfPhysicalQueue, GetQCellBehind).
    std::vector<uint16_t> oldQueue;
    {
        const uint8_t order[4] = {1, 0x10, 0x40, 4};
        uint16_t front = 0;
        for (uint8_t d : order) {
            if ((g.Links(savedEntry) & d) != 0) {
                front = g.Step(savedEntry, d);
                break;
            }
        }
        for (uint16_t c = front; c != 0 && g.Type(c) == kQueue && oldQueue.size() < 1000; c = QueueCellBehind(g, c)) {
            bool again = false;
            for (uint16_t q : oldQueue) again = again || q == c;
            if (again) break;
            oldQueue.push_back(c);
        }
    }

    // The path the old exit opened on, which may now be beside no door.
    const uint16_t o0 = IsStraight(g.Direction(savedExit)) ? g.Step(savedExit, g.Direction(savedExit)) : 0;

    // The doors: every link into the four door cells goes, the old doors become the body, the new ones doors.
    const uint16_t doorCells[4] = {savedEntry, savedExit, e1, x1};
    for (uint16_t door : doorCells) {
        for (uint8_t d : kStraight) {
            const uint16_t n = g.Step(door, d);
            if (n != 0 && !IsRidePart(g, n, parent)) g.Unlink(n, Opposite(d));
        }
    }
    if (savedEntry != e1 && savedEntry != x1) g.SetBody(savedEntry);
    if (savedExit != e1 && savedExit != x1) g.SetBody(savedExit);
    g.SetDoor(e1, kEntrance, want.entrance.out);
    g.SetDoor(x1, kExit, want.exit.out);

    // The old queue becomes a path: its cells lose their queue links and are joined as paths.
    for (uint16_t q : oldQueue) {
        g.Unlink(q, 0xff);
        g.SetEdgeFlag(q, false);
    }
    for (uint16_t q : oldQueue) g.MakePath(q);
    result.oldQueueCells = static_cast<int32_t>(oldQueue.size());

    // The exit: onto the path in front of it, or a path laid over grass to the nearest one.
    if (g.Type(o1) == kPath) {
        g.MakePath(o1);
        result.exitJoined = true;
    } else if (g.Free(o1)) {
        uint16_t path = 0;
        uint8_t side = 0;
        const std::vector<uint16_t> way = WayToPath(g, o1, q1, &path, &side);
        for (auto it = way.rbegin(); it != way.rend(); ++it) g.MakePath(*it);
        result.exitJoined = !way.empty();
        result.laidCells += static_cast<int32_t>(way.size());
    }
    if (result.exitJoined) g.SetEdgeFlag(o1, true);

    // The entrance: a queue in front of it whose back joins a path. The cell in front is a path that can become the
    // queue (no exit opens on it, and the paths it joins stay joined without it), or grass with a way to a path.
    const uint8_t in = Opposite(want.entrance.out);   // a queue cell points towards the door
    std::vector<uint16_t> queue;
    uint16_t joinPath = 0;
    uint8_t joinSide = 0;
    if (g.Type(q1) == kPath) {
        const std::vector<uint16_t> paths = LinkedPaths(g, q1);
        bool exitOnIt = false;
        for (uint8_t d : kStraight) {
            const uint16_t n = g.Step(q1, d);
            if (n != 0 && g.Type(n) == kExit && (g.Links(q1) & d) != 0) exitOnIt = true;
        }
        if (!paths.empty() && !exitOnIt && q1 != o1 && g.Plain(q1) && JoinedWithout(g, paths, q1)) {
            queue = {q1};
            joinPath = paths[0];
            joinSide = Towards(g, q1, joinPath);
        }
    } else if (g.Free(q1)) {
        queue = WayToPath(g, q1, o1, &joinPath, &joinSide);
        result.laidCells += static_cast<int32_t>(queue.size());
    }
    if (!queue.empty()) {
        for (uint8_t d : kAround) {   // the paths around no longer link to the cells that become the queue
            for (uint16_t q : queue) {
                const uint16_t n = g.Step(q, d);
                if (n != 0 && g.Type(n) == kPath) g.Unlink(n, Opposite(d));
            }
        }
        for (size_t i = 0; i < queue.size(); ++i) {
            const uint16_t q = queue[i];
            const uint8_t dir = i == 0 ? in : Towards(g, q, queue[i - 1]);
            g.Unlink(q, 0xff);
            g.SetQueue(q, dir, parent, i == 0);
            g.Link(q, dir);
            if (i + 1 < queue.size()) g.Link(q, Towards(g, q, queue[i + 1]));
        }
        g.Link(queue.back(), joinSide);
        g.Link(joinPath, Opposite(joinSide));
        g.Link(e1, want.entrance.out);
        result.queueJoined = true;
    }

    // The old exit's path keeps the flag of a cell beside a door only while a door still opens on it.
    if (o0 != 0 && g.Type(o0) == kPath) {
        bool door = false;
        for (uint8_t d : kStraight) {
            const uint16_t n = g.Step(o0, d);
            if (n != 0 && (g.Type(n) == kEntrance || g.Type(n) == kExit) && g.Direction(n) == Opposite(d)) door = true;
        }
        if (!door) g.SetEdgeFlag(o0, false);
    }
    return result;
}

}  // namespace ps1repair

