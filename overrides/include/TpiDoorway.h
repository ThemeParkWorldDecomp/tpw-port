// TpiDoorway.h: the TPI Worlds and Rides mod's doorway floors (TPW_MOD_HOOK tpi_worlds, LoadM3D2Header and
// SRenderListEntry::CalcRenderEntry). Theme Park Inc's shops and rides darken their entrances with a floor textured
// doorway_alpha.tga (black, its alpha a gradient from the door inwards, blended: TPW_MOD_HOOK tpi_worlds in
// TGALoadAndConvert), laid a hair above the mesh's opaque base and drawn after it: 0.03 world units in Polar Zone's
// umbrella shop, 0.002 to 0.6 in the others. Each projected vertex's depth is rounded in single precision (the
// projection's clip z and w, then CMatrixPowerPC::Project's depth curve), and the curve's far slope (gei_Display.mZ6C: 7
// at zoom 130, 12 at 250, 22 at 500) multiplies that rounding, so the floor and the base each land up to one or two
// times mZ6C steps of the 24-bit depth buffer off their place, while the umbrella shop's floor is only about 1800 / zoom
// steps above its base. Up to the original's zoom limit (130) that floor still wins the depth test; at the port's farther
// zooms (to 500 with the free camera, 800 with "Zoom out further") the base wins it on whole floor triangles, depending
// on the exact camera, so the ground showed through the entrance as a white wedge that came and went as the camera
// turned (the maintainer, 2026-11-20, Polar Zone's umbrella shop at about zoom 250). In the mod's worlds such a floor is drawn
// kTpiDoorwayFarSteps times mZ6C depth steps nearer the camera than its place, after its projection, so it wins the test
// at every zoom and angle; its geometry, texture, colour, sort depth and draw order stay Theme Park Inc's. Header only
// and pure, so the host test (tests/host_tpi_doorway.cpp) sees what the game runs.
#pragma once


// TPW_PORT_FIX mods TpiDoorway | Mac: no such header | Port: the doorway floors of the mod's worlds (TPW_MOD_HOOK tpi_worlds): which face sets draw doorway_alpha.tga, and how far nearer the camera they are drawn | Why: tpw-mods tpi-worlds; Theme Park Inc's entrance floors lie a hair above the base under them, and at the port's farther zooms the ground showed through as a white wedge that came and went as the camera turned (the maintainer, Polar Zone's umbrella shop, 0.6.1)

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "TpiWorlds.h"
#include "m3d2.h"

namespace tpw {

// The mark of a doorway floor's face set, in its fourth flag byte (sFaceSet::mF0C[3]; zero in every mesh of both games).
inline constexpr uint8_t kTpiDoorwayMark = 1;

// How far the floor is moved nearer: this many times the depth curve's far slope (gei_Display.mZ6C), in steps of a
// 24-bit depth buffer (window depth 0 to 1): about 28 steps at zoom 130, 46 at 250, 86 at 500. Three times the slope still
// lost a few samples of a floor 0.002 above its base at zoom 400 in the host test's turns; four leaves 12 steps or more.
inline constexpr float kTpiDoorwayFarSteps = 4.0f;

// Whether a texture's first frame name (0x14 bytes, not always ended by a zero) is Theme Park Inc's doorway floor.
inline bool TpiDoorwayTexture(const char* name) {
    if (name == nullptr) return false;
    char text[0x15];
    std::memcpy(text, name, 0x14);
    text[0x14] = 0;
    return TpiEqualNoCase(text, "doorway_alpha.tga");
}

// Marks the face sets of a loaded mesh (its records relocated) that draw doorway_alpha.tga; answers how many.
inline int32_t TpiMarkDoorways(tag_sM3D2Header* header) {
    sFaceSet* set = header->mFaceSets.get();
    const sTexture* const textures = header->mTextures.get();
    const sTextureInfo* const infos = header->mTextureInfos.get();
    if (set == nullptr || textures == nullptr || infos == nullptr) return 0;
    int32_t marked = 0;
    for (uint32_t n = header->mFaceSetCount; n != 0; --n, ++set) {
        const sTexture* const texture = set->mTexture.get();
        if (texture == nullptr) continue;
        const std::ptrdiff_t index = texture - textures;
        if (index < 0 || index >= header->mTextureCount || !TpiDoorwayTexture(infos[index].mName.get())) continue;
        set->mF0C[3] |= kTpiDoorwayMark;
        ++marked;
    }
    return marked;
}

// Moves `count` vertices just projected by CMatrixPowerPC::Project's perspective type (x, y, clip z, w first, `stride`
// bytes apart; clip z runs from -w to w, two units of z/w per 2^-24 of window depth) kTpiDoorwayFarSteps times
// `farSlope` (gei_Display.mZ6C) depth steps nearer.
inline void TpiDoorwayNearer(uint8_t* vertices, int32_t stride, int32_t count, float farSlope) {
    const float shift = kTpiDoorwayFarSteps * farSlope / 8388608.0f;   // steps * 2^-23
    for (int32_t i = 0; i < count; ++i, vertices += stride) {
        float* const v = reinterpret_cast<float*>(vertices);
        v[2] -= shift * v[3];
    }
}

}  // namespace tpw

