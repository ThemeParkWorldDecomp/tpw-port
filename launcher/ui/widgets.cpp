// widgets.cpp: the launcher screen's widgets (widgets.h).
#include "widgets.h"

#include <algorithm>
#include <cmath>

namespace tpw::ui {

namespace {
constexpr float kPi = 3.14159265358979f;

// The rectangle grown by `factor` around its centre.
Rect Grown(const Rect& r, float factor) {
    float w = r.w * factor, h = r.h * factor;
    return {r.CentreX() - w * 0.5f, r.CentreY() - h * 0.5f, w, h};
}

float Clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}
}  // namespace

void Ui::Begin(float dt, const Input& input) {
    dt_ = std::clamp(dt, 0.0f, 0.1f);
    time_ += dt_;
    ++frame_;
    input_ = input;
    interactive_ = true;
    animating_ = false;
    if (clipped_) Unclip();
}

void Ui::End() {
    if (input_.released || !input_.down) active_.clear();
}

bool Ui::Hovered(const Rect& r) const {
    return r.Contains(input_.mouseX, input_.mouseY) && (!clipped_ || clip_.Contains(input_.mouseX, input_.mouseY));
}

float Ui::Scroll(const std::string& id, const Rect& area, float step, float maxOffset) {
    ScrollState& s = offsets_[id];
    if (s.frame + 1 != frame_) s.offset = 0;
    s.frame = frame_;
    if (interactive_ && Hovered(area) && input_.wheel != 0) s.offset -= input_.wheel * step;
    // Kept unrounded, so a touchpad's small steps add up; drawn at whole points.
    s.offset = std::clamp(s.offset, 0.0f, std::max(0.0f, maxOffset));
    return std::round(s.offset);
}

void Ui::Clip(const Rect& r) {
    SDL_Rect clip{static_cast<int>(std::floor(r.x)), static_cast<int>(std::floor(r.y)),
                  static_cast<int>(std::ceil(r.w)), static_cast<int>(std::ceil(r.h))};
    SDL_SetRenderClipRect(canvas_.Renderer(), &clip);
    clip_ = r;
    clipped_ = true;
}

void Ui::Unclip() {
    SDL_SetRenderClipRect(canvas_.Renderer(), nullptr);
    clipped_ = false;
}

void Ui::ScrollBar(const Rect& track, float shown, float total, float offset, Rgba trackColour) {
    if (total <= shown) return;
    float radius = track.w * 0.5f;
    canvas_.FillRound(track, radius, trackColour);
    float h = std::max(18.0f, track.h * shown / total);
    float y = track.y + (track.h - h) * Clamp01(offset / (total - shown));
    canvas_.FillRound({track.x, y, track.w, h}, radius, colour::kInkSoft);
}

bool Ui::Clicked(const std::string& id, const Rect& r, bool enabled) {
    bool hot = interactive_ && enabled && Hovered(r);
    if (hot && input_.pressed) active_ = id;
    return hot && input_.released && active_ == id;
}

float Ui::Spring(const std::string& id, float target) {
    SpringState& s = springs_[id];
    if (!s.seen) {
        s.seen = true;
        s.x = target;
        s.frame = frame_;
        return s.x;
    }
    if (s.frame != frame_) {
        s.frame = frame_;
        // Stiffness 320 and damping 15 (a damping ratio of about 0.42): quick, with one small overshoot.
        float left = dt_;
        while (left > 0) {
            float h = std::min(left, 1.0f / 240.0f);
            float a = 320.0f * (target - s.x) - 15.0f * s.v;
            s.v += a * h;
            s.x += s.v * h;
            left -= h;
        }
        if (std::fabs(target - s.x) < 1e-3f && std::fabs(s.v) < 1e-2f) {
            s.x = target;
            s.v = 0;
        }
    }
    if (s.x != target) animating_ = true;
    return s.x;
}

void Ui::Panel(const Rect& r, Rgba top, Rgba bottom, float radius) {
    canvas_.Shadow(r.Moved(0, metric::kShadowDrop), radius, metric::kShadowBlur, colour::kShadow);
    canvas_.OutlinedGradient(r, radius, metric::kOutline, top, bottom, colour::kInk);
}

