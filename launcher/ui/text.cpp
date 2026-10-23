// text.cpp: the launcher screen's text through stb_truetype (text.h). This is the one file that compiles stb's
// implementations (third_party/stb/NOTE.md).
#include "text.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#define STB_RECT_PACK_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#include "stb_rect_pack.h"
#include "stb_truetype.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// The font files as byte arrays, written into the build folder at configure time (CMakeLists.txt, ui_fonts.cpp).
namespace tpw::ui::fontdata {
extern const unsigned char kFredokaBold[];
extern const size_t kFredokaBoldSize;
extern const unsigned char kFredokaSemiBold[];
extern const size_t kFredokaSemiBoldSize;
extern const unsigned char kNunitoRegular[];
extern const size_t kNunitoRegularSize;
extern const unsigned char kNunitoBold[];
extern const size_t kNunitoBoldSize;
}  // namespace tpw::ui::fontdata

namespace tpw::ui {

namespace {
// The characters every atlas holds: printable ASCII, Latin-1, and the punctuation the launcher's texts or a disc's file
// names may use. Glyphs a font lacks, and every other character, draw as '?'.
constexpr int kAsciiFirst = 32, kAsciiCount = 95;
constexpr int kLatinFirst = 160, kLatinCount = 96;
constexpr int kExtras[] = {0x2013, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2026};
constexpr int kExtraCount = sizeof kExtras / sizeof kExtras[0];
constexpr int kGlyphCount = kAsciiCount + kLatinCount + kExtraCount;

int SlotOf(uint32_t cp) {
    if (cp >= static_cast<uint32_t>(kAsciiFirst) && cp < static_cast<uint32_t>(kAsciiFirst + kAsciiCount))
        return static_cast<int>(cp) - kAsciiFirst;
    if (cp >= static_cast<uint32_t>(kLatinFirst) && cp < static_cast<uint32_t>(kLatinFirst + kLatinCount))
        return kAsciiCount + static_cast<int>(cp) - kLatinFirst;
    for (int i = 0; i < kExtraCount; ++i)
        if (static_cast<uint32_t>(kExtras[i]) == cp) return kAsciiCount + kLatinCount + i;
    return -1;
}

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xc0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xe0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (cp & 0x3f));
    } else {
        out += static_cast<char>(0xf0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (cp & 0x3f));
    }
}

std::string Encode(const std::vector<uint32_t>& cps, size_t from, size_t to) {
    std::string out;
    for (size_t i = from; i < to; ++i) AppendUtf8(out, cps[i]);
    return out;
}
}  // namespace

struct Text::Font {
    stbtt_fontinfo info{};
    const unsigned char* data = nullptr;
    int ascent = 0, descent = 0, lineGap = 0;
    bool has[kGlyphCount] = {};
};

struct Text::Atlas {
    Font* font = nullptr;
    float scale = 1;        // canvas pixels per point when it was made
    float fontScale = 0;    // font units to pixels
    float ascentPx = 0, lineHeightPx = 0;
    int width = 0, height = 0;
    SDL_Texture* texture = nullptr;
    stbtt_packedchar chars[kGlyphCount] = {};
    int glyph[kGlyphCount] = {};   // glyph index of each slot (0 where the font lacks it)
    ~Atlas() {
        if (texture) SDL_DestroyTexture(texture);
    }
    int Slot(uint32_t cp) const {
        int slot = SlotOf(cp);
        if (slot < 0 || !font->has[slot]) slot = '?' - kAsciiFirst;
        return slot;
    }
    float Kern(int a, int b) const {
        if (a < 0) return 0;
        return stbtt_GetGlyphKernAdvance(&font->info, glyph[a], glyph[b]) * fontScale;
    }
};

