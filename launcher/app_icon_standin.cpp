// app_icon_standin.cpp: the launcher's stand-in icon (app_icon_standin.h), in the launcher's own colours (ui/theme.h).
#include "app_icon_standin.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "ui/theme.h"

namespace tpw::launcher {

using tpw::assets::IconImage;
using tpw::ui::Rgba;
namespace colour = tpw::ui::colour;

// Drawn with distance functions, so its edges are soft at any size.
IconImage StandInAppIcon() {
    constexpr int n = 256;
    IconImage out;
    out.width = out.height = n;
    out.bitDepth = 32;
    out.rgba.assign(static_cast<size_t>(n) * n * 4, 0);
    auto roundedBox = [](float x, float y, float half, float radius) {   // signed distance from the tile's edge, centred
        const float dx = std::fabs(x) - (half - radius), dy = std::fabs(y) - (half - radius);
        const float ox = std::max(dx, 0.0f), oy = std::max(dy, 0.0f);
        return std::sqrt(ox * ox + oy * oy) + std::min(std::max(dx, dy), 0.0f) - radius;
    };
    auto coverage = [](float distance) { return std::min(1.0f, std::max(0.0f, 0.5f - distance)); };   // 1 inside, 0 outside
    auto mix = [](Rgba under, Rgba over, float amount) {
        auto lerp = [amount](uint8_t a, uint8_t b) { return static_cast<uint8_t>(a + (b - a) * amount + 0.5f); };
        return Rgba{lerp(under.r, over.r), lerp(under.g, over.g), lerp(under.b, over.b), under.a};
    };
    const float half = 116, corner = 40, outline = 9;   // the tile's half size, corner radius and outline, in pixels
    for (int py = 0; py < n; ++py) {
        for (int px = 0; px < n; ++px) {
            const float x = px + 0.5f - n / 2.0f, y = py + 0.5f - n / 2.0f;
            const float edge = roundedBox(x, y, half, corner);
            const float alpha = coverage(edge);
            if (alpha <= 0) continue;
            // the sky, top to bottom
            const float t = std::min(1.0f, std::max(0.0f, (y + half) / (2 * half)));
            Rgba c = mix(colour::kSkyTop, colour::kSkyBottom, t);
            // the sun, top right, with its deeper rim
            const float sun = std::sqrt((x - 46) * (x - 46) + (y + 50) * (y + 50));
            c = mix(c, colour::kOrange, coverage(sun - 38));
            c = mix(c, colour::kYellow, coverage(sun - 30));
            // a cloud, left: three discs
            float cloud = std::min(std::sqrt((x + 52) * (x + 52) + (y + 6) * (y + 6)) - 22,
                                   std::min(std::sqrt((x + 24) * (x + 24) + (y + 16) * (y + 16)) - 26,
                                            std::sqrt((x + 2) * (x + 2) + (y + 4) * (y + 4)) - 20));
            c = mix(c, colour::kPaper, coverage(cloud) * 0.92f);
            // the lawn, rolling, with its deeper edge
            const float ground = y - (44 + 10 * std::sin((x + 60) / 48.0f));
            c = mix(c, colour::kLimeDeep, coverage(-ground - 0));
            c = mix(c, colour::kLime, coverage(-ground + 7));
            // the ink outline all round
            c = mix(c, colour::kInk, coverage(-edge - outline));
            uint8_t* p = &out.rgba[(static_cast<size_t>(py) * n + px) * 4];
            p[0] = c.r, p[1] = c.g, p[2] = c.b, p[3] = static_cast<uint8_t>(alpha * 255 + 0.5f);
        }
    }
    return out;
}

}  // namespace tpw::launcher
