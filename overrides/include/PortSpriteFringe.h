#pragma once

// TPW_PORT_FIX spritefringe PortSpriteFringe | Mac: every nonzero sprite alpha writes depth, in texture-page order | Port: world sprites draw their opaque cores with depth writes, sorted at each page's farthest sprite, and their soft edges back to front across pages without depth writes, both among the park's other deferred primitives | Why: faint pixels in front of another person left square holes in that person; keep soft edges and depth testing against the park
#include "platform/opengl.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace tpw::spritefringe {
// The launcher enables this only for a real game. Differential tests keep the original path.
inline bool enabled = false;
enum class Pass { Original, Core, Fringe };
inline Pass pass = Pass::Original;
constexpr float kCoreAlpha = 1.0f;
constexpr uint32_t kCore = 0x10000000u, kFringe = 0x20000000u;

// Core batches sort among the park's deferred primitives at their farthest sprite, so each core is
// drawn before the soft edges in front of it. Called after every AddSprite on a core primitive with
// the primitive and sums from before the call (another primitive or none: a fresh one began).
template<class Primitive>
inline void KeepFarthest(Primitive* primitive, const Primitive* before, float depthBefore, int32_t countBefore) {
    static const void* current = nullptr;
    static float farthest = 0.0f;
    if (primitive == nullptr) return;
    if (primitive != before) { depthBefore = 0.0f; countBefore = 0; }
    const int32_t added = primitive->mVertexCount - countBefore;
    if (added <= 0) return;
    const float depth = (primitive->mDepth - depthBefore) / static_cast<float>(added);
    if (primitive != current || countBefore == 0) { current = primitive; farthest = depth; }
    else if (depth > farthest) farthest = depth;
    // ExecuteCurrentPrimitive divides the sum by the vertex count, which then gives the farthest depth.
    primitive->mDepth = farthest * static_cast<float>(primitive->mVertexCount);
}

template<class Iterator, class Depth>
inline void SortFringes(Iterator first, Iterator last, Depth depth) {
    std::stable_sort(first, last, [&](const auto& a, const auto& b) {
        const float da = depth(a), db = depth(b);
        // Invalid depths sort last, retaining insertion order, rather than breaking the comparator.
        if (std::isnan(da)) return false;
        if (std::isnan(db)) return true;
        return da > db;
    });
}

inline bool Eligible(uint32_t mode) {
    // Standard source-alpha sprites with alpha and depth tests, and depth writes.
    // Additive effects, shadows and sprites deliberately drawn without depth keep their own rules.
    return (mode & 0x8fffu) == 0x556u;
}

class Scope {
    Pass previous;
public:
    explicit Scope(Pass next) : previous(pass) { pass = next; }
    ~Scope() { pass = previous; }
};

// Called with the primitive's ordinary state already selected. SetBlendingMode gives an eligible mode the
// alpha test (greater than 0), the depth test and depth writes, so putting back the two values changed here
// keeps the GL state and the engine's cached blending mode in step. An attribute push for every sprite batch
// cost about 3 ms a frame with the whole park in view.
class DrawState {
    bool changed = false;
public:
    bool skip;
    // The fog's second pass covers the whole quad whatever its alpha, so it is drawn once, after the soft edges.
    bool secondPass = true;
    explicit DrawState(uint32_t mode) : skip(pass == Pass::Fringe && !Eligible(mode)) {
        if (pass == Pass::Original || !Eligible(mode)) return;
        changed = true;
        secondPass = pass == Pass::Fringe;
        glAlphaFunc(pass == Pass::Core ? GL_GEQUAL : 0x0201 /* GL_LESS */, kCoreAlpha);
        if (pass == Pass::Fringe) glDepthMask(0);
    }
    ~DrawState() {
        if (!changed) return;
        glAlphaFunc(GL_GREATER, 0.0f);
        if (secondPass) glDepthMask(1);  // the fringe pass turned depth writes off
    }
};
}  // namespace tpw::spritefringe
