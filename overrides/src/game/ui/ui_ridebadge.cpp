// ui_ridebadge.cpp: the port's ride badge tag on the Buy Ride panel's picture (no Mac code; the goldbadge fix of UpdateViewWindow in
// src/game/ui/ui_ridepanel.cpp). Its own file so that the unit test of ui_ridepanel.cpp (tests/unit_FillRideList.cpp) needs only a stand-in.
// TPW_PORT_FIX goldbadge DrawRideBadge | Mac: no such function | Port: draws the "GOLD" or "PS1" tag in the top left corner of the Buy Ride panel's picture (UpdateViewWindow) | Why: the launcher's "Show Ride Badges" (on by default) marks the Gold Edition's extra rides and the PlayStation rides mod's rides

#include "LbDraw.h"
#include "LbDrawPrimitives.h"
#include "RideBadges.h"
#include "TbColour.h"
#include "TbFontRenderParams.h"
#include "TbTextFormatter.h"
#include "Text.h"
#include "ui_main.h"
#include <cstdint>

namespace {

// The Mac's TbColour in two registers: the five bytes and nothing else (as in src/game/ui/ui_ridepanel.cpp).
TbColour MakeColour(const uint8_t* bytes) {
    alignas(TbColour) uint8_t raw[sizeof(TbColour)] = {bytes[0], bytes[1], bytes[2], bytes[3], bytes[4]};
    return *reinterpret_cast<const TbColour*>(raw);
}

}  // namespace

// The ride badge's tag (include/RideBadges.h) on the picture's surface, the current render area, `width` x `height`
// pixels: a filled box with a 1 pixel border in the top left corner and the text in the font of the picture's label
// (font 6 of the current resolution group, so it grows with the window as the label does), blended onto the box.
// Nothing for kNone. Its own font parameters and formatter, so that gText's are left as they were.
void DrawRideBadge(RideBadge badge, int32_t width, int32_t height) {
    const char16_t* text = RideBadgeText(badge);
    if (text == nullptr) return;
    TbIFont* font = gText.mFonts[gText.mResolutionGroup * 13 + 6].mFont;
    if (font == nullptr) return;
    TbFontRenderParams params;
    params.SetFont(font);
    TbTextFormatter formatter;
    formatter.SetHorizontalJustification(TbTextFormatter::kLeft);
    formatter.SetVerticalJustification(TbTextFormatter::kTop);
    TbRect measured = {0, 0, 0, 0};
    formatter.Measure(TbRect{0, 0, 0x4000, height}, UIMain_ConvertString(text), params, &measured);
    const RideBadgeBox box = PlaceRideBadge(width, height, measured.right - measured.left, measured.bottom - measured.top);
    if (box.right <= box.left) return;
    const bool gold = badge == RideBadge::kGold;
    const uint8_t border[5] = {0xff, static_cast<uint8_t>(gold ? 0x6b : 0xc8), static_cast<uint8_t>(gold ? 0x46 : 0xc8),
                               static_cast<uint8_t>(gold ? 0x00 : 0xd2), 0};
    const uint8_t fill[5] = {0xff, static_cast<uint8_t>(gold ? 0xf0 : 0x2a), static_cast<uint8_t>(gold ? 0xc0 : 0x2e),
                             static_cast<uint8_t>(gold ? 0x30 : 0x3c), 0};
    const uint8_t ink[5] = {0xff, static_cast<uint8_t>(gold ? 0x3c : 0xff), static_cast<uint8_t>(gold ? 0x26 : 0xff),
                            static_cast<uint8_t>(gold ? 0x00 : 0xff), 0};
    const TbRect outer = {box.left, box.top, box.right, box.bottom};
    const TbRect inner = {box.left + 1, box.top + 1, box.right - 1, box.bottom - 1};
    LbDraw_RectangleFilled(&outer, MakeColour(border));
    LbDraw_RectangleFilled(&inner, MakeColour(fill));
    params.UseReColour(MakeColour(ink));
    LbDraw_SetClipRect(&inner);
    TbRect drawn = {0, 0, 0, 0};
    formatter.Draw(TbRect{box.textLeft - measured.left, box.textTop - measured.top, 0x4000, height}, UIMain_ConvertString(text),
                   params, &drawn);
    LbDraw_SetClipRect(nullptr);
}