Text::Text(Canvas& canvas) : canvas_(canvas) {
    const unsigned char* data[4] = {fontdata::kFredokaBold, fontdata::kFredokaSemiBold, fontdata::kNunitoRegular,
                                    fontdata::kNunitoBold};
    const char* names[4] = {"Fredoka Bold", "Fredoka SemiBold", "Nunito Regular", "Nunito Bold"};
    for (int i = 0; i < 4; ++i) {
        auto font = std::make_unique<Font>();
        font->data = data[i];
        if (!stbtt_InitFont(&font->info, data[i], stbtt_GetFontOffsetForIndex(data[i], 0))) {
            error_ = std::string("cannot read the font ") + names[i];
            continue;
        }
        stbtt_GetFontVMetrics(&font->info, &font->ascent, &font->descent, &font->lineGap);
        for (int slot = 0; slot < kGlyphCount; ++slot) {
            int cp = slot < kAsciiCount ? kAsciiFirst + slot
                     : slot < kAsciiCount + kLatinCount ? kLatinFirst + slot - kAsciiCount
                                                         : kExtras[slot - kAsciiCount - kLatinCount];
            font->has[slot] = stbtt_FindGlyphIndex(&font->info, cp) != 0;
        }
        fonts_[i] = std::move(font);
    }
}

Text::~Text() = default;

std::string Text::FontVersion(Face face) const {
    const Font* font = fonts_[static_cast<int>(face)].get();
    if (!font) return "";
    int length = 0;
    const char* name = stbtt_GetFontNameString(&font->info, &length, STBTT_PLATFORM_ID_MICROSOFT,
                                               STBTT_MS_EID_UNICODE_BMP, STBTT_MS_LANG_ENGLISH, 5);
    std::string out;
    if (name) {
        for (int i = 0; i + 1 < length; i += 2) {   // UTF-16, big-endian; a version string is ASCII
            unsigned hi = static_cast<unsigned char>(name[i]), lo = static_cast<unsigned char>(name[i + 1]);
            if (hi == 0 && lo >= 0x20 && lo < 0x7f) out += static_cast<char>(lo);
        }
    } else if ((name = stbtt_GetFontNameString(&font->info, &length, STBTT_PLATFORM_ID_MAC, STBTT_MAC_EID_ROMAN,
                                               STBTT_MAC_LANG_ENGLISH, 5))) {
        out.assign(name, static_cast<size_t>(length));
    }
    const std::string prefix = "Version ";
    if (out.compare(0, prefix.size(), prefix) == 0) out.erase(0, prefix.size());
    size_t end = out.find_first_of("; ");
    if (end != std::string::npos) out.erase(end);
    return out;
}

