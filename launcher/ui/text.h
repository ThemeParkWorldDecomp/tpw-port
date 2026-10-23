// text.h: the launcher screen's text, drawn with stb_truetype from the two fonts compiled into the launcher
// (third_party/fonts: Fredoka for headings and buttons, Nunito for body text). Each face and pixel size gets its own
// glyph atlas, made the first time it is used: Latin-1 and a few punctuation marks, which covers the launcher's own
// words and the file names of the discs (their MacRoman names arrive as UTF-8). Other characters draw as '?'.
#pragma once
#include <SDL3/SDL.h>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "draw.h"

namespace tpw::ui {

enum class Face { Heading, Button, Body, BodyBold };   // Fredoka Bold, Fredoka SemiBold, Nunito Regular, Nunito Bold

struct Style {
    Face face;
    float size;   // points
};

namespace style {
inline constexpr Style kTitle{Face::Heading, 34};
inline constexpr Style kHeadline{Face::Heading, 24};
inline constexpr Style kHeading{Face::Button, 19};
inline constexpr Style kButton{Face::Button, 17};
inline constexpr Style kBody{Face::Body, 15};
inline constexpr Style kBodyBold{Face::BodyBold, 15};
inline constexpr Style kSmall{Face::Body, 13};
inline constexpr Style kSmallBold{Face::BodyBold, 13};
}  // namespace style

class Text {
public:
    explicit Text(Canvas& canvas);
    ~Text();
    Text(const Text&) = delete;
    Text& operator=(const Text&) = delete;

    // False (with Error()) when a font could not be read or an atlas texture could not be made.
    bool Ok() const { return error_.empty(); }
    const std::string& Error() const { return error_; }

    float Width(Style s, std::string_view utf8);
    float LineHeight(Style s);   // ascent + descent + line gap, in points
    float Ascent(Style s);       // top of the line box to the baseline, in points (baseline-aligns two styles)
    // Draws one line with the top of its line box at y; returns its width.
    float Draw(Style s, float x, float y, std::string_view utf8, Rgba colour);
    void DrawCentred(Style s, const Rect& box, std::string_view utf8, Rgba colour);
    // The text with a dark outline `outline` points wide around every glyph (the title's sticker look).
    void DrawOutlined(Style s, float x, float y, std::string_view utf8, Rgba colour, Rgba line, float outline);

    // Breaks the text into lines no wider than `width` (at spaces; a word wider than a line is cut).
    std::vector<std::string> Wrap(Style s, std::string_view utf8, float width);
    // Draws wrapped lines from y; returns the height used.
    float DrawWrapped(Style s, float x, float y, float width, std::string_view utf8, Rgba colour, float lineGap = 0);
    // The text shortened to `width` with "..." in place of the middle (paths keep their two ends) or the end.
    std::string Fit(Style s, std::string_view utf8, float width, bool keepEnd);
    // The face's version as its font file names it (the name table's version string, "Version " dropped), or "".
    std::string FontVersion(Face face) const;

private:
    struct Font;
    struct Atlas;
    Atlas* Get(Style s);
    std::vector<uint32_t> Decode(std::string_view utf8) const;

    Canvas& canvas_;
    std::unique_ptr<Font> fonts_[4];
    std::map<std::pair<int, int>, std::unique_ptr<Atlas>> atlases_;   // (face, pixel size)
    std::string error_;
};

}  // namespace tpw::ui
