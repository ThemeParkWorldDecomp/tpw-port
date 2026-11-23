// PortGroundPool.h: the sizes of the far ground's procedural textures (the port's fix groundpool, src/engine/tb_init.cpp and
// CHField::Prepare in src/engine/tb_hfield.cpp). When more of the procedural window is in view than the handler's ring of
// tiles holds, the nearest cells keep the ring's full size tiles and every other cell in view gets a tile of the pool, made
// at full size and shrunk to the size the cell covers on the screen: full, a half, a quarter or an eighth across, the
// smallest that still has a texel for each pixel. When the cells in view would need more than the pool's budget, all of them
// step down together. Header only and pure, so the host test (tests/host_ground_pool.cpp) sees what the game does.
#pragma once


// TPW_PORT_FIX groundpool PortGroundLevel | Mac: (none) | Port: the size of a far cell's procedural texture from the pixels it covers, and the step down that keeps the far cells' textures in the pool's budget | Why: the far ground had the mesh's plain texture past the ring's nearest cells (proccap), a visible circle; full size textures for the whole park would take about 256 MB

#include <cstdint>

namespace tpw {

// The pool's sizes: level 0 is the ring's size (CProcTextureHandler::mSize, 32 or 64), each level half the one before, down
// to 8 texels across.
inline constexpr int32_t kPortGroundLevels = 4;
inline constexpr int32_t kPortGroundSmallest = 8;
// The bytes all the pool's tiles may take together (the ring's own tiles not counted): 16 MB.
inline constexpr int64_t kPortGroundBudget = int64_t{16} << 20;
// The budget for a texture cache of `textureMemory` bytes (CTextureSettings::mTextureMemory): a quarter of the cache, so the
// pool's tiles and the ring's leave room for everything else on screen, and never more than kPortGroundBudget.
inline int64_t PortGroundBudgetFor(int64_t textureMemory) {
    const int64_t share = textureMemory / 4;
    return share > 0 && share < kPortGroundBudget ? share : kPortGroundBudget;
}
// The time the pool may spend making tiles in a frame, in milliseconds: at least one tile is made each frame, the rest wait.
inline constexpr double kPortGroundMillis = 3.0;
// A tile is made again larger only when the cell covers more than this many times its size in pixels (PortGroundRemake).
inline constexpr float kPortGroundSlack = 1.25f;

// The levels a ring size has (1 to kPortGroundLevels): 64 has 64, 32, 16 and 8; 16 has 16 and 8; below 8 only its own.
inline int32_t PortGroundLevelCount(int32_t full) {
    int32_t count = 1;
    while (count < kPortGroundLevels && (full >> count) >= kPortGroundSmallest) ++count;
    return count;
}

// The texels across of a level's tiles.
inline int32_t PortGroundSize(int32_t full, int32_t level) { return full >> level; }

// The bytes of one tile of a level at `bytesPerPixel`.
inline int64_t PortGroundBytes(int32_t full, int32_t level, int32_t bytesPerPixel) {
    const int64_t size = PortGroundSize(full, level);
    return size * size * bytesPerPixel;
}

// The pixels across a cell `cellWorld` wide covers at `distance` from the camera, with a projection whose cotangent of half
// the field of view is `cotHalfFov` on a picture `halfWidth` pixels wide from its centre to its side. A cell at or behind
// the camera covers the whole picture.
inline float PortGroundPixels(float cellWorld, float distance, float cotHalfFov, float halfWidth) {
    const float across = cellWorld * cotHalfFov * halfWidth;
    if (!(distance > 0.0f)) return across > 0.0f ? 1.0e9f : 0.0f;
    return across / distance;
}

// The level of a tile for a cell covering `pixels` across: the smallest size with at least as many texels as pixels, the full
// size when none is that small, the smallest when the cell covers less than it.
inline int32_t PortGroundLevel(float pixels, int32_t full) {
    const int32_t levels = PortGroundLevelCount(full);
    int32_t level = 0;
    while (level + 1 < levels && static_cast<float>(PortGroundSize(full, level + 1)) >= pixels) ++level;
    return level;
}

// How many levels every tile steps down so that the cells asking for each level (`counts[level]`, kPortGroundLevels of
// them) fit in `budget` bytes at `bytesPerPixel`: 0 when they fit as asked, else the fewest steps that fit, the smallest
// level when nothing fits.
inline int32_t PortGroundShift(const int32_t* counts, int32_t full, int32_t bytesPerPixel, int64_t budget) {
    const int32_t levels = PortGroundLevelCount(full);
    for (int32_t shift = 0; shift < levels; ++shift) {
        int64_t bytes = 0;
        for (int32_t level = 0; level < kPortGroundLevels; ++level) {
            int32_t at = level + shift;
            if (at > levels - 1) at = levels - 1;
            bytes += static_cast<int64_t>(counts[level]) * PortGroundBytes(full, at, bytesPerPixel);
        }
        if (bytes <= budget) return shift;
    }
    return levels - 1;
}

// The level after a step down of `shift`, never past the smallest.
inline int32_t PortGroundShifted(int32_t level, int32_t shift, int32_t full) {
    const int32_t at = level + shift;
    const int32_t last = PortGroundLevelCount(full) - 1;
    return at > last ? last : at;
}

// Whether a tile of `level` is made again for a cell covering `pixels` across when every tile steps down `shift` levels: only
// when it is smaller than the level asked for, and not within kPortGroundSlack of it (the step down counted), so a cell near
// the edge between two sizes is not made again at every small move of the camera. A larger tile than asked is kept.
inline bool PortGroundRemake(int32_t level, float pixels, int32_t shift, int32_t full) {
    const int32_t target = PortGroundShifted(PortGroundLevel(pixels, full), shift, full);
    if (level <= target) return false;
    if (level > target + 1) return true;
    return static_cast<float>(PortGroundSize(full, level) << shift) * kPortGroundSlack < pixels;
}

// Shrinks a tile of 4-byte pixels `full` across (rows of `full` pixels, each byte of a pixel on its own) to `level`: each
// pixel of `out` is the rounded mean of the block of pixels it covers, byte by byte. Level 0 copies.
inline void PortGroundShrink(const uint8_t* in, int32_t full, int32_t level, uint8_t* out) {
    const int32_t size = PortGroundSize(full, level);
    const int32_t step = full / size;
    const int32_t area = step * step;
    for (int32_t y = 0; y < size; ++y) {
        for (int32_t x = 0; x < size; ++x) {
            uint32_t sums[4] = {0, 0, 0, 0};
            for (int32_t dy = 0; dy < step; ++dy) {
                const uint8_t* row = in + (static_cast<int32_t>(y * step + dy) * full + x * step) * 4;
                for (int32_t dx = 0; dx < step; ++dx)
                    for (int32_t b = 0; b < 4; ++b) sums[b] += row[dx * 4 + b];
            }
            uint8_t* pixel = out + (y * size + x) * 4;
            for (int32_t b = 0; b < 4; ++b) pixel[b] = static_cast<uint8_t>((sums[b] + area / 2) / area);
        }
    }
}

}  // namespace tpw

