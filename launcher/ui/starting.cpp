// starting.cpp: the launcher's Starting screen (launcher_ui.h): after Play the window stays open while the game starts,
// the screen Play was pressed on dimmed under a box that names the game and shows how far its start has got. The bar's
// amount comes from the platform layer (platform/handoff.h): bytes read against the total of the last start, or a bar
// moving without a number when there is none yet.
#include <algorithm>
#include <cmath>
#include <string>

#include "launcher_ui.h"
#include "theme.h"

namespace tpw::ui {

void LauncherUi::BeginStarting(assets::Edition edition, SDL_Texture* behind) {
    if (screen_ != Screen::Starting) startingFrom_ = screen_;
    screen_ = Screen::Starting;
    startingEdition_ = edition;
    startingBehind_ = behind;
    aboutOpen_ = settingsOpen_ = false;
    dragging_ = false;
}

void LauncherUi::FrameStarting(float dt, float width, float height, float scale) {
    canvas_.Begin(scale);
    Rect window{0, 0, width, height};
    ui_.Begin(dt, Input{});   // nothing on it reacts to the pointer
    ui_.SetInteractive(false);
    tilesShown_ = false;
    dropOverlay_ = Rect{};
    if (!startingBehind_) DrawBackdrop(window);
    DrawStarting(window);
    ui_.End();
}

void LauncherUi::DrawStarting(const Rect& w) {
    if (startingBehind_) {
        const SDL_FRect all{w.x, w.y, w.w, w.h};
        SDL_RenderTexture(canvas_.Renderer(), startingBehind_, nullptr, &all);
    } else {
        // The screen Play was pressed on, as it was, but not reacting.
        const bool interactive = ui_.Interactive();
        ui_.SetInteractive(false);
        switch (startingFrom_) {
            case Screen::Home: DrawHome(w); break;
            case Screen::Checking: DrawChecking(w); break;
            case Screen::Result: DrawResult(w); break;
            case Screen::Play: DrawPlay(w); break;
            case Screen::Starting: break;
        }
        ui_.SetInteractive(interactive);
        tilesShown_ = false;
    }
    canvas_.FillRound(w, 0, colour::kDim);

    // "Starting Theme Park World", the bar, and how far it is ("42%"), or what it does when there is no total yet.
    std::string game = EditionGame(startingEdition_);
    game = game.substr(0, game.find(','));
    const std::string title = "Starting " + game;
    const float barH = 30;
    const float titleH = text_.LineHeight(style::kHeadline), lineH = text_.LineHeight(style::kBodyBold);
    const float pw = std::min(w.w - 2 * metric::kMargin, 520.0f);
    const float ph = titleH + 18 + barH + 12 + lineH + 48;
    const Rect panel{w.CentreX() - pw * 0.5f, w.CentreY() - ph * 0.5f, pw, ph};
    ui_.Panel(panel, colour::kPaper, colour::kCream);
    const Rect in = panel.Inset(24);
    float y = in.y;
    text_.DrawCentred(style::kHeadline, {in.x, y, in.w, titleH}, text_.Fit(style::kHeadline, title, in.w, false),
                      colour::kInk);
    y += titleH + 18;
    const bool known = startingFraction_ >= 0;
    ui_.ProgressBar({in.x, y, in.w, barH}, known ? std::min(startingFraction_, 1.0f) : -1.0f);
    y += barH + 12;
    const std::string line =
        known ? std::to_string(static_cast<int>(std::floor(std::min(startingFraction_, 1.0f) * 100.0f))) + "%"
              : std::string("Loading the game's files");
    text_.DrawCentred(style::kBodyBold, {in.x, y, in.w, lineH}, line, colour::kInk);
}

}  // namespace tpw::ui
