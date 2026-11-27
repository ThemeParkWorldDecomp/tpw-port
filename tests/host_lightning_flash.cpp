// host_lightning_flash.cpp: the launcher's "Hide lightning flash" (the port's fix lightflash, include/PortLightningFlash.h): a
// strike of lightning and the frames drawn after it, with the setting off and on. The game's code is the port's
// src/engine/Rain.cpp: CRain::AddLightning and SLightning::Init (the bolt), CRain::LightningFlash (the flash asked for, as
// the island screen asks with each bolt, CLBScreenComponentIslands::TickIslands), and the frame's lightning,
// CRain::__RenderLightning, which draws the flash over the whole screen (CRain::__LightningFlash) and then every bolt. The
// functions they call are stand-ins here: the clock is the test's (UTimer::GetAbsolute, in steps of a frame's time), the
// random numbers come from fixed generators that log their draws, and the two drawings, the flash's sprite
// (CDrawPrimitive::DrawSprite) and a bolt (CRain::RenderLightning2), log what they are asked to draw.
//
// Off: each strike's flash is drawn, first in the frame, as the original draws it (a white sprite over the whole screen,
// blending 0x9a1), on the strike's frame (and, with the port's fixes, for the port's step's time after it, 31 ms), then the
// bolts. On (only with the port's fixes): no flash is ever drawn, and every other call is the same as with it off, in the
// same order with the same arguments: the same bolts in the same colours, the same random numbers, the same bolts' times and
// the same answers. At three frame rates, with strikes close together, bolts with and without a seed, and a park's bolt that
// comes with no flash. Made in both builds: without the port's fixes it checks only the setting off, the original's flash.
// Exits nonzero on a failure.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "LbTime.h"
#include "PortLightningFlash.h"
#include "platform/msl.h"
#include "platform/samsutils.h"
#include "tb_drawprimitive.h"
#include "tb_rain.h"

// The draw primitive the flash's sprite goes to: only its address is used (DrawSprite is a fake below).
alignas(16) static unsigned char gDrawPrimitiveStorage[4096];
CDrawPrimitive* gei_DrawPrimitive = reinterpret_cast<CDrawPrimitive*>(gDrawPrimitiveStorage);

namespace {

int gFailures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%s: %s\n", ok ? "ok" : "FAILED", what.c_str());
    if (!ok) ++gFailures;
}

uint64_t gNow = 0;                 // the clock, microseconds
uint32_t gRandSeed = 1;            // msl_rand's generator
uint32_t gRandomSeed = 7;          // Random's
uint32_t gCycles = 0x1000;         // GetCycleCount's
int32_t gSpriteAnswer = 1;         // what DrawSprite answers
std::vector<std::string> gCalls;   // the fakes' log, for one frame

std::string Number(double value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.6g", value);
    return text;
}

std::string Vector(const D3DVECTOR& v) { return Number(v.x) + "," + Number(v.y) + "," + Number(v.z); }

uint32_t Next(uint32_t& seed) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

bool IsFlash(const std::string& call) { return call.rfind("DrawSprite", 0) == 0; }

// The launcher's setting, as launcher/main.cpp sets it before the game starts. Without the port's fixes there is no such
// setting, and only off is run.
void SetHideFlash(bool on) {
#ifdef TPW_TEST_PORT_FIX
    gPortNoLightningFlash = on;
#else
    (void)on;
#endif
}

// How long a strike's flash stays up: the port's step's time (framerate), the original's one frame without the fixes.
#ifdef TPW_TEST_PORT_FIX
constexpr uint64_t kFlashHold = kPortFrameStepTime;
#else
constexpr uint64_t kFlashHold = 1;
#endif

}  // namespace

