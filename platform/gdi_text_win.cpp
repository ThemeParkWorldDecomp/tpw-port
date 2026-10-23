// gdi_text_win.cpp: gdi_text.h on Windows, with stb_truetype (third_party/stb) where the Mac's gdi_text.cpp has CoreText,
// which CMakeLists.txt leaves out of the Windows build. Fonts are found among the files AddFontResource registered, then
// among the system's font files, and text is laid out without kerning or ligatures, as on the Mac.
//
// What differs from the CoreText side: the system fonts are the Windows font folder read on the first miss; the fallback
// for an unknown name is Arial (Helvetica's metric twin) or the first face found; and a pixel is ink when its glyph
// coverage reaches one half, where CoreGraphics without antialiasing tests the pixel's centre.
#ifdef _WIN32
#include "gdi_text.h"
#include "resource_fork.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace tpw {
void Log(const char* format, ...) __attribute__((format(printf, 1, 2)));   // platform.h
}

namespace tpw {
namespace gdi_text {

namespace {

std::string Lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// Mac Roman 0x80..0xff as Unicode (the game's text is Mac Roman bytes).
const uint16_t kMacRomanHigh[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
    0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3, 0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
    0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF, 0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211, 0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
    0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB, 0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1, 0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
    0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC, 0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7,
};

int Codepoint(char byte) {
    const auto b = static_cast<unsigned char>(byte);
    return b < 0x80 ? b : kMacRomanHigh[b - 0x80];
}

// ---- Font files -----------------------------------------------------------------------------

struct Face {
    std::string family, fullName, postScript;
    bool bold = false, italic = false;
    std::shared_ptr<std::vector<uint8_t>> bytes;   // the sfnt (or collection) the face is in, shared by its faces
    int offset = 0;                                // of the face's table directory in bytes
};

// A name record of the face as UTF-8: the Windows Unicode one (US English) when there is one, else the Mac Roman one.
std::string NameOf(const stbtt_fontinfo& info, int nameId) {
    int length = 0;
    if (const char* utf16 = stbtt_GetFontNameString(&info, &length, STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP,
                                                    STBTT_MS_LANG_ENGLISH, nameId)) {
        std::string out;
        for (int i = 0; i + 1 < length; i += 2) {
            const unsigned c = static_cast<unsigned char>(utf16[i]) << 8 | static_cast<unsigned char>(utf16[i + 1]);
            if (c < 0x80) {
                out += static_cast<char>(c);
            } else if (c < 0x800) {
                out += static_cast<char>(0xc0 | c >> 6);
                out += static_cast<char>(0x80 | (c & 0x3f));
            } else {
                out += static_cast<char>(0xe0 | c >> 12);
                out += static_cast<char>(0x80 | (c >> 6 & 0x3f));
                out += static_cast<char>(0x80 | (c & 0x3f));
            }
        }
        return out;
    }
    if (const char* roman = stbtt_GetFontNameString(&info, &length, STBTT_PLATFORM_ID_MAC, STBTT_MAC_EID_ROMAN,
                                                    STBTT_MAC_LANG_ENGLISH, nameId)) {
        return std::string(roman, static_cast<size_t>(length));
    }
    return std::string();
}

uint16_t ReadU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }

// The face's 'head' macStyle bits: 1 bold, 2 italic.
uint16_t MacStyle(const stbtt_fontinfo& info) {
    const stbtt_uint32 head = stbtt__find_table(info.data, static_cast<stbtt_uint32>(info.fontstart), "head");
    return head != 0 ? ReadU16(info.data + head + 44) : 0;
}

// Every face of every font file added so far, in the order added; the files by host path.
std::vector<Face>& Faces() {
    static auto* faces = new std::vector<Face>;
    return *faces;
}

std::map<std::string, int32_t>& AddedFiles() {
    static auto* files = new std::map<std::string, int32_t>;
    return *files;
}

// The 92 characters of the Mac shim's reference string (0x100125c4); 0xa3 is the pound sign.
const char kReference[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@\xa3$%^&*()_+-=[]{}()|;':?/<>,.";

}  // namespace

void LogOnce(const std::string& key, const std::string& message) {
    static auto* seen = new std::set<std::string>;
    if (seen->insert(key).second) Log("gdi: %s", message.c_str());
}

namespace {

// The faces of an sfnt or a collection held in bytes, appended to faces; how many. what says where they came from, for
// the log (empty: not logged, as for the system's fonts).
int32_t AddFaces(std::vector<Face>& faces, std::shared_ptr<std::vector<uint8_t>> bytes, const std::string& hostPath,
                 const std::string& what, bool log) {
    int32_t count = 0;
    const int n = std::max(1, stbtt_GetNumberOfFonts(bytes->data()));
    for (int i = 0; i < n; ++i) {
        const int offset = stbtt_GetFontOffsetForIndex(bytes->data(), i);
        stbtt_fontinfo info;
        if (offset < 0 || !stbtt_InitFont(&info, bytes->data(), offset)) continue;
        Face face;
        face.family = NameOf(info, 1);
        face.fullName = NameOf(info, 4);
        face.postScript = NameOf(info, 6);
        const uint16_t style = MacStyle(info);
        face.bold = (style & 1) != 0;
        face.italic = (style & 2) != 0;
        face.bytes = bytes;
        face.offset = offset;
        if (log) {
            Log("gdi: font file %s%s: family \"%s\", name \"%s\", PostScript \"%s\"", hostPath.c_str(), what.c_str(),
                face.family.c_str(), face.fullName.c_str(), face.postScript.c_str());
        }
        faces.push_back(std::move(face));
        ++count;
    }
    return count;
}

// Reads up to limit bytes of the file; false when it cannot be opened or is longer than limit.
bool ReadFileBytes(const std::filesystem::path& path, std::vector<uint8_t>& bytes, size_t limit) {
    std::error_code ec;
    const uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec || size > limit) return false;
#ifdef _WIN32
    std::FILE* file = _wfopen(path.c_str(), L"rb");
#else
    std::FILE* file = std::fopen(path.c_str(), "rb");
#endif
    if (file == nullptr) return false;
    bytes.resize(static_cast<size_t>(size));
    const size_t got = bytes.empty() ? 0 : std::fread(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    bytes.resize(got);
    return true;
}

// True when the first four bytes are an sfnt magic: 00 01 00 00, 'true', 'OTTO' or 'ttcf'.
bool LooksLikeSfnt(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 4) return false;
    const uint32_t magic = static_cast<uint32_t>(bytes[0]) << 24 | static_cast<uint32_t>(bytes[1]) << 16 |
                           static_cast<uint32_t>(bytes[2]) << 8 | bytes[3];
    return magic == 0x00010000 || magic == ForkType("true") || magic == ForkType("OTTO") || magic == ForkType("ttcf");
}

// Windows' own fonts, read once, on the first name the game's files do not have.
const std::vector<Face>& SystemFaces() {
    static auto* faces = [] {
        auto* found = new std::vector<Face>;
        const char* windows = std::getenv("WINDIR");
        const std::filesystem::path folders[] = {std::filesystem::path(windows != nullptr ? windows : "C:\\Windows") / "Fonts"};
        for (const auto& folder : folders) {
            std::error_code ec;
            for (auto it = std::filesystem::recursive_directory_iterator(folder, ec);
                 !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                const std::string ext = Lower(it->path().extension().string());
                if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") continue;
                auto bytes = std::make_shared<std::vector<uint8_t>>();
                if (ReadFileBytes(it->path(), *bytes, 64u << 20) && LooksLikeSfnt(*bytes))
                    AddFaces(*found, bytes, it->path().string(), "", false);
            }
        }
        return found;
    }();
    return *faces;
}

// The face of the name: an exact full or PostScript name first, then a family's regular face (its bold one when it
// has no other), so a file named like a system font is never beaten by the system's.
const Face* FindFace(const std::vector<Face>& faces, const std::string& wanted, bool bold, bool italic) {
    for (const Face& face : faces) {
        if (Lower(face.fullName) == wanted || Lower(face.postScript) == wanted) return &face;
    }
    const Face* match = nullptr;
    int best = -1;
    for (const Face& face : faces) {
        if (Lower(face.family) != wanted) continue;
        const int score = (face.bold == bold ? 2 : 0) + (face.italic == italic ? 1 : 0);
        if (score > best) {
            match = &face;
            best = score;
        }
    }
    return match;
}

}  // namespace

int32_t AddFontFile(const std::string& hostPath) {
    auto& added = AddedFiles();
    const auto done = added.find(hostPath);
    if (done != added.end()) return done->second;
    const std::filesystem::path path = std::filesystem::u8path(hostPath);
    auto bytes = std::make_shared<std::vector<uint8_t>>();
    ReadFileBytes(path, *bytes, 64u << 20);
    int32_t faces = 0;
    // The data disc's fonts are Macintosh suitcases: an empty data fork and the sfnt in the resource fork, which the
    // extractor wrote beside the file as "<name>.rsrc".
    std::error_code exists;
    std::filesystem::path forkPath = path;
    forkPath += ".rsrc";
    if (!LooksLikeSfnt(*bytes) && std::filesystem::is_regular_file(forkPath, exists)) {
        std::vector<uint8_t> fork;
        if (ReadFileBytes(forkPath, fork, 64u << 20)) {
            for (const ForkResource& resource : FindResources(fork.data(), fork.size(), ForkType("sfnt"))) {
                auto data = std::make_shared<std::vector<uint8_t>>(resource.data, resource.data + resource.size);
                const int32_t got = AddFaces(Faces(), data, hostPath,
                                             " (sfnt " + std::to_string(resource.id) + " of its resource fork)", true);
                if (got == 0) {
                    LogOnce("badsfnt:" + hostPath, "AddFontResource: sfnt resource " + std::to_string(resource.id) + " of " +
                                                       hostPath + " is not a font stb_truetype can read");
                }
                faces += got;
            }
        }
        if (faces == 0) LogOnce("nofaces:" + hostPath, "AddFontResource: no sfnt resource in " + forkPath.string());
        added[hostPath] = faces;
        return faces;
    }
    if (LooksLikeSfnt(*bytes)) faces = AddFaces(Faces(), bytes, hostPath, "", true);
    if (faces == 0) LogOnce("nofaces:" + hostPath, "AddFontResource: no faces in " + hostPath);
    added[hostPath] = faces;
    return faces;
}

// ---- Fonts ----------------------------------------------------------------------------------

struct Font::Impl {
    Face face;
    stbtt_fontinfo info;
    float scale = 0;      // font units to pixels at the size
    bool underline = false;
    int32_t ascent = 0;
    int32_t avgWidth = 0;
};

Font::Font() : mImpl(new Impl) {}
Font::~Font() = default;

std::unique_ptr<Font> Font::Create(const Style& style) {
    const int32_t height = style.height < 0 ? -style.height : style.height;
    const float size = height == 0 ? 12.0f : static_cast<float>(height);   // TextSize(0) is the default size
    const std::string wanted = Lower(style.face);
    const bool bold = style.weight > 699, italic = style.italic;   // lfWeight > 699 and lfItalic, as on the Mac
    const Face* face = FindFace(Faces(), wanted, bold, italic);
    if (face == nullptr) face = FindFace(SystemFaces(), wanted, bold, italic);
    if (face == nullptr) {
        face = FindFace(SystemFaces(), "arial", bold, italic);
        if (face == nullptr && !SystemFaces().empty()) face = &SystemFaces().front();
        if (face == nullptr && !Faces().empty()) face = &Faces().front();
        LogOnce("face:" + wanted, "no font named \"" + style.face + "\", using " +
                                      (face != nullptr ? "\"" + face->family + "\"" : std::string("nothing")));
    }
    if (face == nullptr) return nullptr;
    if ((bold && !face->bold) || (italic && !face->italic))
        LogOnce("style:" + wanted, "\"" + style.face + "\" has no bold or italic variant, drawn regular");
    std::unique_ptr<Font> font(new Font);
    Impl& impl = *font->mImpl;
    impl.face = *face;
    if (!stbtt_InitFont(&impl.info, impl.face.bytes->data(), impl.face.offset)) return nullptr;
    impl.scale = stbtt_ScaleForMappingEmToPixels(&impl.info, size);
    impl.underline = style.underline;
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&impl.info, &ascent, &descent, &gap);
    impl.ascent = static_cast<int32_t>(std::ceil(ascent * impl.scale));
    impl.avgWidth = static_cast<int32_t>(font->Advance(kReference, 92)) / 92;
    return font;
}

