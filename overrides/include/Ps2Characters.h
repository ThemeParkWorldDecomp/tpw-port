// Ps2Characters.h: the PS2 Guest Models mod (tpw-mods ps2-characters, TPW_MOD_HOOK ps2_characters). With the hook on,
// the park's children (the eight kids and the four theme world costume kids), staff and entertainers are drawn as the
// PlayStation 2 edition's 3D models instead of their sprites: the packs the launcher's converter wrote from the player's
// own PS2 disc (include/Ps2Pack.h, assets/ps2_convert.h), blended between two baked frames on the CPU, lit, and drawn through the
// engine's own 3D path (CDrawPrimitive) in the sprite pass, depth tested with the park. Draw only: nothing here calls the
// game's random numbers or writes the game's state; every choice a person's model makes (which idle, when) comes from a
// hash of its sprite record's address and the steady clock. The sprite stays for anything without a model, far away,
// or not captured this frame; its blob shadow and the pointer's pick are the sprite's, unchanged.
// The runtime is src/game/Ps2Characters.cpp; the pure parts below are tested by tests/host_ps2_characters.cpp.
#pragma once


#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "Ps2Pack.h"

// TPW_PORT_FIX mods ps2_characters | Mac: the park's visitors, staff and entertainers are 2D sprites in eight views | Port: TPW_MOD_HOOK ps2_characters (gModPs2Characters, set by tpw-mods ps2-characters through platform/mods.h): the children, staff and entertainers with a converted PS2 model are drawn as that 3D model in the sprite pass (CSpriteManager::RenderSprites), at the sprite's place, facing where they walk, scaled to the sprite's height, the sprite's animation picking the model's clip, the riders' heads on rides as the model's head, in the park view and in first person; off, the Mac's sprites | Why: tpw-mods ps2-characters, "PS2 Guest Models (3D)", for 0.6.0
inline bool gModPs2Characters = false;
// The converted models' folder (the data folder's assets/ps2): the launcher sets it when the mod is on. Empty: the
// variable TPW_PS2_ROOT, read once (the headless runs' test hook), or no models.
inline std::string gPs2CharactersRoot;

struct SSpriteGameData;
struct tagSCRIPT;
class ESP_CSprite;

