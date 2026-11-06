// gdi.cpp: macdoze's GDI text calls (GetDC, CreateFontIndirect, CreateDIBSection, SelectObject,
// GetTextExtentPoint, TextOut, ...), which TrueTypeRender::render_font_unicode uses to draw the
// ride signs' text into an 8-bit mask. The Mac shim drew with QuickDraw into an offscreen
// GWorld; here CoreText draws into a byte buffer.
//
// How the Mac shim behaved (macdoze shared, macdoze_graphics.cpp) is followed wherever it
// differs from Win32:
//   - A handle carries a reference count (a new one holds 1, selecting it into a DC adds 1);
//     DeleteObject closes only an object whose count is below 2 and answers whether it did.
//   - GetDC (0x10007bac, HDCHandle::AllocateHDCHandle 0x1000e990) gives a DC with a default
//     font and a default bitmap, both selected, so the first SelectObject answers them.
//   - CreateCompatibleDC (0x10007d30) only raises the DC's count and hands back the SAME DC;
//     DeleteDC (0x10007cbc) and ReleaseDC (0x10007c10) are both a CloseHandle, so the window
//     DC lives until the second of the two.
//   - Text is drawn with QuickDraw's StdText: no antialiasing, only the inked pixels change
//     (srcOr), the baseline is y + the font's ascent and the text is scaled horizontally by
//     lfWidth / (average width of a 92-character reference string) (FontHandle::Select
//     0x1000e010, HDCHandle::DrawText 0x1000ead4, MeasureText 0x1000ec0c).
//   - The size is |lfHeight| points (TextSize; 0 means 12), bold is lfWeight > 699.
//
// What differs here: CoreText's metrics are floating point where QuickDraw's came from the
// font's FOND tables (the ascent is rounded up, the average width truncated); kerning and
// ligatures are switched off (StdText has neither); a face with no bold or italic variant is
// drawn in its regular style (the Mac synthesized the style).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "gdi_text.h"
#include "platform.h"
#include "platform/macdoze.h"
#include "path_text.h"

namespace fs = std::filesystem;

namespace NS_MacDoze {

namespace {

using tpw::gdi_text::LogOnce;

// ---- Handles ------------------------------------------------------------------------------

struct Handle {
    enum Kind { kDC, kFont, kBitmap } kind;
    int32_t refs = 1;
    explicit Handle(Kind k) : kind(k) {}
    virtual ~Handle() {}
};

// Every live handle, so a stale or foreign pointer handed to these calls is refused instead
// of dereferenced. Never destroyed (the game's globals may close handles while it exits).
std::unordered_set<Handle*>& Live() {
    static auto* handles = new std::unordered_set<Handle*>;
    return *handles;
}

template<class T> T* Add(T* handle) {
    Live().insert(handle);
    return handle;
}

struct FontHandle : Handle {
    explicit FontHandle(const tagLOGFONTA& font) : Handle(kFont), logfont(font) {
        logfont.lfFaceName[sizeof logfont.lfFaceName - 1] = 0;
    }

    // The CoreText font for the LOGFONT, made the first time it is needed (a font file added
    // after CreateFontIndirect is still found). FontHandle::Select's two figures (+0x58, +0x5c)
    // are the font's ascent and average width.
    tpw::gdi_text::Font* Font() {
        if (!font) {
            tpw::gdi_text::Style style;
            style.face = logfont.lfFaceName;
            style.height = logfont.lfHeight;
            style.weight = logfont.lfWeight;
            style.italic = logfont.lfItalic != 0;
            style.underline = logfont.lfUnderline != 0;
            font = tpw::gdi_text::Font::Create(style);
        }
        return font.get();
    }
    // The horizontal scale StdText is given (numer.h / denom.h = lfWidth / average width).
    double Scale() {
        if (logfont.lfWidth == 0) {
            LogOnce("lfWidth0", "a font with lfWidth 0 is drawn unscaled");
            return 1.0;
        }
        const int32_t average = Font()->AverageWidth();
        return static_cast<double>(logfont.lfWidth) / (average == 0 ? 1 : average);
    }