float Ui::ButtonWidth(std::string_view label) const {
    return std::max(96.0f, text_.Width(style::kButton, label) + 44);
}

bool Ui::Button(const std::string& id, const Rect& r, std::string_view label, ButtonKind kind, bool enabled,
                Style labelStyle) {
    bool clicked = Clicked(id, r, enabled);
    bool hot = interactive_ && enabled && Hovered(r);
    bool held = hot && input_.down && active_ == id;
    float hover = Spring(id + "#hover", hot ? 1.0f : 0.0f);
    float press = Spring(id + "#press", held ? 1.0f : 0.0f);
    float lift = 2.5f * hover - 2.5f * press;
    Rect d = Grown(r, 1.0f + 0.05f * hover - 0.03f * press).Moved(0, -lift);

    Rgba top, bottom, label_colour = colour::kInk;
    switch (kind) {
        case ButtonKind::Go: top = colour::kLimeBright; bottom = colour::kLime; break;
        case ButtonKind::Plain: top = colour::kYellowSoft; bottom = colour::kYellow; break;
        case ButtonKind::Quiet: top = colour::kPaper; bottom = colour::kCream; break;
        case ButtonKind::Danger:
            top = Mix(colour::kRed, colour::kPaper, 0.25f);
            bottom = colour::kRed;
            label_colour = colour::kPaper;
            break;
    }
    Rgba line = colour::kInk;
    if (!enabled) {
        top = Mix(colour::kGrey, colour::kPaper, 0.5f);
        bottom = colour::kGrey;
        label_colour = colour::kInkSoft;
        line = colour::kInkSoft;
    }
    float radius = d.h * 0.5f;
    canvas_.Shadow(d.Moved(0, 3.0f + lift), radius, 6, colour::kShadow);
    canvas_.OutlinedGradient(d, radius, metric::kOutline, top, bottom, line);
    // A gloss along the top half.
    Rect gloss = d.Inset(metric::kOutline + 3);
    gloss.h *= 0.42f;
    canvas_.FillRound(gloss, gloss.h * 0.5f, colour::kPaper.WithAlpha(enabled ? 80 : 40));
    text_.DrawCentred(labelStyle, d.Moved(0, 0.5f), label, label_colour);
    return clicked;
}

void Ui::DrawBadge(Badge badge, float cx, float cy, float radius, float fraction) {
    if (badge == Badge::None) return;
    canvas_.FillCircle(cx, cy, radius, colour::kInk);
    float inner = radius - 2.5f;
    switch (badge) {
        case Badge::Check:
            canvas_.FillCircle(cx, cy, inner, colour::kLime);
            canvas_.CheckMark(cx, cy + radius * 0.04f, radius * 1.15f, radius * 0.26f, colour::kPaper);
            break;
        case Badge::Lock: {
            canvas_.FillCircle(cx, cy, inner, colour::kYellowSoft);
            std::vector<Point> shackle;
            float sr = radius * 0.27f, sy = cy - radius * 0.08f;
            for (int i = 0; i <= 12; ++i) {
                float a = kPi + kPi * i / 12;
                shackle.push_back({cx + std::cos(a) * sr, sy + std::sin(a) * sr});
            }
            canvas_.Polyline(shackle, radius * 0.14f, colour::kInk);
            canvas_.FillRound({cx - radius * 0.4f, sy, radius * 0.8f, radius * 0.56f}, radius * 0.1f, colour::kInk);
            break;
        }
        case Badge::Partial: {
            canvas_.FillCircle(cx, cy, inner, colour::kPaper);
            float ar = radius * 0.56f, width = radius * 0.3f;
            std::vector<Point> track, arc;
            for (int i = 0; i <= 48; ++i) {
                float a = -kPi * 0.5f + 2 * kPi * i / 48;
                track.push_back({cx + std::cos(a) * ar, cy + std::sin(a) * ar});
            }
            canvas_.Polyline(track, width, colour::kYellowSoft);
            int steps = std::max(1, static_cast<int>(std::ceil(48 * Clamp01(fraction))));
            for (int i = 0; i <= steps; ++i) {
                float a = -kPi * 0.5f + 2 * kPi * Clamp01(fraction) * i / steps;
                arc.push_back({cx + std::cos(a) * ar, cy + std::sin(a) * ar});
            }
            if (fraction > 0) canvas_.Polyline(arc, width, colour::kOrange);
            break;
        }
        case Badge::None: break;
    }
}