namespace tpw::ps2 {

// Which clip table a sprite set's actions are read with: the visitors' (the eight kids and the four costume kids) or the
// staff's (the four staff kinds and the twelve entertainers, whose action numbers mean the same: ClipForStaffAction). A
// Head set is a ride's riders' heads (kidsheads, costumeheads): only the model's head is drawn, turned as the ride's
// head node is (HeadPart, Ps2NoteRideHead).
enum class SetKind : uint8_t { Kid, Staff, Head };
struct SetPack {
    const char* pack = nullptr;   // nullptr: the set stays a sprite
    SetKind kind = SetKind::Kid;
};

namespace detail {
// Whether `a` (up to `end`, or its end) is `b` in any case.
inline bool SameName(const char* a, const char* end, const char* b) {
    for (; a != end && *a != '\0' && *b != '\0'; ++a, ++b)
        if ((*a | 32) != (*b | 32)) return false;
    return *b == '\0' && (a == end || *a == '\0' || *a == '.');
}
}  // namespace detail

// The pack a sprite set's name (ESP_CSprite::mName, "generic\kids:spr_be") is drawn with and its clip table, by the
// set's folder and file (the folder matters: "space\costumeheads:spr_sp" is a ride's heads, "space\entertainers:spr_sp"
// the spaceman). The PC's eight kids are paired with the PS2's eight in order, boys then girls, and the four costume
// kids with the PS2's four theme world kids. The staff: the guard, the handyman, the researcher, and both PC mechanics
// (FA and OM) as the PS2's one mechanic (fatmechanic). The entertainers as the PS2's own where it has one, and the four
// it has not as the nearest PS2 entertainer of their own world: the ladybird (LA) as the flower, the hunchback (HU) as
// Frankenstein's monster (franky), the native (NA) as the hunter, the tentacle monster (MO) as the alien. The heads the
// rides show are their kid's pack, kind Head: the kids' heads ("generic\kidsheads:spr_be") and the costume kids' that
// have one ("jungle\costumeheads:spr_ti") by the same file name, the space costume kid's as "space\costumeheads:spr_sp".
inline SetPack PackForSet(const char* name) {
    if (name == nullptr) return {};
    // The file (after the last separator) and the folder (between the separator before it and it).
    const char* base = name;
    const char* folderEnd = name;
    for (const char* p = name; *p != '\0'; ++p)
        if (*p == '/' || *p == ':' || *p == '\\') folderEnd = p, base = p + 1;
    const char* folder = name;
    for (const char* p = name; p < folderEnd; ++p)
        if (*p == '/' || *p == ':' || *p == '\\') folder = p + 1;
    struct Row {
        const char* folder;
        const char* file;
        const char* pack;
        SetKind kind;
    };
    static const Row kRows[] = {
        {"kids", "SPR_BE", "boy1a", SetKind::Kid},           {"kids", "SPR_BI", "boy2a", SetKind::Kid},
        {"kids", "SPR_FR", "boy3a", SetKind::Kid},           {"kids", "SPR_SA", "boy4a", SetKind::Kid},
        {"kids", "SPR_CH", "girl1a", SetKind::Kid},          {"kids", "SPR_KI", "girl2a", SetKind::Kid},
        {"kids", "SPR_SU", "girl3a", SetKind::Kid},          {"kids", "SPR_TA", "girl4a", SetKind::Kid},
        {"costumes", "SPR_BK", "fantasykid", SetKind::Kid},  {"costumes", "SPR_HK", "hallowkid", SetKind::Kid},
        {"costumes", "SPR_TI", "junglekid", SetKind::Kid},   {"costumes", "SPR_SK", "spacekid", SetKind::Kid},
        {"guards", "SPR_GU", "guard", SetKind::Staff},       {"handymen", "SPR_HA", "handyman", SetKind::Staff},
        {"mechanics", "SPR_FA", "fatmechanic", SetKind::Staff}, {"mechanics", "SPR_OM", "fatmechanic", SetKind::Staff},
        {"researchers", "SPR_RE", "researcher", SetKind::Staff},
        {"entertainers", "SPR_FL", "flower", SetKind::Staff}, {"entertainers", "SPR_GN", "gnome", SetKind::Staff},
        {"entertainers", "SPR_LA", "flower", SetKind::Staff}, {"entertainers", "SPR_DR", "vampire", SetKind::Staff},
        {"entertainers", "SPR_FK", "franky", SetKind::Staff}, {"entertainers", "SPR_HU", "franky", SetKind::Staff},
        {"entertainers", "SPR_DI", "dino", SetKind::Staff},   {"entertainers", "SPR_EX", "hunter", SetKind::Staff},
        {"entertainers", "SPR_NA", "hunter", SetKind::Staff}, {"entertainers", "SPR_SM", "alien", SetKind::Staff},
        {"entertainers", "SPR_SP", "spaceman", SetKind::Staff}, {"entertainers", "SPR_MO", "alien", SetKind::Staff},
        {"costumeheads", "SPR_SP", "spacekid", SetKind::Head},   // the space costume kid's (SPR_SK's) head
    };
    const bool kidsHeads = detail::SameName(folder, folderEnd, "kidsheads");
    const bool costumeHeads = detail::SameName(folder, folderEnd, "costumeheads");
    for (const Row& row : kRows) {
        if (!detail::SameName(base, nullptr, row.file)) continue;
        if (detail::SameName(folder, folderEnd, row.folder)) return {row.pack, row.kind};
        if ((kidsHeads && std::strcmp(row.folder, "kids") == 0) || (costumeHeads && std::strcmp(row.folder, "costumes") == 0))
            return {row.pack, SetKind::Head};
    }
    return {};
}
inline const char* PackForSprite(const char* name) { return PackForSet(name).pack; }

// How a clip's time runs: Frame from the sprite's own frame (so the feet keep the sprite's step), Loop and Once from the
// steady clock at the PS2's 30 frames a second (Once holds its last frame), Idle one of the PS2's six idles (group 2),
// chosen by the person's hash, looped; AfterIntro the group's intro (the group before it, 4 before the work loop 5) once,
// if the pack has one, then the group looped.
enum class ClipTime : uint8_t { Frame, Loop, Once, Idle, AfterIntro };
struct ClipChoice {
    uint8_t group = 2, clip = 0;
    ClipTime time = ClipTime::Loop;
};

// The PS2 clip for a PC kid's action (the action number in its sprite set, 0..15). The PC kids' actions: 0 stand,
// 1 walk, 2 run, 4 throw, 6 be sick, 11 fidget, 12 cheer, 14 cry. The PS2's groups: 0 walk, 1 run, 2 idles,
// 4 be sick, 5 throw, 6 fidget, 8 cheer, 12 cry.
inline ClipChoice ClipForAction(int32_t action) {
    switch (action) {
    case 1: return {0, 0, ClipTime::Frame};
    case 2: return {1, 0, ClipTime::Frame};
    case 0: return {2, 0, ClipTime::Idle};
    case 4: return {5, 0, ClipTime::Once};
    case 6: return {4, 0, ClipTime::Once};
    case 11: return {6, 0, ClipTime::Loop};
    case 12: return {8, 0, ClipTime::Once};
    case 14: return {12, 0, ClipTime::Loop};
    default: return {2, 0, ClipTime::Loop};
    }
}

// The PS2 clip for a PC staff member's or entertainer's action. The PC's actions (the sprite scripts in
// src/game/Script.cpp, started by CStaff::SetState and the kinds' own states): 0 stand, 1 walk (the staff's slow walk is
// the same action), 2 run (the guard's chase), 3 stand still, 4 work (the handyman's sweep, the mechanic's repair, the
// researcher's research, an entertainer's act), 5 strike, 6 held in the pointer (the pick-up and hire script),
// 7 the guard's catch, 8 the guard carrying a child, 9 the guard walking a lost child, 10 and 11 entertainers' extra
// acts. The PS2's staff groups: 0 walk, 1 run, 2 idle, 3 picked up (STAFFPICKEDUP), 4 work intro, 5 work (WORK; the
// guard's carry, an entertainer's act), 6 work outro, 7 strike.
inline ClipChoice ClipForStaffAction(int32_t action) {
    switch (action) {
    case 0: return {2, 0, ClipTime::Idle};
    case 1: return {0, 0, ClipTime::Frame};
    case 2: return {1, 0, ClipTime::Frame};
    case 4: return {5, 0, ClipTime::AfterIntro};
    case 5: return {7, 0, ClipTime::Loop};
    case 6: return {3, 0, ClipTime::Loop};
    case 7: return {4, 0, ClipTime::Once};
    case 8: return {5, 0, ClipTime::Frame};
    case 9: return {0, 0, ClipTime::Frame};
    case 10: return {5, 0, ClipTime::Loop};
    default: return {2, 0, ClipTime::Loop};
    }
}
inline ClipChoice ClipFor(SetKind kind, int32_t action) {
    // A ride's head holds the first frame of the walk (HeadPart's frame).
    if (kind == SetKind::Head) return {0, 0, ClipTime::Once};
    return kind == SetKind::Staff ? ClipForStaffAction(action) : ClipForAction(action);
}

// The render side's own hash (a 64-bit finaliser), for choices that must not touch the game's random numbers.
inline uint32_t RenderHash(uint64_t key) {
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccdull;
    key ^= key >> 33;
    key *= 0xc4ceb9fe1a85ec53ull;
    key ^= key >> 33;
    return static_cast<uint32_t>(key);
}

// The clip of a pack with that group and clip number, else nullptr.
inline const Ps2PackClip* FindClip(const Ps2Pack& pack, uint8_t group, uint8_t clip) {
    for (const Ps2PackClip& c : pack.clips)
        if (c.group == group && c.clip == clip) return &c;
    return nullptr;
}

// Vertex `v` of frame `f` of a clip, in the PS2's units.
inline void FramePosition(const Ps2Pack& pack, const Ps2PackClip& clip, int32_t f, uint32_t v, float out[3]) {
    const size_t at = clip.offset + (static_cast<size_t>(f) * pack.vertices + v) * 3;
    for (int i = 0; i < 3; ++i) out[i] = static_cast<float>(pack.frames[at + i]) * pack.scale;
}

// Where a model stands: the middle of its first frame's footprint (x, y), its lowest point (the feet) and its height,
// from a clip's first frame (the walk's: the bind pose's bounds are not the standing model's).
struct Anchor {
    float cx = 0, cy = 0, feet = 0, height = 1;
};
inline Anchor AnchorOf(const Ps2Pack& pack, const Ps2PackClip& clip) {
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (uint32_t v = 0; v < pack.vertices; ++v) {
        float p[3];
        FramePosition(pack, clip, 0, v, p);
        for (int i = 0; i < 3; ++i) {
            lo[i] = p[i] < lo[i] ? p[i] : lo[i];
            hi[i] = p[i] > hi[i] ? p[i] : hi[i];
        }
    }
    Anchor a;
    a.cx = 0.5f * (lo[0] + hi[0]);
    a.cy = 0.5f * (lo[1] + hi[1]);
    a.feet = lo[2];
    a.height = hi[2] - lo[2] > 1e-6f ? hi[2] - lo[2] : 1.0f;
    return a;
}

// A sprite's picture as a card facing the screen: SetupVertices hangs it from the projected point in clip units at that
// point's w, so its edges at clip y edge1 and edge2 are |edge2 - edge1| / s world units apart, s the view's scale of clip
// y (the length of the matrix's clip y column, yx yy yz). 0 for a view that has none.
inline float CardHeight(float edge1, float edge2, float yx, float yy, float yz) {
    const float s = std::sqrt(yx * yx + yy * yy + yz * yz);
    return s > 1e-12f ? std::fabs(edge2 - edge1) / s : 0.0f;
}

// The smallest a model is drawn, its card's height over the screen's (2 across): a smaller one stays a sprite, as far
// off as it is drawn the same either way. A kid 0.02 (15 rows of a 1536-row screen), a ride's head 0.01. In first person
// (riding a ride, walking with the camcorder) 0.01 for both: there the kids stand out to the horizon and the 0.02 line
// was a ring of sprites just past the models (TPW_PS2_DEBUG, the walk-through: up to 15% of the kids posed in a second
// were between 0.01 and 0.02, none under 0.01; the frame times with the mod on and off stayed the same).
inline float NearestCard(SetKind kind, bool firstPerson) {
    return firstPerson || kind == SetKind::Head ? 0.01f : 0.02f;
}

// A model's head, for a ride's riders' heads. The PS2 kids' heads have textures of their own, named for it ("headb01",
// "head01_n", "junglehead", "spacekidhead"): the head is the triangles with such a texture (any case), and the others
// whose three corners are at or above the middle (the median) of those triangles' corners' heights, such as a helmet or
// hat drawn with the body's texture. A model without one: the part above its neck, the narrowest cross-section of the
// frame's vertices between 55% and 90% of its height (the widest of the x and y extents of the vertices within 3% of
// the height of each candidate, every 1%, the highest of equal ones), the neck the lowest of that cross-section's
// vertices, the head the triangles whose three corners are at or above it. The neck is the head's lowest corner; its
// middle is the middle of its corners' bounds, its height theirs. `positions` holds `count` vertices' x, y, z (z up).
// No head (an empty list) when the frame is flat or nothing is above the neck.
struct HeadPart {
    float neck = 0;
    float cx = 0, cy = 0, cz = 0, height = 0;
    std::vector<uint32_t> triangles;   // into the pack's triangles
};
namespace detail {
inline bool IsHeadTexture(const std::string& name) {
    for (size_t i = 0; i + 4 <= name.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(name[i])) == 'h' &&
            std::tolower(static_cast<unsigned char>(name[i + 1])) == 'e' &&
            std::tolower(static_cast<unsigned char>(name[i + 2])) == 'a' &&
            std::tolower(static_cast<unsigned char>(name[i + 3])) == 'd')
            return true;
    return false;
}
}  // namespace detail
inline HeadPart HeadPartOf(const float* positions, uint32_t count, const Ps2PackTriangle* triangles, size_t triangleCount,
                           const std::vector<std::string>& textures) {
    HeadPart head;
    if (count == 0) return head;
    float lo = 1e30f, hi = -1e30f;
    for (uint32_t v = 0; v < count; ++v) {
        lo = positions[v * 3 + 2] < lo ? positions[v * 3 + 2] : lo;
        hi = positions[v * 3 + 2] > hi ? positions[v * 3 + 2] : hi;
    }
    const float height = hi - lo;
    if (height <= 1e-6f) return head;
    auto valid = [&](const Ps2PackTriangle& t) { return t.vertex[0] < count && t.vertex[1] < count && t.vertex[2] < count; };
    auto textured = [&](const Ps2PackTriangle& t) {
        return valid(t) && t.texture < textures.size() && detail::IsHeadTexture(textures[t.texture]);
    };
    std::vector<float> heights;
    for (size_t t = 0; t < triangleCount; ++t)
        if (textured(triangles[t]))
            for (uint16_t v : triangles[t].vertex) heights.push_back(positions[v * 3 + 2]);
    float above = 0;   // the others' corners at or above this are the head's too
    if (!heights.empty()) {
        std::nth_element(heights.begin(), heights.begin() + heights.size() / 2, heights.end());
        above = heights[heights.size() / 2];
    } else {
        float best = 1e30f;
        above = lo + 0.75f * height;
        for (int step = 55; step <= 90; ++step) {
            const float at = lo + height * static_cast<float>(step) / 100.0f;
            float mn[2] = {1e30f, 1e30f}, mx[2] = {-1e30f, -1e30f}, bottom = 1e30f;
            bool any = false;
            for (uint32_t v = 0; v < count; ++v) {
                const float* p = &positions[v * 3];
                if (std::fabs(p[2] - at) > 0.03f * height) continue;
                any = true;
                bottom = p[2] < bottom ? p[2] : bottom;
                for (int i = 0; i < 2; ++i) {
                    mn[i] = p[i] < mn[i] ? p[i] : mn[i];
                    mx[i] = p[i] > mx[i] ? p[i] : mx[i];
                }
            }
            if (!any) continue;
            const float width = (mx[0] - mn[0]) > (mx[1] - mn[1]) ? mx[0] - mn[0] : mx[1] - mn[1];
            if (width <= best) best = width, above = bottom;
        }
    }
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    for (size_t t = 0; t < triangleCount; ++t) {
        bool in = valid(triangles[t]);
        for (uint16_t v : triangles[t].vertex) in = in && positions[v * 3 + 2] >= above;
        if (!in && !textured(triangles[t])) continue;
        head.triangles.push_back(static_cast<uint32_t>(t));
        for (uint16_t v : triangles[t].vertex)
            for (int i = 0; i < 3; ++i) {
                mn[i] = positions[v * 3 + i] < mn[i] ? positions[v * 3 + i] : mn[i];
                mx[i] = positions[v * 3 + i] > mx[i] ? positions[v * 3 + i] : mx[i];
            }
    }
    if (head.triangles.empty()) return head;
    head.neck = mn[2];
    head.cx = 0.5f * (mn[0] + mx[0]);
    head.cy = 0.5f * (mn[1] + mx[1]);
    head.cz = 0.5f * (mn[2] + mx[2]);
    head.height = mx[2] - mn[2];
    return head;
}