    tagLOGFONTA logfont;

private:
    std::unique_ptr<tpw::gdi_text::Font> font;
};

struct BitmapHandle : Handle {
    BitmapHandle() : Handle(kBitmap) {}
    int32_t width = 0;
    int32_t height = 0;
    int32_t bitCount = 8;
    int32_t rowBytes = 0;
    bool topDown = true;
    std::vector<uint8_t> pixels;      // rowBytes * height, storage order (row 0 is the top when topDown)
    std::vector<tagRGBQUAD> palette;  // the colour table of an 8-bit bitmap
};

struct DCHandle : Handle {
    DCHandle() : Handle(kDC) {}
    GrafPort* window = nullptr;
    FontHandle* font = nullptr;            // selected (+0x1c)
    BitmapHandle* bitmap = nullptr;        // selected (+0x20)
    FontHandle* defaultFont = nullptr;     // +0x24
    BitmapHandle* defaultBitmap = nullptr; // +0x28
    uint32_t textColour = 0xffffff;
    uint32_t backColour = 0xffffff;
    int32_t mapMode = 1;
};

// CloseHandle for these handles: the count drops, and a handle at zero is destroyed (a DC
// first closes its four objects, HDCHandle::Cleanup 0x1000e428).
int32_t Close(Handle* handle) {
    if (--handle->refs > 0) return 1;
    handle->refs = 0;
    Live().erase(handle);
    if (handle->kind == Handle::kDC) {
        auto* dc = static_cast<DCHandle*>(handle);
        // (a default object the game deleted itself is gone already: skip it)
        for (Handle* object : {static_cast<Handle*>(dc->font), static_cast<Handle*>(dc->bitmap), static_cast<Handle*>(dc->defaultFont),
                               static_cast<Handle*>(dc->defaultBitmap)}) {
            if (Live().count(object) != 0) Close(object);
        }
    }
    delete handle;
    return 1;
}

template<class T> T* Find(InternalHandle* handle, Handle::Kind kind) {
    auto* candidate = reinterpret_cast<Handle*>(handle);
    if (Live().count(candidate) == 0 || candidate->kind != kind) return nullptr;
    return static_cast<T*>(candidate);
}

InternalHandle* Opaque(Handle* handle) { return reinterpret_cast<InternalHandle*>(handle); }

DCHandle* FindDC(InternalHandle* dc, const char* call) {
    DCHandle* found = Find<DCHandle>(dc, Handle::kDC);
    if (found == nullptr) LogOnce(std::string("baddc:") + call, std::string(call) + ": not a device context, refused");
    return found;
}

// Selects an object into the DC (HDCHandle::SelectObject 0x1000e5c8): the previous object of
// that kind loses a count (and dies at zero), the new one gains one.
Handle* SelectInto(DCHandle* dc, Handle* object) {
    Handle* previous;
    ++object->refs;
    if (object->kind == Handle::kFont) {
        previous = dc->font;
        dc->font = static_cast<FontHandle*>(object);
    } else {
        previous = dc->bitmap;
        dc->bitmap = static_cast<BitmapHandle*>(object);
    }
    if (previous != nullptr) Close(previous);
    return previous;
}

// The index of the colour table entry nearest to the COLORREF (squared RGB distance).
uint8_t NearestIndex(const std::vector<tagRGBQUAD>& palette, uint32_t colour) {
    const int32_t red = colour & 0xff;
    const int32_t green = (colour >> 8) & 0xff;
    const int32_t blue = (colour >> 16) & 0xff;
    int32_t best = 0;
    int32_t bestDistance = INT32_MAX;
    for (size_t i = 0; i < palette.size(); ++i) {
        const int32_t dr = palette[i].rgbRed - red;
        const int32_t dg = palette[i].rgbGreen - green;
        const int32_t db = palette[i].rgbBlue - blue;
        const int32_t distance = dr * dr + dg * dg + db * db;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = static_cast<int32_t>(i);
        }
    }
    return static_cast<uint8_t>(best);
}

BitmapHandle* NewBitmap(int32_t width, int32_t height, int32_t bitCount, bool topDown) {
    auto* bitmap = new BitmapHandle;
    bitmap->width = width;
    bitmap->height = height;
    bitmap->bitCount = bitCount;
    bitmap->rowBytes = (bitCount / 8) * width;   // the shim's rows are not padded
    bitmap->topDown = topDown;
    bitmap->pixels.assign(static_cast<size_t>(bitmap->rowBytes) * static_cast<size_t>(height), 0);
    return Add(bitmap);
}

// The LOGFONTA of TrueTypeRender's constructor (src/game/rides/RideSign.cpp), the DC's default font.
tagLOGFONTA DefaultLogFont() {
    tagLOGFONTA font;
    std::memset(&font, 0, sizeof font);
    font.lfWeight = 400;
    font.lfCharSet = 1;
    font.lfOutPrecision = 4;
    font.lfClipPrecision = 1;
    font.lfQuality = 4;
    std::strcpy(font.lfFaceName, "Arial Black");
    return font;
}

}  // namespace

// ---- Fonts --------------------------------------------------------------------------------