// ---- the fakes
uint64_t SamsUtilities::UTimer::GetAbsolute() { return gNow; }
int32_t msl_rand() {
    const int32_t value = static_cast<int32_t>(Next(gRandSeed) & 0x7fff);
    gCalls.push_back("msl_rand " + std::to_string(value));
    return value;
}
int32_t Random() {
    const int32_t value = static_cast<int32_t>(Next(gRandomSeed) & 0x7fff);
    gCalls.push_back("Random " + std::to_string(value));
    return value;
}
uint32_t GetCycleCount() {
    gCycles += 0x111;
    gCalls.push_back("GetCycleCount " + std::to_string(gCycles));
    return gCycles;
}
void msl_assertion_failed(const char* condition, const char*, int32_t line) {
    gCalls.push_back(std::string("assertion ") + condition + " " + std::to_string(line));
}
int32_t CDrawPrimitive::DrawSprite(SSpriteDrawParameters* sprite, int32_t index) {
    std::string call = "DrawSprite " + std::to_string(index) + " flags " + std::to_string(sprite->mFlags) + " colour " +
                       std::to_string(sprite->mF08) + " blend " + std::to_string(sprite->mBlendingMode) + " texture " +
                       (sprite->mTexture != nullptr ? "set" : "none") + " uv " + Number(sprite->mU0) + "," +
                       Number(sprite->mV0) + "," + Number(sprite->mU1) + "," + Number(sprite->mV1) + " type " +
                       std::to_string(sprite->mF28) + " corners";
    const float* corners = static_cast<const float*>(sprite->mVertices);
    for (int i = 0; i < 16; ++i) call += " " + Number(corners[i]);
    gCalls.push_back(call);
    return gSpriteAnswer;
}
int32_t CRain::RenderLightning2(D3DVECTOR* from, D3DVECTOR* to) {
    gCalls.push_back("RenderLightning2 " + Vector(*from) + " " + Vector(*to) + " bolt " + std::to_string(mCurrentLightning) +
                     " colour " + std::to_string(mLightningColor) + " depth " + std::to_string(mLightningDepth) + " seed " +
                     std::to_string(mFA198));
    return 1;
}

namespace {

alignas(16) unsigned char gRainStorage[sizeof(CRain)];
CRain& Rain() { return *reinterpret_cast<CRain*>(gRainStorage); }

// What happens at a frame before its lightning is drawn.
enum class Strike { None, Island, IslandUnseeded, Park };

struct Frame {
    std::vector<std::string> calls;   // the fakes' calls: the strike's, then the frame's drawing
    int32_t answer = -1;              // __RenderLightning's
    int32_t flashAfter = -1;          // mLightningFlash after the frame
    int32_t bolts = -1;               // mF3308 after it
    std::string times;                // the bolts' times after it
};

// One run: `frames` frames `step` microseconds apart, with the strikes of `strikes` (frame number, kind), from a fresh
// rain, generators and clock (a second's gap after the last run, so the port's frame pace starts again as after a pause).
std::vector<Frame> Run(bool hide, uint64_t step, int frames, const std::vector<std::pair<int, Strike>>& strikes,
                       int32_t spriteAnswer = 1) {
    SetHideFlash(hide);
    std::memset(gRainStorage, 0, sizeof(gRainStorage));
    gRandSeed = 1;
    gRandomSeed = 7;
    gCycles = 0x1000;
    gSpriteAnswer = spriteAnswer;
    gNow += 1000000;
    std::vector<Frame> out;
    for (int f = 0; f < frames; ++f) {
        gNow += step;
        gCalls.clear();
        CRain& rain = Rain();
        rain.mDeltaMilliseconds = static_cast<int32_t>(step / 1000);
        rain.mDeltaTime = static_cast<float>(step) / 1000000.0f;
        for (const auto& strike : strikes) {
            if (strike.first != f) continue;
            const float x = 10.0f * static_cast<float>(f);
            switch (strike.second) {
            case Strike::Island:   // the island screen's: a bolt of 500 ms with a seed, and the flash
                rain.AddLightning({x, 0.0f, 3.0f}, {x + 1.0f, 500.0f, 5.0f}, 500, 1);
                rain.LightningFlash();
                break;
            case Strike::IslandUnseeded:   // a bolt without a seed (white) and the flash
                rain.AddLightning({x, 0.0f, -3.0f}, {x - 1.0f, 500.0f, -5.0f}, 400, 0);
                rain.LightningFlash();
                break;
            case Strike::Park:   // the park's (CWeather::UpdateTurn): a bolt of 2000 ms, no flash
                rain.AddLightning({x, 2.0f, 7.0f}, {x, 40.0f, 9.0f}, 2000, 1);
                break;
            case Strike::None:
                break;
            }
        }
        Frame frame;
        frame.answer = rain.__RenderLightning();
        frame.calls = gCalls;
        frame.flashAfter = rain.mLightningFlash;
        frame.bolts = rain.mF3308;
        for (int b = 0; b < rain.mF3308; ++b) frame.times += std::to_string(rain.mLightning[b].mTime) + " ";
        out.push_back(frame);
    }
    SetHideFlash(false);
    return out;
}

// The flash as the original draws it: a white sprite with no texture over the whole screen (corners at +-1, depth
// 0.001), blending 0x9a1, at index -1.
const std::string kFlash =
    "DrawSprite -1 flags 0 colour -1 blend 2465 texture none uv 0,0,0,0 type 0 corners -1 -1 0.001 1 1 -1 0.001 1 1 1 "
    "0.001 1 -1 1 0.001 1";

}  // namespace

