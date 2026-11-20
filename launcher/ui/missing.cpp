// The exact file list behind each edition card, including gaps that do not block Play.
#include "launcher_ui.h"

#include <algorithm>

namespace tpw::ui {
// The edition chosen to play has none of its intro films: Skip intro is forced on and locked (the game would skip
// each one anyway, GameSys.cpp).
bool LauncherUi::IntroUnavailable() const {
    const assets::EditionStatus& s = status_[playIndex_];
    return s.introFilms > 0 && s.introFilmsPresent == 0;
}

// Only on a card with something in place and a gap to name: a card with nothing yet says which discs to drag in.
void LauncherUi::MissingButton(int index, const Rect& rect) {
    const assets::EditionStatus& s = status_[index];
    if (s.present == 0 || s.gaps.empty()) return;
    missingButtons_[index] = rect;
    if (ui_.Button("missing-" + std::to_string(index), rect, "What's missing", ButtonKind::Plain, true,
                   style::kSmallBold)) ShowMissing(index);
}

// An orange card's state line ("Ready to play (files missing, minimal effect)", the maintainer, 2026-11-19) tells what is behind
// it: the pointer over it shows the gaps in short (DrawGapHint), a click opens the What's missing box.
void LauncherUi::StateLine(int index) {
    const assets::EditionStatus& s = status_[index];
    if (!s.unlocked || s.gaps.empty()) return;
    const Rect r = ui_.TileStateRect();
    stateLines_[index] = r;
    if (!ui_.Interactive() || !ui_.Hovered(r)) return;
    canvas_.FillRound({r.x, r.Bottom() - 1, r.w, 1.5f}, 0, colour::kInk);   // underlined while it is pointed at
    if (ui_.ClickArea("state-" + std::to_string(index), r)) ShowMissing(index);
    else gapHint_ = index;
}

// The gaps of the card under the pointer, under its state line (above it near the window's bottom): up to four, each
// file and what it changes, then how many more and how to see them all.
void LauncherUi::DrawGapHint(const Rect& window) {
    const assets::EditionStatus& s = status_[gapHint_];
    const Rect at = stateLines_[gapHint_];
    const float width = std::min(window.w - 2 * metric::kMargin, 380.0f), textW = width - 28;
    const float lineH = text_.LineHeight(style::kSmall), boldH = text_.LineHeight(style::kSmallBold);
    std::vector<std::pair<std::vector<std::string>, bool>> lines;   // a block's lines, bold or not
    const size_t shown = std::min<size_t>(s.gaps.size(), 4);
    for (size_t i = 0; i < shown; ++i) {
        const assets::FileGap& gap = s.gaps[i];
        lines.push_back({text_.Wrap(style::kSmallBold, gap.dest, textW), true});
        lines.push_back({text_.Wrap(style::kSmall, std::string(gap.essential ? "Essential. " : "Optional. ") + gap.effect,
                                    textW), false});
    }
    std::string more = s.gaps.size() > shown ? "And " + FormatCount(s.gaps.size() - shown) + " more. " : std::string();
    lines.push_back({text_.Wrap(style::kSmall, more + "Click for the full list.", textW), false});
    float height = 28;
    for (const auto& block : lines) height += (block.second ? boldH : lineH) * block.first.size() + (block.second ? 0 : 6);
    float x = std::clamp(at.x, window.x + metric::kMargin, window.Right() - metric::kMargin - width);
    float y = at.Bottom() + 8;
    if (y + height > window.Bottom() - 8) y = std::max(window.y + 8, at.y - 8 - height);
    gapHintRect_ = {x, y, width, height};
    ui_.Panel(gapHintRect_, colour::kPaper, colour::kCream, 12);
    y += 14;
    for (const auto& block : lines) {
        for (const std::string& line : block.first) {
            text_.Draw(block.second ? style::kSmallBold : style::kSmall, x + 14, y, line,
                       block.second ? colour::kInk : colour::kInkSoft);
            y += block.second ? boldH : lineH;
        }
        if (!block.second) y += 6;
    }
}

void LauncherUi::DrawMissing(const Rect& window) {
    canvas_.FillRound(window, 0, colour::kDim);
    const assets::EditionStatus& s = status_[missingOpen_];
    const float width = std::min(window.w - 2 * metric::kMargin, 720.0f);
    const float textW = width - 40 - 16;   // the panel's insets, and room for the scroll bar
    const float lineH = text_.LineHeight(style::kSmall), boldH = text_.LineHeight(style::kSmallBold);
    // The rows first: the panel is as tall as they need, up to the window.
    std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>> rows;
    float total = 0;
    const auto add = [&](const std::string& head, const std::string& note) {
        rows.push_back({text_.Wrap(style::kSmallBold, head, textW), text_.Wrap(style::kSmall, note, textW)});
        total += boldH * rows.back().first.size() + lineH * rows.back().second.size() + 16;
    };
    for (const auto& gap : s.gaps) add(gap.dest, std::string(gap.essential ? "Essential. " : "Optional. ") + gap.effect);
    if (s.gaps.empty()) add("Every required file is present.", "The game is ready to play.");
    if (s.edited) add(Files(s.edited) + " edited settings kept.", "Open Customized settings to see each file.");
    const std::vector<std::string> intro = text_.Wrap(
        style::kSmall, std::string("Only a missing essential file stops Play. ") + kDropMore, width - 40);
    const float head = 44 + text_.LineHeight(style::kBodyBold) + 6 + lineH * intro.size() + 12;
    const float height = std::min(window.h - 2 * metric::kMargin, 40 + head + total);
    Rect panel{window.CentreX() - width / 2, window.CentreY() - height / 2, width, height};
    ui_.Panel(panel, colour::kPaper, colour::kCream);
    const Rect in = panel.Inset(20);
    const float closeW = ui_.ButtonWidth("Close");
    text_.Draw(style::kHeadline, in.x, in.y, "What's missing", colour::kInk);
    if (ui_.Button("missing-close", {in.Right() - closeW, in.y, closeW, 36}, "Close", ButtonKind::Quiet))
        missingOpen_ = -1;
    float y = in.y + 44;
    const std::string heading =
        std::string(EditionTitle(s.edition)) + ": " + (!s.unlocked ? std::string("not ready yet") : ReadyLabel(s, true));
    text_.Draw(style::kBodyBold, in.x, y, heading, colour::kInk);
    y += text_.LineHeight(style::kBodyBold) + 6;
    for (const auto& line : intro) {
        text_.Draw(style::kSmall, in.x, y, line, colour::kInkSoft);
        y += lineH;
    }
    const Rect list{in.x, y + 12, in.w, std::max(0.0f, in.Bottom() - y - 12)};
    const Rect bar{list.Right() - 7, list.y, 7, list.h};
    float offset = ui_.Scroll("missing-list-" + std::to_string(missingOpen_), list, 36,
                             std::max(0.0f, total - list.h), bar, list.h);
    ui_.Clip(list);
    y = list.y - offset;
    for (const auto& row : rows) {
        for (const auto& line : row.first) {
            text_.Draw(style::kSmallBold, list.x, y, line, colour::kInk);
            y += boldH;
        }
        for (const auto& line : row.second) {
            text_.Draw(style::kSmall, list.x, y, line, colour::kInkSoft);
            y += lineH;
        }
        y += 16;
    }
    ui_.Unclip();
    if (total > list.h) ui_.ScrollBar(bar, list.h, total, offset, colour::kYellowSoft);
}
}  // namespace tpw::ui
