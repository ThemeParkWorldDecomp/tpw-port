#pragma once

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
}