// The number of font faces in the file (the Mac shim answered the number of 'FOND' resources),
// 0 when the file cannot be read. The path is the game's (Mac or Windows form); a full host
// path of an existing file is accepted as well.
int32_t AddFontResource(const char* path) {
    if (path == nullptr) return 0;
    fs::path host = tpw::FindHostPath(path);
    std::error_code ec;
    if (host.empty() && path[0] == '/' && fs::is_regular_file(path, ec)) host = path;
    if (host.empty() || !fs::is_regular_file(host, ec)) {
        LogOnce(std::string("nofont:") + path, std::string("AddFontResource: cannot open ") + path);
        return 0;
    }
    return tpw::gdi_text::AddFontFile(tpw::PathText(host));
}

InternalHandle* CreateFontIndirect(const tagLOGFONTA* font) {
    if (font == nullptr) return nullptr;
    return Opaque(Add(new FontHandle(*font)));
}

// ---- Device contexts ----------------------------------------------------------------------

InternalHandle* GetDC(GrafPort* window) {
    auto* dc = Add(new DCHandle);
    dc->window = window;
    // Born with a default font and a default bitmap, both selected (count 2: the DC's own and
    // the selection's).
    dc->defaultFont = Add(new FontHandle(DefaultLogFont()));
    dc->defaultBitmap = NewBitmap(1, 1, 8, true);
    dc->defaultBitmap->palette.assign(256, tagRGBQUAD{0, 0, 0, 0});
    for (int32_t i = 0; i < 256; ++i) {
        dc->defaultBitmap->palette[i] = tagRGBQUAD{static_cast<uint8_t>(i), static_cast<uint8_t>(i), static_cast<uint8_t>(i), 0};
    }
    SelectInto(dc, dc->defaultFont);
    SelectInto(dc, dc->defaultBitmap);
    return Opaque(dc);
}

int32_t ReleaseDC(GrafPort*, InternalHandle* dc) {
    DCHandle* found = Find<DCHandle>(dc, Handle::kDC);
    if (found == nullptr) {
        LogOnce("ReleaseDC", "ReleaseDC: not a device context, refused");
        return 0;
    }
    return Close(found);
}

// The Mac shim only raised the DC's count; the caller got the same DC back.
InternalHandle* CreateCompatibleDC(InternalHandle* dc) {
    DCHandle* found = FindDC(dc, "CreateCompatibleDC");
    if (found == nullptr) return nullptr;
    ++found->refs;
    return dc;
}

int32_t DeleteDC(InternalHandle* dc) {
    DCHandle* found = FindDC(dc, "DeleteDC");
    if (found == nullptr) return 0;
    Close(found);
    return 1;
}

int32_t SetMapMode(InternalHandle* dc, int32_t mode) {
    DCHandle* found = FindDC(dc, "SetMapMode");
    if (found == nullptr) return 0;
    const int32_t previous = found->mapMode;
    found->mapMode = mode;
    return previous;
}

uint32_t SetBkColor(InternalHandle* dc, uint32_t colour) {
    DCHandle* found = FindDC(dc, "SetBkColor");
    if (found == nullptr) return 0;
    const uint32_t previous = found->backColour;
    found->backColour = colour & 0xffffff;
    return previous;
}

uint32_t SetTextColor(InternalHandle* dc, uint32_t colour) {
    DCHandle* found = FindDC(dc, "SetTextColor");
    if (found == nullptr) return 0;
    const uint32_t previous = found->textColour;
    found->textColour = colour & 0xffffff;
    return previous;
}

InternalHandle* SelectObject(InternalHandle* dc, InternalHandle* object) {
    if (object == nullptr) return nullptr;
    DCHandle* found = FindDC(dc, "SelectObject");
    auto* candidate = reinterpret_cast<Handle*>(object);
    if (found == nullptr || Live().count(candidate) == 0 || candidate->kind == Handle::kDC) {
        LogOnce("SelectObject", "SelectObject: not a font or bitmap, refused");
        return nullptr;
    }
    return Opaque(SelectInto(found, candidate));
}

// Closes an object that is not selected anywhere (count below 2); answers whether it did.
int32_t DeleteObject(InternalHandle* object) {
    auto* handle = reinterpret_cast<Handle*>(object);
    if (Live().count(handle) == 0) {
        LogOnce("DeleteObject", "DeleteObject: not a live handle, refused");
        return 0;
    }
    if (handle->refs >= 2) return 0;
    Close(handle);
    return 1;
}

// ---- Bitmaps ------------------------------------------------------------------------------