void Ui::Tile(const std::string& id, const Rect& r, const TileView& v) {
    float drop = Spring(id + "#drop", v.highlight ? 1.0f : 0.0f);
    float hover = Spring(id + "#hover", interactive_ && Hovered(r) ? 1.0f : 0.0f);
    Rect d = Grown(r, 1.0f + 0.035f * drop + 0.012f * hover).Moved(0, -3.0f * drop - 1.5f * hover);
    const float radius = metric::kRadius, outline = metric::kOutline + 1.5f * Clamp01(drop);
    if (drop > 0.01f)
        canvas_.Shadow(d.Inset(-7 * Clamp01(drop)), radius + 7, 16, colour::kLimeBright.WithAlpha(
                                                                         static_cast<uint8_t>(200 * Clamp01(drop))));
    canvas_.Shadow(d.Moved(0, metric::kShadowDrop + 2 * drop), radius, metric::kShadowBlur, colour::kShadow);
    Rgba line = Mix(colour::kInk, colour::kLimeDeep, Clamp01(drop));
    Rgba body = Mix(colour::kCream, colour::kPaper, Clamp01(drop));
    canvas_.Outlined(d, radius, outline, body, line);

    // The header band: the inner shape's top, its rounded bottom covered by the body.
    const float band = 38;
    Rect inner = d.Inset(outline);
    float innerRadius = radius - outline;
    canvas_.FillRoundGradient(inner.TakeTop(band + innerRadius), innerRadius, Mix(v.accent, colour::kPaper, 0.3f),
                              v.accent);
    canvas_.FillRound({inner.x, inner.y + band, inner.w, innerRadius + 1}, 0, body);
    canvas_.FillRound({inner.x, inner.y + band - 1.25f, inner.w, 2.5f}, 0, line);

    const float pad = 14;
    float x = inner.x + pad, w = inner.w - 2 * pad;
    text_.Draw(style::kHeading, x, inner.y + (band - text_.LineHeight(style::kHeading)) * 0.5f,
               text_.Fit(style::kHeading, v.title, w, false), colour::kInk);
    float y = inner.y + band + 8;
    text_.Draw(style::kSmall, x, y, text_.Fit(style::kSmall, v.game, w, false), colour::kInkSoft);
    y += text_.LineHeight(style::kSmall) + 8;

    const float badgeR = 15;
    DrawBadge(v.badge, x + badgeR, y + badgeR, badgeR, v.fraction);
    float tx = x + (v.badge == Badge::None ? 0 : 2 * badgeR + 10), tw = inner.Right() - pad - tx;
    // A narrow tile sets the state a size smaller before it shortens it.
    Style state = text_.Width(style::kBodyBold, v.state) <= tw ? style::kBodyBold : style::kSmallBold;
    text_.Draw(state, tx, y + (text_.LineHeight(style::kBodyBold) - text_.LineHeight(state)) * 0.5f,
               text_.Fit(state, v.state, tw, false), colour::kInk);
    y += text_.LineHeight(style::kBodyBold) + 1;
    y += text_.DrawWrapped(style::kSmall, tx, y, tw, v.detail, colour::kInkSoft);

    // The bar repeats the badge's ring, so it gives way when the words need its room.
    Rect bar{x, inner.Bottom() - pad - 8, w, 8};
    if (v.badge == Badge::Partial && y + 4 <= bar.y) {
        canvas_.FillRound(bar, 4, colour::kYellowSoft);
        if (v.fraction > 0)
            canvas_.FillRound({bar.x, bar.y, std::max(bar.h, bar.w * Clamp01(v.fraction)), bar.h}, 4, colour::kOrange);
    }
}