// The strike sign. Every PS2 staff member and entertainer holds a placard ("$$") in its right hand in every clip, walk
// and idle too: the sign is a mesh node of its own, rigid on the hand's bone, a board textured "plackard" on a stick
// textured "broom", and the PS2's game code hides it except on strike (its animations never do). The sign shows only in
// the strike group (7, the PC's action 5, ClipForStaffAction).
constexpr uint8_t kStrikeGroup = 7;
inline bool ShowsSign(uint8_t group) { return group == kStrikeGroup; }
// The sign's triangles in a pack, one flag per triangle: those with the board's texture, and the stick's, the
// triangles textured "broom" joined by their vertices whose lowest vertex is the one after the board's highest (the
// PS2 numbers a node's vertices together, the board's first: the pack keeps its order). A broom of its own (the
// handyman's) is another node and stays. All zero for a model with no board.
inline std::vector<uint8_t> SignTriangles(const Ps2PackTriangle* triangles, size_t triangleCount,
                                          const std::vector<std::string>& textures) {
    std::vector<uint8_t> sign(triangleCount, 0);
    auto named = [&](const Ps2PackTriangle& t, const char* name) {
        return t.texture < textures.size() && textures[t.texture] == name;
    };
    int64_t top = -1;
    for (size_t t = 0; t < triangleCount; ++t)
        if (named(triangles[t], "plackard")) {
            sign[t] = 1;
            for (uint16_t v : triangles[t].vertex) top = v > top ? v : top;
        }
    if (top < 0) return sign;
    // The broom-textured parts, each the triangles sharing vertices, and each part's lowest vertex.
    std::vector<size_t> part(triangleCount);
    for (size_t t = 0; t < triangleCount; ++t) part[t] = t;
    auto root = [&](size_t t) {
        while (part[t] != t) t = part[t] = part[part[t]];
        return t;
    };
    for (size_t a = 0; a < triangleCount; ++a) {
        if (!named(triangles[a], "broom")) continue;
        for (size_t b = a + 1; b < triangleCount; ++b) {
            if (!named(triangles[b], "broom")) continue;
            bool shared = false;
            for (uint16_t va : triangles[a].vertex)
                for (uint16_t vb : triangles[b].vertex) shared = shared || va == vb;
            if (shared) part[root(b)] = root(a);
        }
    }
    std::vector<int64_t> lowest(triangleCount, INT64_MAX);
    for (size_t t = 0; t < triangleCount; ++t)
        if (named(triangles[t], "broom"))
            for (uint16_t v : triangles[t].vertex) lowest[root(t)] = v < lowest[root(t)] ? v : lowest[root(t)];
    for (size_t t = 0; t < triangleCount; ++t)
        if (named(triangles[t], "broom") && lowest[root(t)] == top + 1) sign[t] = 1;
    return sign;
}

