// app_icon_standin.cpp: the launcher's stand-in icon (app_icon_standin.h), in the launcher's own colours (ui/theme.h).
#include "app_icon_standin.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "ui/theme.h"

namespace tpw::launcher {

using tpw::assets::IconImage;
using tpw::ui::Rgba;
namespace colour = tpw::ui::colour;

namespace {

// The lettering's own block capitals: each letter is a few strokes with round ends, in a box one unit high (y down) and
// `width` wide, drawn kStroke thick on each side of the line. Our own drawing, not a font and not the game's logo.
struct Stroke {
    float x0, y0, x1, y1;   // a line from (x0, y0) to (x1, y1); or, when bowl is set, the right half of a circle
    bool bowl = false;      // centred at (x0, y0) with radius x1
};
struct Glyph {
    float width;
    std::vector<Stroke> strokes;
};
constexpr float kStroke = 0.15f;   // half the stroke's thickness, in units of the letter's height

Glyph GlyphFor(char c) {
    switch (c) {
        case 'T': return {0.80f, {{0, 0, 0.80f, 0}, {0.40f, 0, 0.40f, 1}}};
        case 'H': return {0.72f, {{0, 0, 0, 1}, {0.72f, 0, 0.72f, 1}, {0, 0.5f, 0.72f, 0.5f}}};
        case 'E': return {0.56f, {{0, 0, 0, 1}, {0, 0, 0.56f, 0}, {0, 0.5f, 0.46f, 0.5f}, {0, 1, 0.56f, 1}}};
        case 'M': return {0.90f, {{0, 1, 0, 0}, {0, 0, 0.45f, 0.58f}, {0.45f, 0.58f, 0.90f, 0}, {0.90f, 0, 0.90f, 1}}};
        case 'P': return {0.62f, {{0, 0, 0, 1}, {0, 0, 0.34f, 0}, {0, 0.56f, 0.34f, 0.56f}, {0.34f, 0.28f, 0.28f, 0, true}}};
        case 'A': return {0.82f, {{0, 1, 0.41f, 0}, {0.41f, 0, 0.82f, 1}, {0.17f, 0.64f, 0.65f, 0.64f}}};
        case 'R': return {0.66f, {{0, 0, 0, 1}, {0, 0, 0.34f, 0}, {0, 0.56f, 0.34f, 0.56f}, {0.34f, 0.28f, 0.28f, 0, true},
                                  {0.30f, 0.56f, 0.66f, 1}}};
        case 'K': return {0.68f, {{0, 0, 0, 1}, {0.66f, 0, 0.04f, 0.60f}, {0.24f, 0.42f, 0.68f, 1}}};
        default: return {0.5f, {}};
    }
}

float SegmentDistance(float px, float py, float ax, float ay, float bx, float by) {
    const float dx = bx - ax, dy = by - ay;
    const float len2 = dx * dx + dy * dy;
    const float t = len2 > 0 ? std::min(1.0f, std::max(0.0f, ((px - ax) * dx + (py - ay) * dy) / len2)) : 0.0f;
    const float ex = px - (ax + t * dx), ey = py - (ay + t * dy);
    return std::sqrt(ex * ex + ey * ey);
}

float StrokeDistance(const Stroke& s, float x, float y) {
    if (!s.bowl) return SegmentDistance(x, y, s.x0, s.y0, s.x1, s.y1);
    const float dx = x - s.x0, dy = y - s.y0, r = s.x1;
    if (dx >= 0) return std::fabs(std::sqrt(dx * dx + dy * dy) - r);
    return std::min(std::sqrt(dx * dx + (dy + r) * (dy + r)), std::sqrt(dx * dx + (dy - r) * (dy - r)));
}

// A letter set on the icon: its glyph, its box's centre (pixels from the icon's centre), its height in pixels and its
// tilt.
struct PlacedLetter {
    Glyph glyph;
    float cx, cy, size, cosA, sinA;
};

// One line of letters, centred on x = 0 at height `y`, `size` pixels high. The line arches (its ends sit `arch` pixels
// lower) and the letters tilt left and right in turn, as fairground lettering bounces.
void SetLine(const char* text, float y, float size, float arch, float tiltDegrees, std::vector<PlacedLetter>& out) {
    constexpr float gap = 0.06f;
    std::vector<Glyph> glyphs;
    float total = 0;
    for (const char* c = text; *c; ++c) {
        glyphs.push_back(GlyphFor(*c));
        total += glyphs.back().width + 2 * kStroke + (c[1] ? gap : 0);
    }
    const float halfWidth = total * size / 2;
    float x = -total / 2;
    for (size_t i = 0; i < glyphs.size(); ++i) {
        const float w = glyphs[i].width + 2 * kStroke;
        const float cx = (x + w / 2) * size, along = cx / halfWidth;
        const float angle = (i % 2 ? -tiltDegrees : tiltDegrees) * 3.14159265f / 180;
        out.push_back({glyphs[i], cx, y + arch * along * along, size, std::cos(angle), std::sin(angle)});
        x += w + gap;
    }
}

}  // namespace

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
    // The words THEME over PARK, the second larger: a yellow-to-orange fill, a red rim and the ink outline, with a soft
    // shadow below and to the right.
    std::vector<PlacedLetter> letters;
    SetLine("THEME", -26, 33, 5, 4, letters);
    SetLine("PARK", 30, 46, 6, 5, letters);
    const float rim = 3.5f, ink = 4.5f;   // the red rim and the ink outline round the letters, in pixels
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
            const float sun = std::sqrt((x - 62) * (x - 62) + (y + 72) * (y + 72));
            c = mix(c, colour::kOrange, coverage(sun - 24));
            c = mix(c, colour::kYellow, coverage(sun - 18));
            // a cloud, top left: three discs
            const float cloud = std::min(std::sqrt((x + 72) * (x + 72) + (y + 70) * (y + 70)) - 13,
                                         std::min(std::sqrt((x + 54) * (x + 54) + (y + 77) * (y + 77)) - 16,
                                                  std::sqrt((x + 37) * (x + 37) + (y + 69) * (y + 69)) - 12));
            c = mix(c, colour::kPaper, coverage(cloud) * 0.92f);
            // the lawn, rolling, with its deeper edge
            const float ground = y - (70 + 7 * std::sin((x + 60) / 40.0f));
            c = mix(c, colour::kLimeDeep, coverage(-ground - 0));
            c = mix(c, colour::kLime, coverage(-ground + 6));
            // the lettering: the distance from the nearest stroke's edge in pixels (negative inside), the same for the
            // shadow, and how far down its letter the nearest stroke's point is (0 top, 1 bottom) for the fill's shading
            float word = 1e9f, shadow = 1e9f, depth = 0;
            for (const PlacedLetter& l : letters) {
                auto local = [&l](float ax, float ay, float& lx, float& ly) {   // a point in the letter's own units
                    const float rx = ax - l.cx, ry = ay - l.cy;
                    lx = (rx * l.cosA + ry * l.sinA) / l.size + l.glyph.width / 2;
                    ly = (-rx * l.sinA + ry * l.cosA) / l.size + 0.5f;
                };
                float lx, ly, sx, sy;
                local(x, y, lx, ly);
                local(x - 3, y - 4, sx, sy);
                for (const Stroke& s : l.glyph.strokes) {
                    const float d = (StrokeDistance(s, lx, ly) - kStroke) * l.size;
                    if (d < word) word = d, depth = std::min(1.0f, std::max(0.0f, ly));
                    shadow = std::min(shadow, (StrokeDistance(s, sx, sy) - kStroke) * l.size);
                }
            }
            c = mix(c, colour::kInk, std::min(1.0f, std::max(0.0f, (rim + ink + 3 - shadow) / 6)) * 0.35f);
            c = mix(c, colour::kInk, coverage(word - rim - ink));
            c = mix(c, colour::kRed, coverage(word - rim));
            const Rgba fill = depth < 0.5f ? mix(colour::kYellowSoft, colour::kYellow, depth * 2)
                                           : mix(colour::kYellow, colour::kOrange, (depth - 0.5f) * 2);
            c = mix(c, fill, coverage(word));
            // the ink outline all round
            c = mix(c, colour::kInk, coverage(-edge - outline));
            uint8_t* p = &out.rgba[(static_cast<size_t>(py) * n + px) * 4];
            p[0] = c.r, p[1] = c.g, p[2] = c.b, p[3] = static_cast<uint8_t>(alpha * 255 + 0.5f);
        }
    }
    return out;
}

}  // namespace tpw::launcher