int main() {
    // Strikes as the island screen makes them (one now and then, two close together, one without a seed) and a
    // park's bolt, at 60, 240 and 20 frames a second.
    const std::vector<std::pair<int, Strike>> strikes = {
        {2, Strike::Island}, {9, Strike::Park}, {30, Strike::Island}, {31, Strike::Island},
        {45, Strike::IslandUnseeded}, {70, Strike::Island}, {71, Strike::Park},
    };
    const uint64_t kSteps[] = {16667, 4167, 50000};
    for (uint64_t step : kSteps) {
        const std::string rate = std::to_string((1000000 + step / 2) / step) + " frames a second";
        const int frames = static_cast<int>(3000000 / step);   // three seconds
        std::vector<std::pair<int, Strike>> scaled;
        for (const auto& s : strikes) scaled.push_back({static_cast<int>(s.first * 16667 / step), s.second});
        const std::vector<Frame> off = Run(false, step, frames, scaled);

        // Setting off: the original's flash on the strike's frame (and the step's time after it), first in the frame, then
        // the bolts; nothing else draws it.
        int flashes = 0, expectedFlashes = 0, boltsDrawn = 0;
        bool flashesRight = true, firstInFrame = true, answers = true, flagClear = true;
        uint64_t lastStrike = 0;
        bool struck = false;
        for (int f = 0; f < frames; ++f) {
            const uint64_t now = static_cast<uint64_t>(f + 1) * step;
            for (const auto& s : scaled) {
                if (s.first == f && s.second != Strike::Park) {
                    lastStrike = now;
                    struck = true;
                }
            }
            const bool shown = struck && now < lastStrike + kFlashHold;
            if (shown) ++expectedFlashes;
            int here = 0;
            for (size_t c = 0; c < off[f].calls.size(); ++c) {
                const std::string& call = off[f].calls[c];
                if (IsFlash(call)) {
                    ++here;
                    if (call != kFlash) flashesRight = false;
                    for (size_t d = 0; d < c; ++d) {
                        if (off[f].calls[d].rfind("RenderLightning2", 0) == 0) firstInFrame = false;
                    }
                }
                if (call.rfind("RenderLightning2", 0) == 0) ++boltsDrawn;
            }
            if (here != (shown ? 1 : 0)) flashesRight = false;
            flashes += here;
            if (off[f].answer != 1) answers = false;
            if (off[f].flashAfter != 0) flagClear = false;
        }
        Check(flashes == expectedFlashes && flashes > 0 && flashesRight,
              rate + ", off: the original's flash on each strike's frame and for as long as it holds (" +
                  std::to_string(flashes) + " of " + std::to_string(expectedFlashes) + ")");
        Check(firstInFrame, rate + ", off: the flash is drawn before the frame's bolts");
        Check(boltsDrawn > 0 && answers && flagClear, rate + ", off: the bolts are drawn (" + std::to_string(boltsDrawn) +
                                                          "), every frame answers 1 and leaves no flash asked for");

#ifdef TPW_TEST_PORT_FIX
        // Setting on: no flash at all, and the frame's every other call, answer, flag and bolt the same as with it off.
        const std::vector<Frame> on = Run(true, step, frames, scaled);
        int onFlashes = 0;
        bool same = true, sameState = true;
        for (int f = 0; f < frames; ++f) {
            std::vector<std::string> withoutFlash;
            for (const std::string& call : off[f].calls) {
                if (!IsFlash(call)) withoutFlash.push_back(call);
            }
            for (const std::string& call : on[f].calls) {
                if (IsFlash(call)) ++onFlashes;
            }
            if (on[f].calls != withoutFlash) same = false;
            if (on[f].answer != off[f].answer || on[f].flashAfter != off[f].flashAfter || on[f].bolts != off[f].bolts ||
                on[f].times != off[f].times) {
                sameState = false;
            }
        }
        Check(onFlashes == 0, rate + ", on: no flash is drawn");
        Check(same, rate + ", on: the bolts and the random numbers are the original's, call for call");
        Check(sameState, rate + ", on: the same answers, the same bolts and their times, no flash left asked for");
#endif
    }

#ifdef TPW_TEST_PORT_FIX
    // A park's bolts alone (no flash asked for): the setting changes nothing.
    {
        const std::vector<std::pair<int, Strike>> park = {{1, Strike::Park}, {20, Strike::Park}, {21, Strike::Park}};
        const std::vector<Frame> off = Run(false, 16667, 150, park);
        const std::vector<Frame> on = Run(true, 16667, 150, park);
        bool same = true, none = true;
        for (size_t f = 0; f < off.size(); ++f) {
            if (on[f].calls != off[f].calls || on[f].answer != off[f].answer || on[f].times != off[f].times) same = false;
            for (const std::string& call : off[f].calls) {
                if (IsFlash(call)) none = false;
            }
        }
        Check(none, "a park's bolt asks for no flash");
        Check(same, "a park's bolts are drawn the same with the setting on");
    }

    // A flash whose sprite cannot be drawn: with the setting off the frame stops there (its bolts are not drawn, and it
    // answers 0, as the original); with it on nothing is asked of DrawSprite and the bolts are drawn.
    {
        const std::vector<std::pair<int, Strike>> one = {{3, Strike::Island}};
        const std::vector<Frame> off = Run(false, 16667, 6, one, 0);
        const std::vector<Frame> on = Run(true, 16667, 6, one, 0);
        bool offStops = off[3].answer == 0 && off[3].calls.size() >= 1 && IsFlash(off[3].calls.back());
        for (const std::string& call : off[3].calls) {
            if (call.rfind("RenderLightning2", 0) == 0) offStops = false;
        }
        bool onDraws = on[3].answer == 1;
        int bolts = 0;
        for (const std::string& call : on[3].calls) {
            if (IsFlash(call)) onDraws = false;
            if (call.rfind("RenderLightning2", 0) == 0) ++bolts;
        }
        Check(offStops, "off: a flash that cannot be drawn ends the frame, as the original");
        Check(onDraws && bolts == 1, "on: no flash is asked for, and the strike's bolt is drawn");
    }
#else
    // Without the port's fixes there is no setting: a park's bolt still asks for no flash, and a flash that cannot be
    // drawn still ends the frame.
    {
        const std::vector<Frame> off = Run(false, 16667, 150, {{1, Strike::Park}, {20, Strike::Park}});
        bool none = true;
        for (const Frame& frame : off) {
            for (const std::string& call : frame.calls) {
                if (IsFlash(call)) none = false;
            }
        }
        Check(none, "a park's bolt asks for no flash");
        const std::vector<Frame> stop = Run(false, 16667, 6, {{3, Strike::Island}}, 0);
        Check(stop[3].answer == 0 && !stop[3].calls.empty() && IsFlash(stop[3].calls.back()),
              "off: a flash that cannot be drawn ends the frame, as the original");
    }
#endif

    std::printf("%s\n", gFailures == 0 ? "all passed" : "FAILURES");
    return gFailures == 0 ? 0 : 1;
}