void Ui::ProgressBar(const Rect& r, float fraction) {
    canvas_.Outlined(r, r.h * 0.5f, 2.5f, colour::kPaper, colour::kInk);
    Rect inner = r.Inset(5);
    Rect fill;
    if (fraction < 0) {
        float w = inner.w * 0.25f;
        float t = 0.5f + 0.5f * std::sin(static_cast<float>(time_) * 2.4f);
        fill = {inner.x + (inner.w - w) * t, inner.y, w, inner.h};
        animating_ = true;
    } else {
        fill = {inner.x, inner.y, std::max(inner.h, inner.w * Clamp01(fraction)), inner.h};
        if (fraction <= 0) return;
    }
    canvas_.FillRoundGradient(fill, inner.h * 0.5f, colour::kLimeBright, colour::kLime);
    Rect shine = fill.Inset(inner.h * 0.18f, inner.h * 0.14f);
    shine.h *= 0.4f;
    canvas_.FillRound(shine, shine.h * 0.5f, colour::kPaper.WithAlpha(110));
}

void Ui::LogList(const std::string& id, const Rect& r, const std::vector<LogEntry>& entries) {
    canvas_.Outlined(r, 12, 2.5f, colour::kPaper, colour::kInk);
    Rect inner = r.Inset(10, 8);
    float rowH = text_.LineHeight(style::kSmall) + 5;
    int visible = std::max(1, static_cast<int>(inner.h / rowH));
    int n = static_cast<int>(entries.size());
    float maxTop = static_cast<float>(std::max(0, n - visible));
    float& top = scroll_[id];
    auto followIt = follow_.find(id);
    if (followIt == follow_.end()) followIt = follow_.emplace(id, true).first;
    if (interactive_ && Hovered(r) && input_.wheel != 0) {
        top -= input_.wheel * 3;
        followIt->second = top >= maxTop;
    }
    if (followIt->second) top = maxTop;
    top = std::clamp(std::round(top), 0.0f, maxTop);

    if (entries.empty()) {
        text_.DrawCentred(style::kSmall, inner, "Nothing checked yet", colour::kInkSoft);
        return;
    }
    Clip(inner);
    int first = static_cast<int>(top);
    float scrollW = n > visible ? 10.0f : 0.0f;
    for (int i = first; i < n && i <= first + visible; ++i) {
        float y = inner.y + (i - top) * rowH;
        if (i % 2) canvas_.FillRound({inner.x, y, inner.w - scrollW, rowH}, 5, colour::kCream);
        const LogEntry& e = entries[static_cast<size_t>(i)];
        float statusW = text_.Width(style::kSmallBold, e.status);
        float pathW = inner.w - scrollW - statusW - 24;
        text_.Draw(style::kSmall, inner.x + 6, y + 2.5f, text_.Fit(style::kSmall, e.path, pathW, true), colour::kInk);
        text_.Draw(style::kSmallBold, inner.Right() - scrollW - 6 - statusW, y + 2.5f, e.status, e.colour);
    }
    Unclip();
    ScrollBar({inner.Right() - 6, inner.y, 6, inner.h}, static_cast<float>(visible), static_cast<float>(n), top,
              colour::kCream);
}

bool Ui::Toggle(const std::string& id, const Rect& r, bool& on, bool enabled) {
    bool flipped = Clicked(id, r, enabled);
    if (flipped) on = !on;
    float k = Spring(id, on ? 1.0f : 0.0f);
    float hover = Spring(id + "#hover", interactive_ && enabled && Hovered(r) ? 1.0f : 0.0f);
    Rgba track = enabled ? Mix(colour::kGrey, colour::kLime, Clamp01(k)) : Mix(colour::kGrey, colour::kPaper, 0.5f);
    Rgba line = enabled ? colour::kInk : colour::kInkSoft;
    canvas_.Outlined(r, r.h * 0.5f, 2.5f, track, line);
    float knobR = r.h * 0.5f - 5 + 1.0f * hover;
    float cx = r.x + r.h * 0.5f + (r.w - r.h) * k;
    canvas_.FillCircle(cx, r.CentreY(), knobR + 2.5f, line);
    canvas_.FillCircle(cx, r.CentreY(), knobR, colour::kPaper);
    return flipped;
}