Text::Atlas* Text::Get(Style s) {
    int face = static_cast<int>(s.face);
    if (!fonts_[face]) return nullptr;
    float scale = canvas_.Scale();
    int px = std::max(4, static_cast<int>(std::lround(s.size * scale)));
    auto key = std::make_pair(face, px);
    auto it = atlases_.find(key);
    if (it != atlases_.end()) {
        if (it->second->texture) return it->second.get();
        return nullptr;
    }
    auto atlas = std::make_unique<Atlas>();
    Atlas* a = atlas.get();
    atlases_[key] = std::move(atlas);
    Font* font = fonts_[face].get();
    a->font = font;
    a->scale = scale;
    a->fontScale = stbtt_ScaleForMappingEmToPixels(&font->info, static_cast<float>(px));
    a->ascentPx = font->ascent * a->fontScale;
    a->lineHeightPx = (font->ascent - font->descent + font->lineGap) * a->fontScale;

    stbtt_pack_range ranges[3] = {};
    ranges[0].font_size = STBTT_POINT_SIZE(static_cast<float>(px));
    ranges[0].first_unicode_codepoint_in_range = kAsciiFirst;
    ranges[0].num_chars = kAsciiCount;
    ranges[0].chardata_for_range = a->chars;
    ranges[1].font_size = ranges[0].font_size;
    ranges[1].first_unicode_codepoint_in_range = kLatinFirst;
    ranges[1].num_chars = kLatinCount;
    ranges[1].chardata_for_range = a->chars + kAsciiCount;
    int extras[kExtraCount];
    std::copy(std::begin(kExtras), std::end(kExtras), extras);
    ranges[2].font_size = ranges[0].font_size;
    ranges[2].array_of_unicode_codepoints = extras;
    ranges[2].num_chars = kExtraCount;
    ranges[2].chardata_for_range = a->chars + kAsciiCount + kLatinCount;

    std::vector<unsigned char> alpha;
    int w = 256, h = 256;
    bool packed = false;
    while (!packed && w <= 4096) {
        alpha.assign(static_cast<size_t>(w) * h, 0);
        stbtt_pack_context pc;
        if (stbtt_PackBegin(&pc, alpha.data(), w, h, 0, 1, nullptr)) {
            stbtt_PackSetOversampling(&pc, 1, 1);
            packed = stbtt_PackFontRanges(&pc, font->data, 0, ranges, 3) != 0;
            stbtt_PackEnd(&pc);
        }
        if (!packed) {
            if (w == h) w *= 2;
            else h *= 2;
        }
    }
    if (!packed) {
        error_ = "a glyph atlas does not fit 4096 x 4096";
        return nullptr;
    }
    for (int slot = 0; slot < kGlyphCount; ++slot) {
        int cp = slot < kAsciiCount ? kAsciiFirst + slot
                 : slot < kAsciiCount + kLatinCount ? kLatinFirst + slot - kAsciiCount
                                                     : kExtras[slot - kAsciiCount - kLatinCount];
        a->glyph[slot] = stbtt_FindGlyphIndex(&font->info, cp);
    }
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < alpha.size(); ++i) {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = alpha[i];
    }
    a->width = w;
    a->height = h;
    a->texture = SDL_CreateTexture(canvas_.Renderer(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!a->texture) {
        error_ = std::string("cannot make a glyph texture: ") + SDL_GetError();
        return nullptr;
    }
    SDL_UpdateTexture(a->texture, nullptr, rgba.data(), w * 4);
    SDL_SetTextureBlendMode(a->texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(a->texture, SDL_SCALEMODE_NEAREST);
    return a;
}

std::vector<uint32_t> Text::Decode(std::string_view s) const {
    std::vector<uint32_t> out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0xfffd;
        size_t len = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xe0) == 0xc0) {
            len = 2;
        } else if ((c & 0xf0) == 0xe0) {
            len = 3;
        } else if ((c & 0xf8) == 0xf0) {
            len = 4;
        }
        if (len > 1) {
            if (i + len > s.size()) {
                len = 1;
            } else {
                cp = c & (0x7f >> len);
                for (size_t k = 1; k < len; ++k) {
                    unsigned char d = static_cast<unsigned char>(s[i + k]);
                    if ((d & 0xc0) != 0x80) {
                        cp = 0xfffd;
                        len = k;
                        break;
                    }
                    cp = (cp << 6) | (d & 0x3f);
                }
            }
        }
        out.push_back(cp);
        i += len;
    }
    return out;
}

float Text::LineHeight(Style s) {
    Atlas* a = Get(s);
    return a ? a->lineHeightPx / a->scale : s.size * 1.3f;
}

float Text::Ascent(Style s) {
    Atlas* a = Get(s);
    return a ? a->ascentPx / a->scale : s.size;
}

float Text::Width(Style s, std::string_view utf8) {
    Atlas* a = Get(s);
    if (!a) return 0;
    float x = 0;
    int prev = -1;
    for (uint32_t cp : Decode(utf8)) {
        int slot = a->Slot(cp);
        x += a->Kern(prev, slot) + a->chars[slot].xadvance;
        prev = slot;
    }
    return x / a->scale;
}

