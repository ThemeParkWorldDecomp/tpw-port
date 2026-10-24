// widgets.h: the launcher screen's widgets, immediate mode: each frame the screen code calls them in drawing order and
// they draw themselves and report clicks. About eight kinds: panel, button, tile, progress bar, log list, toggle,
// segmented switch and the drop field, plus badges, a notice and what scrolling needs (an offset the wheel moves, a
// clip and a bar). Hover and press move through damped springs, which gives buttons and tiles their bouncy hover.
// Everything is in points (draw.h).
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "draw.h"
#include "text.h"

namespace tpw::ui {

// The pointer, as the screen code collected it from SDL events since the last frame.
struct Input {
    float mouseX = -1e6f, mouseY = -1e6f;
    bool down = false;      // the left button is held
    bool pressed = false;   // it went down since the last frame
    bool released = false;  // it went up since the last frame
    float wheel = 0;        // lines scrolled since the last frame (positive: towards the top)
};

enum class ButtonKind {
    Go,      // lime: the step forward (Play, the check that unlocks)
    Plain,   // yellow
    Quiet,   // cream: Close, Back
    Danger,  // red: Cancel, Remove
};

enum class Badge { None, Lock, Partial, Check };

// What an edition tile shows (texts.h writes the words).
struct TileView {
    std::string title;      // "European edition"
    std::string game;       // "Sim Theme Park, Mac CD", "Theme Park World, PC CD"
    std::string state;      // "Ready to play", "715 of 845 confirmed", ...
    std::string detail;     // "Drag in the US Mac CD to unlock", ...
    Rgba accent;            // the tile's header band
    Badge badge = Badge::None;
    float fraction = 0;     // files confirmed, for the partial badge and bar
    bool highlight = false; // a drag is over the tile
};

struct LogEntry {
    std::string path;
    std::string status;
    Rgba colour;  // of the status
};

class Ui {
public:
    Ui(Canvas& canvas, Text& text) : canvas_(canvas), text_(text) {}
    Canvas& canvas() { return canvas_; }
    Text& text() { return text_; }

    // Starts a frame: `dt` seconds since the last one.
    void Begin(float dt, const Input& input);
    void End();
    double Time() const { return time_; }
    // False while an overlay covers the widgets being drawn, or while a drag is over the window: they draw but do not
    // react to the pointer.
    void SetInteractive(bool on) { interactive_ = on; }
    bool Interactive() const { return interactive_; }
    // The pointer is over `r` (and inside the clip, while there is one).
    bool Hovered(const Rect& r) const;
    // True while any spring is still moving (the window keeps redrawing until everything rests).
    bool Animating() const { return animating_; }

    // A damped spring per id, moving towards `target`; returns where it is now. Underdamped, so it overshoots a little.
    float Spring(const std::string& id, float target);

    void Panel(const Rect& r, Rgba top, Rgba bottom, float radius = metric::kRadius);
    bool Button(const std::string& id, const Rect& r, std::string_view label, ButtonKind kind, bool enabled = true,
                Style labelStyle = style::kButton);
    float ButtonWidth(std::string_view label) const;   // a comfortable width for the label
    void Tile(const std::string& id, const Rect& r, const TileView& view);
    // `fraction` < 0: busy without a known amount (a block sweeps back and forth).
    void ProgressBar(const Rect& r, float fraction);
    // Rows of path and status; keeps the newest row in view unless the player scrolled up.
    void LogList(const std::string& id, const Rect& r, const std::vector<LogEntry>& entries);
    // A switch `r` (about 52 x 30); returns true when it was flipped.
    bool Toggle(const std::string& id, const Rect& r, bool& on, bool enabled = true);
    // Side by side choices; returns true when the selection changed.
    bool Segmented(const std::string& id, const Rect& r, const std::vector<std::string>& options,
                   const std::vector<bool>& enabled, int& selected);
    // The big dashed field the player drops discs on, its icon's top at `iconTop`; `dropHover` while a drag is over it.
    void DropField(const std::string& id, const Rect& r, bool dropHover, float iconTop);
    void DrawBadge(Badge badge, float cx, float cy, float radius, float fraction);
    // A message in a pill at the bottom of the window.
    void Notice(const Rect& window, std::string_view message, Rgba fill);

    // Content taller than its view: how far it is scrolled, in points from its top (0 to `maxOffset`). The wheel over
    // `area` moves it `step` points a line. Content not drawn the frame before (a box opened again) starts at its top.
    float Scroll(const std::string& id, const Rect& area, float step, float maxOffset);
    // Drawing, and the pointer the widgets see, kept inside `r` until Unclip.
    void Clip(const Rect& r);
    void Unclip();
    // A bar in `track` showing where the view is: `shown` of `total` in view, scrolled `offset` (all in one unit).
    void ScrollBar(const Rect& track, float shown, float total, float offset, Rgba trackColour);

private:
    struct SpringState {
        float x = 0, v = 0;
        uint64_t frame = 0;
        bool seen = false;
    };
    struct ScrollState {
        float offset = 0;
        uint64_t frame = 0;   // the frame it was last drawn in
    };
    // True when the press and the release both landed on `r` while interactive.
    bool Clicked(const std::string& id, const Rect& r, bool enabled);

    Canvas& canvas_;
    Text& text_;
    Input input_;
    float dt_ = 0;
    double time_ = 0;
    uint64_t frame_ = 0;
    bool interactive_ = true;
    bool animating_ = false;
    bool clipped_ = false;
    Rect clip_;            // while clipped_
    std::string active_;   // the widget the button went down on
    std::unordered_map<std::string, SpringState> springs_;
    std::unordered_map<std::string, float> scroll_;   // log lists: rows hidden above the top
    std::unordered_map<std::string, bool> follow_;    // log lists: keep the newest row in view
    std::unordered_map<std::string, ScrollState> offsets_;   // Scroll: points hidden above the top
};

}  // namespace tpw::ui