int32_t Font::Ascent() const { return mImpl->ascent; }
int32_t Font::AverageWidth() const { return mImpl->avgWidth; }
std::string Font::FamilyName() const { return mImpl->face.family; }

double Font::Advance(const char* text, int32_t length) const {
    if (text == nullptr || length <= 0) return 0;
    double width = 0;
    for (int32_t i = 0; i < length; ++i) {
        int advance = 0, bearing = 0;
        stbtt_GetCodepointHMetrics(&mImpl->info, Codepoint(text[i]), &advance, &bearing);
        width += advance * static_cast<double>(mImpl->scale);
    }
    return width;
}

void Font::Draw(uint8_t* ink, int32_t width, int32_t height, int32_t x, int32_t baseline, double scale, const char* text,
                int32_t length) const {
    if (text == nullptr || length <= 0 || width <= 0 || height <= 0) return;
    const float sx = static_cast<float>(mImpl->scale * scale), sy = mImpl->scale;
    std::vector<uint8_t> coverage;
    double pen = x;   // the horizontal scale stretches the advances too
    for (int32_t i = 0; i < length; ++i) {
        const int codepoint = Codepoint(text[i]);
        const int whole = static_cast<int>(std::floor(pen));
        const float shift = static_cast<float>(pen - whole);
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBoxSubpixel(&mImpl->info, codepoint, sx, sy, shift, 0, &x0, &y0, &x1, &y1);
        const int64_t w = static_cast<int64_t>(x1) - x0, h = static_cast<int64_t>(y1) - y0;
        // A glyph far larger than the image (a nonsense size) is skipped rather than rasterised whole.
        const bool huge = !std::isfinite(sx) || !std::isfinite(sy) || w * h > int64_t(64) << 20;
        if (huge) LogOnce("hugeglyph", "a glyph " + std::to_string(w) + " x " + std::to_string(h) + " pixels is not drawn");
        if (w > 0 && h > 0 && !huge) {
            coverage.assign(static_cast<size_t>(w) * h, 0);
            stbtt_MakeCodepointBitmapSubpixel(&mImpl->info, coverage.data(), w, h, w, sx, sy, shift, 0, codepoint);
            for (int row = 0; row < h; ++row) {
                const int py = baseline + y0 + row;
                if (py < 0 || py >= height) continue;
                for (int col = 0; col < w; ++col) {
                    const int px = whole + x0 + col;
                    if (px < 0 || px >= width) continue;
                    if (coverage[static_cast<size_t>(row) * w + col] >= 128) ink[static_cast<size_t>(py) * width + px] = 0xff;
                }
            }
        }
        int advance = 0, bearing = 0;
        stbtt_GetCodepointHMetrics(&mImpl->info, codepoint, &advance, &bearing);
        pen += advance * static_cast<double>(sx);
    }
    if (mImpl->underline) {
        // The 'post' table's underline position (below the baseline when negative) and thickness, in font units.
        const stbtt_fontinfo& info = mImpl->info;
        const stbtt_uint32 post = stbtt__find_table(info.data, static_cast<stbtt_uint32>(info.fontstart), "post");
        const int position = post != 0 ? static_cast<int16_t>(ReadU16(info.data + post + 8)) : -100;
        const int thickness = post != 0 ? static_cast<int16_t>(ReadU16(info.data + post + 10)) : 50;
        const double t = std::max(1.0, std::round(thickness * static_cast<double>(sy)));
        const double centre = baseline - std::round(position * static_cast<double>(sy));
        const int top = static_cast<int>(std::lround(centre - t / 2)), bottom = static_cast<int>(std::lround(centre + t / 2));
        const int right = static_cast<int>(std::lround(x + Advance(text, length) * scale));
        for (int py = std::max(0, top); py < std::min(height, bottom); ++py)
            for (int px = std::max(0, x); px < std::min(width, right); ++px) ink[static_cast<size_t>(py) * width + px] = 0xff;
    }
}

}  // namespace gdi_text
}  // namespace tpw
#endif