float Text::Draw(Style s, float x, float y, std::string_view utf8, Rgba colour) {
    Atlas* a = Get(s);
    if (!a) return 0;
    float scale = a->scale;
    float startPx = std::round(x * scale);
    float xp = startPx;
    float yp = std::round(y * scale + a->ascentPx);
    SDL_FColor fc = ToFColor(colour);
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    int prev = -1;
    for (uint32_t cp : Decode(utf8)) {
        int slot = a->Slot(cp);
        xp += a->Kern(prev, slot);
        prev = slot;
        stbtt_aligned_quad q;
        float yq = yp;
        stbtt_GetPackedQuad(a->chars, a->width, a->height, slot, &xp, &yq, &q, 1);
        if (q.x1 <= q.x0 || q.y1 <= q.y0) continue;
        int base = static_cast<int>(v.size());
        v.push_back({{q.x0 / scale, q.y0 / scale}, fc, {q.s0, q.t0}});
        v.push_back({{q.x1 / scale, q.y0 / scale}, fc, {q.s1, q.t0}});
        v.push_back({{q.x1 / scale, q.y1 / scale}, fc, {q.s1, q.t1}});
        v.push_back({{q.x0 / scale, q.y1 / scale}, fc, {q.s0, q.t1}});
        idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    if (!idx.empty())
        SDL_RenderGeometry(canvas_.Renderer(), a->texture, v.data(), static_cast<int>(v.size()), idx.data(),
                           static_cast<int>(idx.size()));
    return (xp - startPx) / scale;
}

void Text::DrawCentred(Style s, const Rect& box, std::string_view utf8, Rgba colour) {
    float w = Width(s, utf8);
    Draw(s, box.CentreX() - w * 0.5f, box.CentreY() - LineHeight(s) * 0.5f, utf8, colour);
}

void Text::DrawOutlined(Style s, float x, float y, std::string_view utf8, Rgba colour, Rgba line, float outline) {
    const int kSteps = 12;
    for (int i = 0; i < kSteps; ++i) {
        float a = 6.2831853f * i / kSteps;
        Draw(s, x + std::cos(a) * outline, y + std::sin(a) * outline, utf8, line);
    }
    Draw(s, x, y, utf8, colour);
}

std::vector<std::string> Text::Wrap(Style s, std::string_view utf8, float width) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= utf8.size()) {
        size_t nl = utf8.find('\n', start);
        std::string_view para = utf8.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
        std::string line;
        size_t i = 0;
        while (i < para.size()) {
            size_t sp = para.find(' ', i);
            std::string_view word = para.substr(i, sp == std::string_view::npos ? std::string_view::npos : sp - i);
            std::string candidate = line.empty() ? std::string(word) : line + " " + std::string(word);
            if (line.empty() || Width(s, candidate) <= width) {
                line = candidate;
            } else {
                lines.push_back(line);
                line = std::string(word);
            }
            // A single word wider than the line is cut where it no longer fits.
            while (Width(s, line) > width) {
                std::vector<uint32_t> cps = Decode(line);
                size_t keep = cps.size();
                while (keep > 1 && Width(s, Encode(cps, 0, keep)) > width) --keep;
                lines.push_back(Encode(cps, 0, keep));
                line = Encode(cps, keep, cps.size());
            }
            if (sp == std::string_view::npos) break;
            i = sp + 1;
        }
        lines.push_back(line);
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
    return lines;
}

float Text::DrawWrapped(Style s, float x, float y, float width, std::string_view utf8, Rgba colour, float lineGap) {
    float lh = LineHeight(s) + lineGap;
    float top = y;
    for (const std::string& line : Wrap(s, utf8, width)) {
        Draw(s, x, y, line, colour);
        y += lh;
    }
    return y - top;
}

std::string Text::Fit(Style s, std::string_view utf8, float width, bool keepEnd) {
    if (Width(s, utf8) <= width) return std::string(utf8);
    Atlas* a = Get(s);
    if (!a) return std::string(utf8);
    std::vector<uint32_t> cps = Decode(utf8);
    std::vector<float> adv(cps.size());
    for (size_t i = 0; i < cps.size(); ++i) adv[i] = a->chars[a->Slot(cps[i])].xadvance / a->scale;
    float budget = width - Width(s, "...");
    if (budget <= 0) return "...";
    size_t head = 0, tail = cps.size();
    float used = 0;
    if (keepEnd) {
        // The end of a path names the file: give it up to half, then fill the rest from the start.
        while (tail > 0 && used + adv[tail - 1] <= budget * 0.5f) used += adv[--tail];
    }
    while (head < tail && used + adv[head] <= budget) used += adv[head++];
    if (keepEnd) {
        while (tail > head && used + adv[tail - 1] <= budget) used += adv[--tail];
        return Encode(cps, 0, head) + "..." + Encode(cps, tail, cps.size());
    }
    return Encode(cps, 0, head) + "...";
}

}  // namespace tpw::ui
