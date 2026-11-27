// PortGroundPad.h: a ride's or shop's base drawn a little nearer the camera than the park's ground under it, a switch for
// tpw-port #125 (SRenderListEntry::CalcRenderEntry). A ride placed with flag 0x2000 (TPRide.cpp, SetBaseBlendFlags) has
// bit 0x800 (plain blending) on the texture record of each face set of its base, and with Ground blending on the ground's
// cells under it (CHField::Prepare, GetBlendedBaseTextures) and the base itself draw the same blended ground texture over
// the same place: the base is drawn over the ground at the same depth and wins the depth test only by GL_LEQUAL and the
// draw order. On the reporter's NVIDIA cards (Windows 10, a GeForce MX150 and an RTX 3070) whole parts of those bases
// showed the ground's own copy of the cells instead, lighter or blurrier, flickering as the camera moved and worse zoomed
// out: what a test lost by a depth step or two looks like. No Mac shows it, so the cause is not certain, and the switch is
// off unless the player sets it (launcher.ini's ground_pad_nearer or TPW_GROUND_PAD_NEARER, read once at the start, the
// number of depth steps); off, the Mac's. Header only and pure, so the host test (tests/host_ground_pad.cpp) sees what the
// game runs.
#pragma once


// TPW_PORT_FIX groundpad PortGroundPad | Mac: no such header | Port: the switch for drawing the bases of rides and shops nearer the camera than the ground under them, and how far | Why: tpw-port #125, bright rectangles flickering on the ground around rides and shops on NVIDIA cards

#include <cstdint>

namespace tpw::groundpad {

// The texture record bit (sTexture::mF00) SetBaseBlendFlags puts on the face sets of a ride's base: plain blending.
inline constexpr uint16_t kBaseRecord = 0x0800;

// The most depth steps the switch takes (launcher.ini ground_pad_nearer=1..kMostSteps).
inline constexpr int32_t kMostSteps = 64;

// How far a base is moved nearer: this many times the depth curve's far slope (gei_Display.mZ6C) steps of a 24-bit depth
// buffer, as TPW_MOD_HOOK tpi_worlds moves Theme Park Inc's doorway floors (TpiDoorway.h); 0, the switch is off and the
// bases keep the depth Project gives them. Set once by the launcher at the start of a game.
inline float nearerSteps = 0.0f;

// The switch's value from its text (launcher.ini or the environment): a whole number of steps from 1 to kMostSteps, or 0
// (off) for anything else.
inline int32_t StepsSetting(const char* text) {
    if (text == nullptr || *text == 0) return 0;
    int32_t steps = 0;
    for (const char* c = text; *c != 0; ++c) {
        if (*c < '0' || *c > '9' || steps > kMostSteps) return 0;
        steps = steps * 10 + (*c - '0');
    }
    return steps <= kMostSteps ? steps : 0;
}

// Whether a face set's texture record (NULL: an untextured face set) is a ride's base.
inline bool IsBase(const uint16_t* recordFlags) { return recordFlags != nullptr && (*recordFlags & kBaseRecord) != 0; }

// Moves `count` vertices just projected by CMatrixPowerPC::Project's perspective type (x, y, clip z, w first, `stride`
// bytes apart; clip z runs from -w to w, two units of z/w per 2^-24 of window depth) `steps` times `farSlope`
// (gei_Display.mZ6C) depth steps nearer.
inline void Nearer(uint8_t* vertices, int32_t stride, int32_t count, float steps, float farSlope) {
    const float shift = steps * farSlope / 8388608.0f;   // steps * 2^-23
    for (int32_t i = 0; i < count; ++i, vertices += stride) {
        float* const v = reinterpret_cast<float*>(vertices);
        v[2] -= shift * v[3];
    }
}

}  // namespace tpw::groundpad

