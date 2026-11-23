// Ps2Characters.cpp: the PS2 Guest Models mod's runtime (include/Ps2Characters.h, TPW_MOD_HOOK ps2_characters). Port
// code, no original behind it: the hooks in ESP_AddHLSpriteFromScript (src/game/HLSprite.cpp) and CSpriteManager
// (src/engine/SpriteManager.cpp) call it only with gModPs2Characters set.
//
// A frame: the script sprites are listed (Ps2CaptureSprite keeps, per listed record, the kid's sprite set, action, frame
// and place, and follows its walk for its heading); the sprite manager prepares the listed sprites (Ps2PrepareModels
// picks those drawn as models, near enough and captured with the record they show, and prepares their textures in the
// texture cache); the sprites are drawn with the models' left out, and the models after them (Ps2DrawModels): two of a
// clip's baked frames blended, lit by a fixed light, turned and scaled into the world and through the view's matrix, then
// added to one triangle list per texture page as the sprites are (clipped by AddPoly_TLCTEX, fogged, projected).
// A ride's riders' heads (the kidsheads and costumeheads sets): TickRideHeads gives each head's node matrix
// (Ps2NoteRideHead), and the head's model is the kid model's head (HeadPartOf) in the walk's first frame, turned by that
// matrix, as large as the head's picture and at its middle; a head with no matrix this frame stays a sprite.
// The staff's and entertainers' strike sign (SignTriangles) is left out of every clip but the strike's (ShowsSign).
// Debugging: TPW_PS2_DEBUG=1 prints what it loads and, once a second, the kids it draws; TPW_PS2_MIRROR=1 mirrors the
// models (the handedness check); TPW_PS2_BESIDE=1 keeps the sprites and draws each model beside its sprite (the facing
// and timing check); TPW_PS2_ACTION=N draws the staff and entertainers with action N's clip (the strike's, 5).
// TPW_PORT_FIX mods gPs2Runtime | Mac: (none: the sprites are drawn as they are) | Port: TPW_MOD_HOOK ps2_characters (gModPs2Characters): the mod's runtime, which captures the listed sprites, picks those drawn as models (near enough; nearer in the park view than in first person) and draws the models and the riders' heads after the sprites; off, nothing here runs | Why: tpw-mods ps2-characters, "PS2 Guest Models (3D)", for 0.6.0

#include "Ps2Characters.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

#include "analyser.h"
#include "platform/samsutils.h"
#include "ESP_sprites.h"
#include "MapWho.h"
#include "TPCamera.h"
#include "sprite_engine.h"
#include "tb_drawprimitive.h"
#include "tb_hfield.h"
#include "tb_loaders.h"
#include "tb_matrix.h"
#include "tb_resource_display.h"
#include "tb_resource_texture.h"
#include "tb_starfield.h"
#include "tb_texturecache.h"
#include "tb_textureentry.h"