bool Ui::Segmented(const std::string& id, const Rect& r, const std::vector<std::string>& options,
                   const std::vector<bool>& enabled, int& selected) {
    int n = static_cast<int>(options.size());
    if (n == 0) return false;
    canvas_.Shadow(r.Moved(0, 3), r.h * 0.5f, 6, colour::kShadow);
    canvas_.Outlined(r, r.h * 0.5f, metric::kOutline, colour::kCream, colour::kInk);
    Rect inner = r.Inset(metric::kOutline + 3);
    float cellW = inner.w / n;
    bool changed = false;
    for (int i = 0; i < n; ++i) {
        Rect cell{inner.x + cellW * i, inner.y, cellW, inner.h};
        bool on = i < static_cast<int>(enabled.size()) ? enabled[static_cast<size_t>(i)] : true;
        if (Clicked(id + "/" + std::to_string(i), cell, on) && selected != i) {
            selected = i;
            changed = true;
        }
        if (on && i != selected && interactive_ && Hovered(cell))
            canvas_.FillRound(cell, cell.h * 0.5f, colour::kYellowSoft);
    }
    float pos = Spring(id + "#sel", static_cast<float>(std::clamp(selected, 0, n - 1)));
    Rect pill{inner.x + cellW * pos, inner.y, cellW, inner.h};
    canvas_.OutlinedGradient(pill, pill.h * 0.5f, 2.5f, colour::kLimeBright, colour::kLime, colour::kInk);
    for (int i = 0; i < n; ++i) {
        Rect cell{inner.x + cellW * i, inner.y, cellW, inner.h};
        bool on = i < static_cast<int>(enabled.size()) ? enabled[static_cast<size_t>(i)] : true;
        std::string label = text_.Fit(style::kButton, options[static_cast<size_t>(i)], cell.w - 16, false);
        text_.DrawCentred(style::kButton, cell, label, on ? colour::kInk : colour::kGrey);
    }
    return changed;
}

void Ui::DropField(const std::string& id, const Rect& r, bool dropHover, float iconTop) {
    float k = Clamp01(Spring(id, dropHover ? 1.0f : 0.0f));
    const float radius = 22;
    canvas_.Shadow(r.Moved(0, metric::kShadowDrop), radius, metric::kShadowBlur, colour::kShadow);
    canvas_.Outlined(r, radius, metric::kOutline, Mix(colour::kCream, colour::kYellowSoft, k),
                     Mix(colour::kInk, colour::kLimeDeep, k));
    canvas_.DashedRound(r.Inset(9), radius - 6, 3, 14, 9, Mix(colour::kOrange, colour::kLimeDeep, k));

    // The icon: a tray with an arrow dropping into it, bouncing while a drag is over the field.
    float size = 44, cx = r.CentreX();
    float cy = iconTop + size * 0.5f + 4 * k * std::sin(static_cast<float>(time_) * 6.0f);
    if (dropHover) animating_ = true;
    float trayY = iconTop + size * 0.72f;
    canvas_.Polyline({{cx - size * 0.5f, trayY - size * 0.12f},
                      {cx - size * 0.5f, trayY + size * 0.18f},
                      {cx + size * 0.5f, trayY + size * 0.18f},
                      {cx + size * 0.5f, trayY - size * 0.12f}},
                     5, colour::kInk);
    float ay = cy - size * 0.55f;
    canvas_.Polyline({{cx, ay}, {cx, cy + size * 0.02f}}, 6, Mix(colour::kOrangeDeep, colour::kLimeDeep, k));
    canvas_.Polyline({{cx - size * 0.2f, cy - size * 0.16f}, {cx, cy + size * 0.06f}, {cx + size * 0.2f,
                      cy - size * 0.16f}},
                     6, Mix(colour::kOrangeDeep, colour::kLimeDeep, k));
}

void Ui::Notice(const Rect& window, std::string_view message, Rgba fill) {
    float w = std::min(window.w - 2 * metric::kMargin, text_.Width(style::kBodyBold, message) + 40);
    Rect r{window.CentreX() - w * 0.5f, window.Bottom() - metric::kMargin - 36, w, 36};
    canvas_.Shadow(r.Moved(0, 3), 18, 8, colour::kShadow);
    canvas_.Outlined(r, 18, 2.5f, fill, colour::kInk);
    text_.DrawCentred(style::kBodyBold, r, text_.Fit(style::kBodyBold, message, w - 32, false), colour::kInk);
}

}  // namespace tpw::ui