// A 24- or 32-bit uncompressed TGA (type 2) as the engine's source pixels: B, G, R(, A) per texel in the file's order,
// rows from the top. False when it is not one.
inline bool ReadTga(const uint8_t* data, size_t size, int32_t& width, int32_t& height, int32_t& bytes, std::string& pixels) {
    if (size < 18 || data[1] != 0 || data[2] != 2) return false;
    width = data[12] | data[13] << 8;
    height = data[14] | data[15] << 8;
    bytes = data[16] / 8;
    if (width <= 0 || height <= 0 || (bytes != 3 && bytes != 4)) return false;
    const size_t start = 18 + data[0];
    const size_t row = static_cast<size_t>(width) * bytes;
    if (start + row * height > size) return false;
    const bool top = (data[17] & 0x20) != 0;
    pixels.resize(row * height);
    for (int32_t y = 0; y < height; ++y) {
        const int32_t from = top ? y : height - 1 - y;
        std::memcpy(&pixels[row * y], data + start + row * from, row);
    }
    return true;
}

// The texels' colour times gain (alpha untouched), each channel held at 255: B, G, R(, A) source pixels as ReadTga's.
inline void Brighten(std::string& pixels, int32_t bytes, float gain) {
    for (size_t at = 0; at + bytes <= pixels.size(); at += static_cast<size_t>(bytes))
        for (int32_t c = 0; c < 3; ++c) {
            const float v = static_cast<float>(static_cast<uint8_t>(pixels[at + c])) * gain + 0.5f;
            pixels[at + c] = static_cast<char>(static_cast<uint8_t>(v >= 255.0f ? 255.0f : v));
        }
}

}  // namespace tpw::ps2