namespace {

using namespace tpw::ps2;

constexpr float kReachPerSize = 20.0f;      // SetupVertices: a sprite's quad is 20 times mF2C clip units high
constexpr uint32_t kDisplayFog = 0x2000;    // gei_Display.mFlags
constexpr uint32_t kDisplayVertexFog = 0x4000;
constexpr uint32_t kFogCleared = 0x100000;  // AddSprite's blending bit with the display's fog on
constexpr int32_t kSpritePool = 2;          // AddSprite's render memory pool, primitive type and vertex format
constexpr int32_t kTriangleList = 4;
constexpr uint32_t kTLCTEXFormat = 0x1c4;
constexpr int32_t kStride = 0x20;
constexpr float kHeightPerCard = 1.2f;      // a kid's height over its picture's (the lean of the pictures at the
                                            // middle of the default view: 1.0 to 1.3, TPW_PS2_DEBUG)
constexpr float kHeightPerHeadCard = 0.92f; // a head's height over its picture's (the PC heads fill 23 of their pictures' 25 rows)
// The textures' colour times this: the PS2 drew them brighter than they are stored (its vertex colour 0x80 is 1, up to
// 2), and the PC's sprites are rendered bright. Measured on one fixed-clock park, mod on and off, the mean colour of
// the people's pixels: 0.80 to 0.90 of the sprites' at 1, 1.06 to 1.18 at 1.5.
constexpr float kTexelGain = 1.35f;
constexpr float kClipRate = 30.0f;          // the PS2's clips run at 30 frames a second
constexpr float kTurnRate = 10.0f;          // radians a second a model turns at most
constexpr float kPi = 3.14159265358979f;

bool Debug() {
    static const bool on = [] {
        const char* v = std::getenv("TPW_PS2_DEBUG");
        return v != nullptr && v[0] == '1';
    }();
    return on;
}
bool Mirror() {
    static const bool on = [] {
        const char* v = std::getenv("TPW_PS2_MIRROR");
        return v != nullptr && v[0] == '1';
    }();
    return on;
}
bool Beside() {
    static const bool on = [] {
        const char* v = std::getenv("TPW_PS2_BESIDE");
        return v != nullptr && v[0] == '1';
    }();
    return on;
}

// TPW_PS2_ACTION=N: the staff and entertainers are drawn with action N's clip, whatever they do (-1, the default: their
// own). Draw only, for checking a clip (the strike's, 5, with its sign) without waiting for the game to play it.
int32_t DrawnStaffAction() {
    static const int32_t action = [] {
        const char* v = std::getenv("TPW_PS2_ACTION");
        return v != nullptr && v[0] >= '0' && v[0] <= '9' ? std::atoi(v) : -1;
    }();
    return action;
}

// Seconds on the port's clock (a test run's fixed clock with TPW_FIXED_CLOCK, so its frames repeat).
double Now() { return static_cast<double>(SamsUtilities::UTimer::GetAbsolute()) / 1e6; }

std::filesystem::path Root() {
    if (!gPs2CharactersRoot.empty()) return std::filesystem::u8path(gPs2CharactersRoot);
    static const std::string hook = [] {
        const char* v = std::getenv("TPW_PS2_ROOT");
        return std::string(v != nullptr ? v : "");
    }();
    return std::filesystem::u8path(hook);
}

bool ReadFile(const std::filesystem::path& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

// One converted character: its pack, where it stands and its textures in the engine's format.
struct Model {
    std::string name;                             // the pack's
    tpw::Ps2Pack pack;
    Anchor anchor;
    const tpw::Ps2PackClip* walk = nullptr;       // the clip the anchor is of
    HeadPart head;                                // its head in the walk's first frame, for the rides' heads
    std::vector<uint8_t> sign;                    // per triangle: the strike sign's (SignTriangles), drawn only on strike
    float normalSign = 1;                         // -1 when the pack's triangles wind the other way
    std::vector<CTextureEntry*> textures;         // one per pack texture, nullptr when it did not load
    std::vector<std::vector<uint8_t>> texels;     // the entries' pixels
};
std::map<std::string, std::unique_ptr<Model>> gModels;   // by pack name; nullptr: it did not load

CTextureEntry* LoadTexture(const std::filesystem::path& path, std::vector<uint8_t>& texels) {
    std::vector<uint8_t> file;
    if (!ReadFile(path, file)) return nullptr;
    int32_t width = 0, height = 0, bytes = 0;
    std::string source;
    if (!ReadTga(file.data(), file.size(), width, height, bytes, source) || width > 256 || height > 256) return nullptr;
    Brighten(source, bytes, kTexelGain);
    const int32_t destBytes = gTextureBytesPerPixel[gei_TextureSettings.mFormat];
    texels.assign(static_cast<size_t>(width) * height * destBytes + 4, 0);
    // UncompressedConvert leaves the texture's average colour in a global no one reads; it is kept as the game left it.
    const uint32_t average = gTextureAverageColour;
    const uint32_t alpha = UncompressedConvert(texels.data(), source.data(), width, height, nullptr, destBytes, bytes, 0);
    gTextureAverageColour = average;
    auto* entry = new CTextureEntry;
    // As CTextureLoadManager::LoadFile flags a TGA (0x1000000 | its load flags 0x300 >> 2), cut out where it has alpha:
    // a model's parts are drawn in any order, so they are not blended.
    entry->mFlags = 0x10000c0u | ((alpha & 1) != 0 ? 0x100u : 0u);
    entry->mData = texels.data();
    entry->SetSize(width, height);
    entry->mPitch = static_cast<int16_t>(width);
    return entry;
}

Model* LoadModel(const char* name) {
    auto found = gModels.find(name);
    if (found != gModels.end()) return found->second.get();
    std::unique_ptr<Model>& slot = gModels[name];
    const std::filesystem::path root = Root();
    if (root.empty()) return nullptr;
    std::vector<uint8_t> bytes;
    auto model = std::make_unique<Model>();
    std::string reason;
    if (!ReadFile(root / (std::string(name) + ".p2m"), bytes) ||
        !tpw::ReadPs2Pack(bytes.data(), bytes.size(), model->pack, reason)) {
        if (Debug()) std::fprintf(stderr, "ps2: %s: not loaded (%s)\n", name, reason.empty() ? "no file" : reason.c_str());
        return nullptr;
    }
    const tpw::Ps2PackClip* walk = FindClip(model->pack, 0, 0);
    if (walk == nullptr && !model->pack.clips.empty()) walk = &model->pack.clips[0];
    if (walk == nullptr) return nullptr;
    model->name = name;
    model->walk = walk;
    model->anchor = AnchorOf(model->pack, *walk);
    // Which way the triangles wind: the normals of a closed model point away from its middle.
    {
        float mid[3] = {0, 0, 0};
        std::vector<float> p(model->pack.vertices * 3);
        for (uint32_t v = 0; v < model->pack.vertices; ++v) {
            FramePosition(model->pack, *walk, 0, v, &p[v * 3]);
            for (int i = 0; i < 3; ++i) mid[i] += p[v * 3 + i] / model->pack.vertices;
        }
        double out = 0;
        for (const tpw::Ps2PackTriangle& t : model->pack.triangles) {
            const float* a = &p[t.vertex[0] * 3];
            const float* b = &p[t.vertex[1] * 3];
            const float* c = &p[t.vertex[2] * 3];
            const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
            out += n[0] * (a[0] - mid[0]) + n[1] * (a[1] - mid[1]) + n[2] * (a[2] - mid[2]);
        }
        model->normalSign = out < 0 ? -1.0f : 1.0f;
        model->head = HeadPartOf(p.data(), model->pack.vertices, model->pack.triangles.data(), model->pack.triangles.size(),
                                 model->pack.textures);
    }
    model->sign = SignTriangles(model->pack.triangles.data(), model->pack.triangles.size(), model->pack.textures);
    model->texels.resize(model->pack.textures.size());
    for (size_t i = 0; i < model->pack.textures.size(); ++i) {
        CTextureEntry* entry = LoadTexture(root / "tex" / (model->pack.textures[i] + ".tga"), model->texels[i]);
        model->textures.push_back(entry);
        if (entry == nullptr && Debug()) std::fprintf(stderr, "ps2: %s: texture %s not loaded\n", name, model->pack.textures[i].c_str());
    }
    if (Debug())
        std::fprintf(stderr, "ps2: %s: %u vertices, %zu triangles, %zu clips, height %.1f, winding %+.0f, head %zu triangles "
                     "height %.1f, sign %zu triangles\n",
                     name, model->pack.vertices, model->pack.triangles.size(), model->pack.clips.size(),
                     model->anchor.height, model->normalSign, model->head.triangles.size(), model->head.height,
                     static_cast<size_t>(std::count(model->sign.begin(), model->sign.end(), 1)));
    slot = std::move(model);
    return slot.get();
}

// A sprite set's model and where its kid's picture is in its sprites: the medians, over its walk's records, of the
// picture's top and bottom in the used frame (SetupVertices hangs the picture that far, times the sprite's 20 * mF2C
// clip units, from the projected point).
struct SetInfo {
    std::string name;
    Model* model = nullptr;
    SetKind kind = SetKind::Kid;
    float top = 0, bottom = 0;
};
std::unordered_map<const ESP_CSprite*, SetInfo> gSets;

const SetInfo& SetOf(ESP_CSprite* sprite) {
    SetInfo& set = gSets[sprite];
    if (set.name == sprite->mName) return set;
    set = SetInfo{};
    set.name = sprite->mName;
    const SetPack choice = PackForSet(sprite->mName);
    const char* pack = choice.pack;
    if (pack == nullptr) return set;
    set.model = LoadModel(pack);
    set.kind = choice.kind;
    const ESP_CSprite::Action& walk = sprite->mActions[1].mFrames != 0 ? sprite->mActions[1] : sprite->mActions[0];
    const CSpriteFileIO* file = sprite->mFile;
    std::vector<float> tops, bottoms;
    const int32_t views = walk.mViews != 0 ? 5 : 1;
    for (int32_t i = 0; file != nullptr && i < walk.mFrames * views; ++i) {
        const int32_t at = walk.mFirstFrame + i;
        if (at < 0 || at >= file->mCount) break;
        const SSpriteEngineData& record = file->mRecords[at];
        if (record.mUsedHeight == 0) continue;
        tops.push_back(static_cast<float>(record.mOffsetY) / record.mUsedHeight);
        bottoms.push_back(static_cast<float>(record.mOffsetY + record.mHeight) / record.mUsedHeight);
    }
    if (!tops.empty()) {
        std::nth_element(tops.begin(), tops.begin() + tops.size() / 2, tops.end());
        std::nth_element(bottoms.begin(), bottoms.begin() + bottoms.size() / 2, bottoms.end());
        set.top = tops[tops.size() / 2];
        set.bottom = bottoms[bottoms.size() / 2];
    }
    if (Debug() && file != nullptr && walk.mFirstFrame < file->mCount) {
        const SSpriteEngineData& r = file->mRecords[walk.mFirstFrame];
        std::fprintf(stderr, "ps2: set %s -> %s, picture %.3f..%.3f (record %dx%d used %dx%d offset %d,%d)\n", sprite->mName,
                     pack, set.top, set.bottom, r.mWidth, r.mHeight, r.mUsedWidth, r.mUsedHeight, r.mOffsetX, r.mOffsetY);
    }
    if (set.bottom == set.top) set.model = nullptr;
    if (set.kind == SetKind::Head && set.model != nullptr && set.model->head.triangles.empty()) set.model = nullptr;
    return set;
}

// One kid's model state, by its listed sprite record (one per script, so it follows the kid).
struct Person {
    Model* model = nullptr;
    SetKind kind = SetKind::Kid;
    float top = 0, bottom = 0;
    const SSpriteEngineData* record = nullptr;   // the record it was captured showing
    int32_t handle = 0;                          // its script's
    int32_t action = 0, frameBase = 0, frames = 1, viewOffset = 0;
    uint64_t captured = 0;                       // the frame it was captured in
    uint64_t drawn = 0;                          // the frame it is drawn as a model in
    float x = 0, y = 0, z = 0;                   // where its feet are: the sprite's place (a ride's head: its middle)
    float height = 0;                            // its picture's height in the world
    float up[3] = {0, 1, 0}, ahead[3] = {1, 0, 0}, right[3] = {0, 0, -1};   // a ride's head's: its node's axes
    // Followed from frame to frame.
    float lastX = 0, lastZ = 0;
    bool seen = false;
    double moved = -1;                           // when it last moved
    float heading = 0, target = 0;
    bool headed = false;
    double lastTime = 0;
    int32_t lastAction = -1, lastView = -1;
    uint32_t actions = 0;                        // actions begun, for the idle's choice
    double actionStart = 0;
    int32_t lastFrame = -1;
    double frameChange = 0, frameInterval = 0.1;
};
std::unordered_map<const SSpriteGameData*, Person> gPeople;
uint64_t gFrame = 1;   // CSpriteManager::Prepare's calls

// The rides' heads' node matrices, by the head's script handle, and the frame each was given in.
struct HeadNote {
    float m[16];
    uint64_t frame = 0;
};
std::unordered_map<int32_t, HeadNote> gHeads;

void NoteRideHead(int32_t handle, const float* matrix) {
    if (!gModPs2Characters || matrix == nullptr) return;
    HeadNote& note = gHeads[handle];
    std::memcpy(note.m, matrix, sizeof note.m);
    note.frame = gFrame;
}

float Normalise(float* v) {
    const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 1e-12f)
        for (int i = 0; i < 3; ++i) v[i] /= len;
    return len;
}

// Which of the head node's axes the face looks along: 0 its z, 1 its -z, 2 its x, 3 its -x (TPW_PS2_HEADFACE, the
// facing check; calibrated against the heads' pictures).
int32_t HeadFace() {
    static const int32_t face = [] {
        const char* v = std::getenv("TPW_PS2_HEADFACE");
        return v != nullptr && v[0] >= '0' && v[0] <= '3' ? v[0] - '0' : 0;
    }();
    return face;
}

// A ride's head's axes from its node's matrix (rows 0 to 2 its x, y and z, row 3 its place): up its y; ahead the
// face's axis, square to up; right up x ahead, so that the model is not mirrored (the walkers' right x ahead is down).
bool HeadAxes(const float* m, float* up, float* ahead, float* right) {
    for (int i = 0; i < 3; ++i) up[i] = m[4 + i];
    if (Normalise(up) <= 1e-12f) return false;
    const int32_t face = HeadFace();
    const float* axis = face < 2 ? &m[8] : &m[0];
    const float sign = (face & 1) != 0 ? -1.0f : 1.0f;
    for (int i = 0; i < 3; ++i) ahead[i] = sign * axis[i];
    const float along = ahead[0] * up[0] + ahead[1] * up[1] + ahead[2] * up[2];
    for (int i = 0; i < 3; ++i) ahead[i] -= along * up[i];
    if (Normalise(ahead) <= 1e-12f) return false;
    right[0] = up[1] * ahead[2] - up[2] * ahead[1];
    right[1] = up[2] * ahead[0] - up[0] * ahead[2];
    right[2] = up[0] * ahead[1] - up[1] * ahead[0];
    return true;
}

float AngleOfView(int32_t viewOffset) {
    // The sprite's eight views: viewOffset * 45 degrees (calibrated against the walks' headings, TPW_PS2_DEBUG).
    return -static_cast<float>(viewOffset) * (kPi / 4) + kPi / 2;
}

float Wrap(float a) {
    while (a > kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    return a;
}

// The clip and the time in it a kid shows now.
const tpw::Ps2PackClip* ClipNow(const Person& person, const Model& model, float& t, double now) {
    const int32_t drawn = person.kind == SetKind::Staff ? DrawnStaffAction() : -1;
    ClipChoice choice = ClipFor(person.kind, drawn >= 0 ? drawn : person.action);
    const tpw::Ps2Pack& pack = model.pack;
    double since = now - person.actionStart;
    if (choice.time == ClipTime::AfterIntro) {
        // The intro (the group before) once, then the group looped from its start.
        const tpw::Ps2PackClip* intro = choice.group > 0 ? FindClip(pack, choice.group - 1, 0) : nullptr;
        const double introLength = intro != nullptr ? intro->length / static_cast<double>(kClipRate) : 0.0;
        if (intro != nullptr && since < introLength) {
            t = static_cast<float>(since * kClipRate);
            return intro;
        }
        since -= introLength;
        choice.time = ClipTime::Loop;
    }
    if (choice.time == ClipTime::Idle) {
        // One of the idles there are, by the kid's hash and how many actions it has begun.
        int32_t idles = 0;
        for (const tpw::Ps2PackClip& c : pack.clips) idles += c.group == 2 ? 1 : 0;
        const uint64_t key = reinterpret_cast<uintptr_t>(&person) ^ (static_cast<uint64_t>(person.actions) << 40);
        choice.clip = static_cast<uint8_t>(idles > 0 ? RenderHash(key) % static_cast<uint32_t>(idles) : 0);
    }
    const tpw::Ps2PackClip* clip = FindClip(pack, choice.group, choice.clip);
    if (clip == nullptr && choice.group == 1) clip = FindClip(pack, 0, 0), choice.time = ClipTime::Frame;
    if (clip == nullptr) clip = FindClip(pack, 2, 0), choice.time = ClipTime::Loop;
    if (clip == nullptr) clip = &pack.clips[0];
    const float length = clip->length > 0 ? static_cast<float>(clip->length) : 1.0f;
    switch (choice.time) {
    case ClipTime::Frame: {
        const double frac = std::min((now - person.frameChange) / person.frameInterval, 0.999);
        const double phase = (person.frameBase + frac) / std::max(person.frames, 1);
        t = static_cast<float>(phase - std::floor(phase)) * length;
        break;
    }
    case ClipTime::Once: t = std::min(static_cast<float>(since * kClipRate), length); break;
    case ClipTime::Loop:
    case ClipTime::AfterIntro:
    case ClipTime::Idle: t = std::fmod(static_cast<float>(since * kClipRate), length); break;
    }
    return clip;
}

void Capture(SSpriteGameData* listed, const tagSCRIPT* script, ESP_CSprite* sprite, int32_t actionNumber, int32_t frameBase,
             int32_t viewOffset) {
    if (!gModPs2Characters || listed == nullptr || sprite == nullptr) return;
    const SetInfo& set = SetOf(sprite);
    if (set.model == nullptr) return;
    Person& person = gPeople[listed];
    const double now = Now();
    if (person.model != set.model) person = Person{};   // another kid on this record
    person.model = set.model;
    person.kind = set.kind;
    person.top = set.top;
    person.bottom = set.bottom;
    person.record = listed->mSprite;
    person.handle = script != nullptr ? script->mSlot : 0;
    person.captured = gFrame;
    person.frames = sprite->mActions[actionNumber & 15].mFrames;
    person.viewOffset = viewOffset;
    if (actionNumber != person.lastAction) {
        person.lastAction = actionNumber;
        person.actionStart = now;
        ++person.actions;
    }
    person.action = actionNumber;
    if (frameBase != person.lastFrame) {
        if (person.lastFrame >= 0) {
            const double gap = now - person.frameChange;
            if (gap > 0.01 && gap < 0.6) person.frameInterval = 0.7 * person.frameInterval + 0.3 * gap;
        }
        person.lastFrame = frameBase;
        person.frameChange = now;
    }
    person.frameBase = frameBase;
    // The heading: where it walks; standing, where its sprite looks once that changes.
    const float x = listed->mPosition.x, z = listed->mPosition.z;
    if (person.seen) {
        const float dx = x - person.lastX, dz = z - person.lastZ;
        const float d2 = dx * dx + dz * dz;
        if (d2 > 25.0f) {
            person.headed = false;   // a jump: another kid, or put down elsewhere
        } else if (d2 > 1e-6f) {
            person.target = std::atan2(dz, dx);
            person.moved = now;
            if (!person.headed) person.heading = person.target, person.headed = true;
            if (Debug() && person.lastView != viewOffset)
                std::fprintf(stderr, "ps2: calibrate view %d heading %.0f\n", viewOffset, person.target * 180 / kPi);
        }
    }
    if (now - person.moved > 0.5 && (person.lastView != viewOffset || !person.headed)) {
        person.target = AngleOfView(viewOffset);
        if (!person.headed) person.heading = person.target, person.headed = true;
    }
    person.lastView = viewOffset;
    const float dt = static_cast<float>(std::min(now - person.lastTime, 0.1));
    person.lastTime = now;
    const float turn = Wrap(person.target - person.heading);
    const float step = kTurnRate * dt;
    person.heading = Wrap(person.heading + (turn > step ? step : turn < -step ? -step : turn));
    person.lastX = x;
    person.lastZ = z;
    person.seen = true;
}

void Prepare(SSpriteGameData** sprites, int32_t count) {
    ++gFrame;
    if (!gModPs2Characters) return;
    const sMatrix* m = gei_FinalMatrix;
    const bool firstPerson = (gFirstPersonFlags & kFirstPersonAny) != 0;
    for (int32_t i = 0; i < count; ++i) {
        SSpriteGameData* sprite = sprites[i];
        auto found = gPeople.find(sprite);
        if (found == gPeople.end()) continue;
        Person& person = found->second;
        // Captured this frame (or the one before) with the record the sprite shows, and big enough on the screen.
        if (person.model == nullptr || person.record != sprite->mSprite || gFrame - person.captured > 2) continue;
        const float w0 = sprite->mScreenW, y0 = sprite->mScreenY;
        if (w0 <= 0.0f) continue;
        // The picture's top and bottom edges on the screen, as SetupVertices hangs them.
        const float reach = kReachPerSize * sprite->mF2C;
        const float edge1 = y0 + reach * person.top, edge2 = y0 + reach * person.bottom;
        if (std::fabs(edge2 - edge1) / w0 < NearestCard(person.kind, firstPerson)) continue;
        if (person.kind == SetKind::Head) {
            // A ride's head: turned by its node's matrix given this frame (or the one before), as large as its
            // picture, at the picture's middle (the point a clip y of the edges' middle hangs from the sprite's point).
            auto note = gHeads.find(person.handle);
            if (note == gHeads.end() || gFrame - note->second.frame > 2) continue;
            if (!HeadAxes(note->second.m, person.up, person.ahead, person.right)) continue;
            const float gx = m->m[0][1], gy = m->m[1][1], gz = m->m[2][1];
            const float g2 = gx * gx + gy * gy + gz * gz;
            if (g2 <= 1e-24f) continue;
            person.height = kHeightPerHeadCard * CardHeight(edge1, edge2, gx, gy, gz);
            const float hang = (0.5f * (edge1 + edge2) - y0) / g2;
            person.x = sprite->mPosition.x + hang * gx;
            person.y = sprite->mPosition.y + hang * gy;
            person.z = sprite->mPosition.z + hang * gz;
            person.drawn = gFrame;
            for (CTextureEntry* entry : person.model->textures)
                if (entry != nullptr) gei_TCache->Prepare(entry, sprite->mDepth);
            continue;
        }
        // The picture is a card facing the screen; the camera looks too steeply down to tell a standing kid's height
        // from how its picture leans, so the kid is as tall as its card times kHeightPerCard, its feet on the sprite's
        // point (the ground the game puts it on, where its shadow is).
        person.height = kHeightPerCard * CardHeight(edge1, edge2, m->m[0][1], m->m[1][1], m->m[2][1]);
        if (person.height <= 0.0f) continue;
        person.x = sprite->mPosition.x;
        person.y = sprite->mPosition.y;
        person.z = sprite->mPosition.z;
        person.drawn = gFrame;
        for (CTextureEntry* entry : person.model->textures)
            if (entry != nullptr) gei_TCache->Prepare(entry, sprite->mDepth);
    }
}

bool IsModel(const SSpriteGameData* sprite) {
    auto found = gPeople.find(sprite);
    return found != gPeople.end() && found->second.drawn == gFrame && !Beside();
}

int32_t PickOnly(SSpriteGameData* sprite) {
    SSpriteDrawParameters params = {};
    SVertex vertices[4] = {};
    params.mVertices = vertices;
    return sprite->CalcRegular(&params);
}

int32_t Draw(SSpriteGameData** sprites, int32_t count) {
    if (!gModPs2Characters) return 1;
    const double now = Now();
    const sMatrix* m = gei_FinalMatrix;
    const bool mirror = Mirror();

    // Every model's vertices in the view's clip space, lit.
    struct Posed {
        const Person* person;
        const SSpriteGameData* sprite;
        size_t first;   // into `vertices`
        bool sign;      // the strike sign is drawn (its clip is the strike's)
    };
    static std::vector<Posed> posed;
    static std::vector<SFVF_TLCTEX> vertices;
    static std::vector<float> world, normals;
    posed.clear();
    vertices.clear();
    // The light: from above, ahead and to one side, as the park's sun is (fixed: the engine's light chain is the 3D
    // objects', not ready in the sprite pass).
    const float light[3] = {0.40f, 0.80f, -0.45f};
    const float lightLen = std::sqrt(light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
    for (int32_t i = 0; i < count; ++i) {
        auto found = gPeople.find(sprites[i]);
        if (found == gPeople.end() || found->second.drawn != gFrame) continue;
        const Person& person = found->second;
        const Model& model = *person.model;
        const tpw::Ps2Pack& pack = model.pack;
        const bool head = person.kind == SetKind::Head;
        float t = 0;
        // A ride's head holds the walk's first frame, the one its head was found in.
        const tpw::Ps2PackClip* clip = head ? model.walk : ClipNow(person, model, t, now);
        const int32_t f0 = std::min(static_cast<int32_t>(t), clip->frames - 1);
        const int32_t f1 = std::min(f0 + 1, clip->frames - 1);
        const float blend = t - static_cast<float>(f0);
        const float scale = person.height / (head ? model.head.height : model.anchor.height);
        const float c = std::cos(person.heading), s = std::sin(person.heading);
        const float side = mirror ? -1.0f : 1.0f;
        const uint32_t n = pack.vertices;
        world.resize(n * 3);
        normals.assign(n * 3, 0.0f);
        for (uint32_t v = 0; v < n; ++v) {
            float a[3], b[3];
            FramePosition(pack, *clip, f0, v, a);
            FramePosition(pack, *clip, f1, v, b);
            if (head) {
                // About the head's middle: its up the node's up, its face the node's ahead.
                const float ahead = -(a[1] - model.head.cy), right = -(a[0] - model.head.cx) * side, up = a[2] - model.head.cz;
                const float at[3] = {person.x, person.y, person.z};
                for (int j = 0; j < 3; ++j)
                    world[v * 3 + j] = at[j] +
                                       scale * (ahead * person.ahead[j] + right * person.right[j] + up * person.up[j]) +
                                       (Beside() && j == 0 ? person.height : 0.0f);
                continue;
            }
            const float mx = a[0] + (b[0] - a[0]) * blend - model.anchor.cx;
            const float my = a[1] + (b[1] - a[1]) * blend - model.anchor.cy;
            const float mz = a[2] + (b[2] - a[2]) * blend - model.anchor.feet;
            // Ahead, to the right and up in the model; ahead is the heading on the world's ground (x, z), right a
            // quarter turn from it, up the world's y. Every PS2 character faces -y with its left hand at +x.
            const float ahead = -my;
            const float right = -mx * side;
            world[v * 3 + 0] = person.x + scale * (ahead * c + right * s) + (Beside() ? person.height : 0.0f);
            world[v * 3 + 1] = person.y + scale * mz;
            world[v * 3 + 2] = person.z + scale * (ahead * s - right * c);
        }
        const std::vector<uint32_t>* only = head ? &model.head.triangles : nullptr;
        const size_t drawnCount = only != nullptr ? only->size() : pack.triangles.size();
        const bool showSign = !head && ShowsSign(clip->group);
        for (size_t k = 0; k < drawnCount; ++k) {
            const size_t at = only != nullptr ? (*only)[k] : k;
            if (model.sign[at] != 0 && !showSign) continue;
            const tpw::Ps2PackTriangle& tri = pack.triangles[at];
            const float* p0 = &world[tri.vertex[0] * 3];
            const float* p1 = &world[tri.vertex[1] * 3];
            const float* p2 = &world[tri.vertex[2] * 3];
            const float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
            const float e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
            const float nn[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
            for (uint16_t k : tri.vertex)
                for (int j = 0; j < 3; ++j) normals[k * 3 + j] += nn[j];
        }
        const uint32_t mask = static_cast<uint32_t>(sprites[i]->mF24) & static_cast<uint32_t>(gei_LightManager->mF04);
        // A mirrored world turns the triangles' winding over, and with it the normals.
        const float sign = model.normalSign * side;
        const size_t first = vertices.size();
        vertices.resize(first + n);
        gei_MathUnit->Transform(const_cast<sMatrix*>(m), &vertices[first], world.data(), kStride, 12, static_cast<int32_t>(n), 0);
        for (uint32_t v = 0; v < n; ++v) {
            const float* nv = &normals[v * 3];
            const float len = std::sqrt(nv[0] * nv[0] + nv[1] * nv[1] + nv[2] * nv[2]);
            const float lit = len > 0 ? sign * (nv[0] * light[0] + nv[1] * light[1] + nv[2] * light[2]) / (len * lightLen) : 0;
            const float value = std::min(1.0f, 0.62f + 0.45f * std::max(lit, 0.0f));
            const uint32_t g = static_cast<uint32_t>(255.0f * value);
            SFVF_TLCTEX& out = vertices[first + v];
            out.colour = mask & ((g << 24) | (g << 16) | (g << 8) | 0xffu);
            out.specular = 0;
        }
        posed.push_back({&person, sprites[i], first, showSign});
    }
    if (posed.empty()) return 1;

    // One triangle list per texture page and blending mode, as the sprites have.
    struct Kind {
        CTextureBase* page;
        uint32_t blend;
        bool operator<(const Kind& o) const { return page != o.page ? page < o.page : blend < o.blend; }
    };
    const bool fog = (gei_Display.mFlags & kDisplayFog) != 0;
    const bool vertexFog = fog && (gei_Display.mFlags & kDisplayVertexFog) != 0;
    std::map<Kind, int> kinds;
    auto kindOf = [&](CTextureEntry* entry) {
        Kind k{nullptr, 0x2756};
        if (entry != nullptr) {
            k.page = (entry->mFlags & CTextureEntry::kOnPage) != 0 ? entry->mTexture : nullptr;
            k.blend = entry->GetBlendingMode();
        }
        if (fog) k.blend |= kFogCleared;
        return k;
    };
    for (const Posed& p : posed)
        for (CTextureEntry* entry : p.person->model->textures) kinds[kindOf(entry)] = 0;

    CDrawPrimitive& draw = *gei_DrawPrimitive;
    draw.mCurrent = nullptr;
    for (const auto& kind : kinds) {
        SPrimitive* primitive = nullptr;
        for (const Posed& p : posed) {
            const Model& model = *p.person->model;
            const std::vector<uint32_t>* only = p.person->kind == SetKind::Head ? &model.head.triangles : nullptr;
            const size_t drawnCount = only != nullptr ? only->size() : model.pack.triangles.size();
            for (size_t at = 0; at < drawnCount; ++at) {
                const size_t index = only != nullptr ? (*only)[at] : at;
                if (model.sign[index] != 0 && !p.sign) continue;
                const tpw::Ps2PackTriangle& tri = model.pack.triangles[index];
                CTextureEntry* entry = tri.texture < model.textures.size() ? model.textures[tri.texture] : nullptr;
                const Kind k = kindOf(entry);
                if (k.page != kind.first.page || k.blend != kind.first.blend) continue;
                // Room for this triangle clipped (AddPoly_TLCTEX wants ten vertices and 24 indices), else a new list.
                if (primitive != nullptr &&
                    (primitive->mVertexCount > 60000 ||
                     reinterpret_cast<uintptr_t>(draw.mVertexEnd) <
                         reinterpret_cast<uintptr_t>(primitive->mVertexData) +
                             static_cast<uint32_t>(draw.VertexSize()) * (static_cast<uint32_t>(primitive->mVertexCount) + 16u) ||
                     reinterpret_cast<uintptr_t>(draw.mIndexEnd) <
                         reinterpret_cast<uintptr_t>(primitive->mIndexData) + 2u * (static_cast<uint32_t>(primitive->mIndexCount) + 32u))) {
                    if (draw.ExecuteCurrentPrimitive(-1.0f) == 0) return 0;
                    primitive = nullptr;
                }
                if (primitive == nullptr) {
                    primitive = draw.GetNextPrimitive(kSpritePool, kTriangleList, kTLCTEXFormat,
                                                      static_cast<int32_t>(kind.first.blend), kind.first.page);
                    if (primitive == nullptr) return 1;
                }
                SFVF_TLCTEX corners[10];   // the clipper's work space: a clipped triangle grows to up to nine
                for (int j = 0; j < 3; ++j) {
                    corners[j] = vertices[p.first + tri.vertex[j]];
                    const float u = tri.uv[j * 2], v = tri.uv[j * 2 + 1];
                    corners[j].u = entry != nullptr ? entry->mUOffset + u * entry->mUSize : 0.0f;
                    corners[j].v = entry != nullptr ? entry->mVOffset + v * entry->mVSize : 0.0f;
                }
                const int32_t before = primitive->mVertexCount;
                draw.AddPoly_TLCTEX(corners, 3, kStride, nullptr, 0);
                const int32_t added = primitive->mVertexCount - before;
                if (added <= 0) continue;
                auto* first = reinterpret_cast<SFVF_TLCTEX*>(primitive->mVertexData + static_cast<size_t>(before) * kStride);
                if (vertexFog) {
                    const uint8_t fogged = gei_LightManager->CalcVertexFogIntensities(first, added, draw.VertexSize());
                    primitive->mBlendingMode &= ~SPrimitive::kFogCleared;
                    if (fogged != 0) primitive->mBlendingMode |= SPrimitive::kFogged;
                }
                primitive->mDepth += gei_MathUnit->Project(first, kStride, added, 0);
            }
        }
        if (primitive != nullptr && draw.ExecuteCurrentPrimitive(-1.0f) == 0) return 0;
    }

    if (Debug()) {
        static double last = 0;
        if (now - last > 1.0) {
            last = now;
            for (const Posed& p : posed)
                std::fprintf(stderr, "ps2: draw %s%s action %d%s frame %d/%d view %d heading %.0f height %.2f at (%.1f, %.1f, %.1f) "
                             "screen (%.3f, %.3f)\n",
                             p.person->model->name.c_str(), p.person->kind == SetKind::Head ? " (head)" : "", p.person->action,
                             p.sign ? " sign" : "", p.person->frameBase, p.person->frames,
                             p.person->viewOffset,
                             (p.person->kind == SetKind::Head ? std::atan2(p.person->ahead[2], p.person->ahead[0]) : p.person->heading) * 180 / kPi,
                             p.person->height, p.person->x, p.person->y,
                             p.person->z, p.sprite->mScreenX / p.sprite->mScreenW, p.sprite->mScreenY / p.sprite->mScreenW);
        }
    }
    return 1;
}

}  // namespace

void Ps2InstallRuntime() { gPs2Runtime = Ps2Runtime{&Capture, &Prepare, &IsModel, &PickOnly, &Draw, &NoteRideHead}; }