// Only the layout the game uses is real: usage 0 (the colour table in the BITMAPINFO) and 8,
// 24 or 32 bits. *bits is a zero-filled buffer of rowBytes (no padding) per row, with row 0
// at the top for a negative biHeight, as in the shim's packed buffer.
InternalHandle* CreateDIBSection(InternalHandle*, const tagBITMAPINFO* info, uint32_t usage, void** bits,
                                 InternalHandle*, uint32_t) {
    if (info == nullptr || bits == nullptr) return nullptr;
    const tagBITMAPINFOHEADER& header = info->bmiHeader;
    const int32_t bitCount = header.biBitCount;
    const int32_t height = header.biHeight < 0 ? -header.biHeight : header.biHeight;
    if (usage != 0 || (bitCount != 8 && bitCount != 24 && bitCount != 32) || header.biWidth <= 0 || height <= 0 ||
        header.biWidth > 16384 || height > 16384) {
        LogOnce("dib", "CreateDIBSection: only usage 0, 8, 24 or 32 bits and a sane size are supported, refused");
        return nullptr;
    }
    BitmapHandle* bitmap = NewBitmap(header.biWidth, height, bitCount, header.biHeight < 0);
    if (bitCount == 8) {
        const int32_t entries = header.biClrUsed == 0 || header.biClrUsed > 256 ? 256 : static_cast<int32_t>(header.biClrUsed);
        const auto* colors = reinterpret_cast<const tagRGBQUAD*>(reinterpret_cast<const uint8_t*>(info) + header.biSize);
        bitmap->palette.assign(colors, colors + entries);
    }
    *bits = bitmap->pixels.data();
    return Opaque(bitmap);
}

// ---- Text ---------------------------------------------------------------------------------

int32_t GetTextExtentPoint(InternalHandle* dc, const char* text, int32_t length, tagSIZE* size) {
    DCHandle* found = FindDC(dc, "GetTextExtentPoint");
    if (found == nullptr || size == nullptr) return 0;
    FontHandle* font = found->font;
    double width = 0;
    tpw::gdi_text::Font* face = font->Font();
    if (face != nullptr) width = face->Advance(text, static_cast<int16_t>(length)) * font->Scale();
    // StdTxMeas answers a short.
    width = std::min(32767.0, std::max(-32768.0, width));
    size->cx = static_cast<int32_t>(width);
    size->cy = font->logfont.lfHeight;
    return 1;
}

int32_t TextOut(InternalHandle* dc, int32_t x, int32_t y, const char* text, int32_t length) {
    DCHandle* found = FindDC(dc, "TextOut");
    if (found == nullptr) return 0;
    BitmapHandle* bitmap = found->bitmap;
    FontHandle* font = found->font;
    tpw::gdi_text::Font* face = font->Font();
    if (face == nullptr) return 1;

    // The glyphs go into a scratch image the size of the bitmap; every pixel they touch gets
    // the text colour in the bitmap (srcOr: nothing else changes). The pen is at (x, y +
    // ascent), x cut to a short as the shim did.
    const size_t w = static_cast<size_t>(bitmap->width);
    const size_t h = static_cast<size_t>(bitmap->height);
    std::vector<uint8_t> ink(w * h, 0);
    face->Draw(ink.data(), bitmap->width, bitmap->height, static_cast<int16_t>(x), y + face->Ascent(), font->Scale(), text,
               static_cast<int16_t>(length));

    const uint32_t colour = found->textColour;
    const uint8_t index = bitmap->bitCount == 8 ? NearestIndex(bitmap->palette, colour) : 0;
    const int32_t bytes = bitmap->bitCount / 8;
    for (size_t row = 0; row < h; ++row) {
        uint8_t* destination = bitmap->pixels.data() +
                               static_cast<size_t>(bitmap->rowBytes) * (bitmap->topDown ? row : h - 1 - row);
        const uint8_t* source = ink.data() + row * w;
        for (size_t column = 0; column < w; ++column) {
            if (source[column] == 0) continue;
            if (bytes == 1) {
                destination[column] = index;
            } else {
                destination[column * bytes + 0] = static_cast<uint8_t>(colour >> 16);   // blue
                destination[column * bytes + 1] = static_cast<uint8_t>(colour >> 8);    // green
                destination[column * bytes + 2] = static_cast<uint8_t>(colour);         // red
                if (bytes == 4) destination[column * bytes + 3] = 0;
            }
        }
    }
    return 1;
}

}  // namespace NS_MacDoze

namespace tpw {
// How many GDI handles are alive (the platform test checks that none leak).
size_t GdiLiveHandles() { return NS_MacDoze::Live().size(); }
}  // namespace tpw