// The runtime (src/game/Ps2Characters.cpp), reached through this table so that the test libraries that build the hooked
// files without it link: until Ps2InstallRuntime (the launcher calls it at its start) each entry does nothing.
struct Ps2Runtime {
    // ESP_AddHLSpriteFromScript, once the script's sprite is listed for the frame: what the model needs from it.
    void (*capture)(SSpriteGameData* listed, const tagSCRIPT* script, ESP_CSprite* sprite, int32_t actionNumber,
                    int32_t frameBase, int32_t viewOffset) =
        [](SSpriteGameData*, const tagSCRIPT*, ESP_CSprite*, int32_t, int32_t, int32_t) {};
    // CSpriteManager::Prepare, once the frame's sprites are listed: the sprites drawn as models this frame are chosen and
    // their textures prepared in the texture cache.
    void (*prepare)(SSpriteGameData** sprites, int32_t count) = [](SSpriteGameData**, int32_t) {};
    // CSpriteManager::RenderSprites: whether this listed sprite is drawn as a model (chosen by prepare).
    bool (*isModel)(const SSpriteGameData* sprite) = [](const SSpriteGameData*) { return false; };
    // CSpriteManager::RenderSprites, for a sprite drawn as a model: its quad worked out as DrawRegular and AddRegular do
    // (the pointer's pick, SSpriteGameData::SetupVertices) but not drawn. CalcRegular's answer.
    int32_t (*pickOnly)(SSpriteGameData* sprite) = [](SSpriteGameData*) { return 1; };
    // CSpriteManager::RenderSprites, after the sprites: the models. 0 when the renderer fails.
    int32_t (*draw)(SSpriteGameData** sprites, int32_t count) = [](SSpriteGameData**, int32_t) { return 1; };
    // TickRideHeads, for each ride's head it turns: the head's script handle and its 4 x 4 matrix (the head node's, in
    // the world), so that the head's model turns with the ride.
    void (*noteRideHead)(int32_t handle, const float* matrix) = [](int32_t, const float*) {};
};
inline Ps2Runtime gPs2Runtime;
void Ps2InstallRuntime();

inline void Ps2CaptureSprite(SSpriteGameData* listed, const tagSCRIPT* script, ESP_CSprite* sprite, int32_t actionNumber,
                             int32_t frameBase, int32_t viewOffset) {
    gPs2Runtime.capture(listed, script, sprite, actionNumber, frameBase, viewOffset);
}
inline void Ps2PrepareModels(SSpriteGameData** sprites, int32_t count) { gPs2Runtime.prepare(sprites, count); }
inline bool Ps2IsModel(const SSpriteGameData* sprite) { return gPs2Runtime.isModel(sprite); }
inline int32_t Ps2PickOnly(SSpriteGameData* sprite) { return gPs2Runtime.pickOnly(sprite); }
inline int32_t Ps2DrawModels(SSpriteGameData** sprites, int32_t count) { return gPs2Runtime.draw(sprites, count); }
inline void Ps2NoteRideHead(int32_t handle, const float* matrix) { gPs2Runtime.noteRideHead(handle, matrix); }

