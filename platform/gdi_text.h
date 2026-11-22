// gdi_text.h: the CoreText side of gdi.cpp, in its own file because Apple's headers (MacTypes.h)
// define Rect and Point, which the game's headers define as well. Only standard types cross.
#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace tpw {
namespace gdi_text {

// Registers the font file for this process and remembers its faces (family, full and PostScript
// name); answers how many faces it has, 0 when it cannot be read.
int32_t AddFontFile(const std::string& hostPath);

// A message printed once per key to the platform log.
void LogOnce(const std::string& key, const std::string& message);

// What a LOGFONTA says.
struct Style {
    std::string face;
    int32_t height = 0;      // lfHeight (negative or positive: the size is |height|, 0 meaning 12)
    int32_t weight = 400;
    bool italic = false;
    bool underline = false;
    int32_t charset = 0;     // lfCharSet: 204 (RUSSIAN_CHARSET) reads the text as code page 1251, any other as Mac Roman
};

// Windows code page 1251's 0x80..0xff as Unicode (0x98 has no letter), for a font of charset 204: the signs of a
// translation written in Cyrillic (TPW_PORT_FIX signcyrillic, src/game/rides/RideSign.cpp). Such a font whose face has no
// Cyrillic letters is drawn in the face an unknown name gets.
inline constexpr char16_t kCp1251High[128] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0xFFFD, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
};
inline constexpr int32_t kCyrillicCharset = 204;

// One face at one size.
class Font {
public:
    // The face of a registered font file, else a system font of that name, else Helvetica.
    static std::unique_ptr<Font> Create(const Style& style);
    ~Font();
    // QuickDraw's GetFontInfo ascent (rounded up) and TextWidth of the 92-character reference
    // string divided by 92 (truncated).
    int32_t Ascent() const;
    int32_t AverageWidth() const;
    // The family name of the face actually in use (Helvetica after a fallback); for the tests and logs.
    std::string FamilyName() const;
    // The width of the text in pixels at the size, unscaled; no kerning or ligatures.
    double Advance(const char* text, int32_t length) const;
    // Sets to 0xff every pixel of ink (width * height bytes, top row first) the glyphs cover,
    // without antialiasing: the text starts at column x on the baseline row, scaled
    // horizontally by scale from x. Pixels outside the image are clipped.
    void Draw(uint8_t* ink, int32_t width, int32_t height, int32_t x, int32_t baseline, double scale, const char* text,
              int32_t length) const;

private:
    Font();
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

}  // namespace gdi_text
}  // namespace tpw
