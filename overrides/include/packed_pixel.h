// packed_pixel.h: a packed pixel of 1 to 4 bytes read without touching a byte past it (CMaskConverter in
// src/bullfrog/TbRaConvert.cpp). Header only and pure, so the host test (platform/tests/pixel_bounds_tests.cpp) sees what
// the game does.
#pragma once
// TPW_PORT_FIX pixelbounds tpw::ReadPackedPixel | Mac: CMaskConverter reads every source pixel as a whole 32 bit word, 1 to 3 bytes past a narrower pixel | Port: only the pixel's own bytes are read, the missing high bytes 0 | Why: the last pixel of a line can end at the last byte of a mapped page, and on Android the word read past it crashed; the bytes past the pixel are masked off anyway; contributed by mario910 (tpw-port PR #135)
#include <algorithm>
#include <cstdint>

namespace tpw {
inline uint32_t ReadPackedPixel(const uint8_t* source, uint32_t bytes) {
    uint32_t pixel = 0;
    for (uint32_t i = 0; i < std::min(bytes, 4u); ++i) {
        pixel |= uint32_t(source[i]) << (i * 8);
    }
    return pixel;
}
}  // namespace tpw
