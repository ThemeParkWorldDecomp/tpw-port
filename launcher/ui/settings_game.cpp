// settings_game.cpp: the Settings box's Game options section (launcher_ui.h): every setting of the game's Options panel, by
// the names and with the ranges the panel has, shown from and written to the game's own files (game_settings.h). A row
// changes one setting in the file at once; the game reads the files when it starts and when a player logs in, and while
// it runs the rows are off, since the Options panel has them then.
#include <algorithm>
#include <string>
#include <vector>

#include "launcher_ui.h"
#include "theme.h"

namespace tpw::ui {

namespace {

// The rows of the section: the edition's, Screen resolution's two among them, the player's row, and the player's.
constexpr int kConfigRows = 6;
constexpr int kPlayerRows = 11;

}  // namespace

void LauncherUi::SetGameSettings(const GameSettingsSource& source) {
    gameShown_ = true;
    gameSource_ = source;
}

void LauncherUi::RefreshGameSettings() {
    gameMessage_.clear();
    if (!gameShown_) return;
    if (gameEdition_ < 0) gameEdition_ = playIndex_;
    gameView_ = ReadGameSettings(gameSource_.userFolder(gameEdition_), gameSource_.soundSam(gameEdition_), gamePlayer_);
    int shape = 0;
    gameSizeOpened_ = gameSource_.sizes.chosen(gameView_.config.resolution, shape);
    gameResolutionOpened_ = gameView_.config.resolution;
}

void LauncherUi::WriteGameSetting(GameOption option, int value) {
    std::string err;
    const bool kept = WriteGameOption(gameSource_.userFolder(gameEdition_), gameSource_.soundSam(gameEdition_), gamePlayer_,
                                      option, value, err);
    const GameSize opened = gameSizeOpened_;
    const int openedNumber = gameResolutionOpened_;
    RefreshGameSettings();
    gameSizeOpened_ = opened;
    gameResolutionOpened_ = openedNumber;
    if (!kept) gameMessage_ = "Cannot keep the setting: " + err;
}

float LauncherUi::GameOptionsHeight(float rowH) {
    return text_.LineHeight(style::kBodyBold) + 4 + text_.LineHeight(style::kSmall) + 6 + rowH +
           text_.LineHeight(style::kSmallBold) + 4 + kConfigRows * rowH + text_.LineHeight(style::kSmallBold) + 4 +
           (1 + kPlayerRows) * rowH;
}

void LauncherUi::DrawGameOptions(const Rect& in, float& y, float rowH) {
    const GameSettingsView& view = gameView_;
    const bool hasPlayer = view.player >= 0;
    const std::string editionName = gameEdition_ == 0 ? "Sim Theme Park" : "Theme Park World";
    const std::string playerName = hasPlayer ? view.players[static_cast<size_t>(view.player)].name : std::string();

    // What can be changed now: nothing while the game has the files, nothing of a file that does not read, and without
    // Config.tcf only when the edition's defaults are known, the values a new file starts from.
    const bool free = !view.running;
    const bool configOk = free && (view.configState == GameFileState::Ok ||
                                   (view.configState == GameFileState::Missing && view.defaultsKnown));
    const bool playerOk = free && hasPlayer && view.playerState == GameFileState::Ok;
    const bool movieOk = configOk && (!hasPlayer || playerOk);

    text_.Draw(style::kBodyBold, in.x, y, "Game options", colour::kInk);
    y += text_.LineHeight(style::kBodyBold) + 4;
    std::string status = "The game's Options panel, kept in the game's own files. The game reads them when it starts";
    bool problem = false;
    if (!gameMessage_.empty()) {
        status = gameMessage_;
        problem = true;
    } else if (view.running) {
        status = "The game is running: change these in its Options. They show here when it quits";
    } else if (view.configState == GameFileState::Damaged) {
        status = "Config.tcf cannot be read, so it is left as it is. The game writes a new one when it next saves";
        problem = true;
    } else if (view.configState == GameFileState::Missing && !view.defaultsKnown) {
        status = "Add this edition's game data to change its options";
    }
    text_.Draw(style::kSmall, in.x, y, text_.Fit(style::kSmall, status, in.w, false), problem ? colour::kRed : colour::kInkSoft);
    y += text_.LineHeight(style::kSmall) + 6;

    int shown = 0;
    // A row's stripes, title and line, leaving `controlW` at the right for its controls; returns the row.
    auto rowOf = [&](const std::string& title, const std::string& line, float controlW) {
        Rect row{in.x, y, in.w, rowH - 6};
        canvas_.FillRound(row, 10, shown++ % 2 ? colour::kCream : colour::kYellowSoft.WithAlpha(120));
        float ty = row.y + (row.h - text_.LineHeight(style::kBodyBold) - text_.LineHeight(style::kSmall)) * 0.5f;
        text_.Draw(style::kBodyBold, row.x + 12, ty, text_.Fit(style::kBodyBold, title, row.w - 12 - controlW - 24, false),
                   colour::kInk);
        text_.Draw(style::kSmall, row.x + 12, ty + text_.LineHeight(style::kBodyBold),
                   text_.Fit(style::kSmall, line, row.w - 12 - controlW - 24, false), colour::kInkSoft);
        y += rowH;
        return row;
    };
    auto rectOf = [&](GameOption option) -> Rect& { return gameRects_[static_cast<int>(option)]; };
    // A row of side by side choices: `values[i]` is what the file holds for choice i. A file's value that is not one of them
    // shows the nearest it can, and stays in the file until a choice is made.
    auto choices = [&](GameOption option, const std::string& id, const std::string& title, const std::string& line,
                       const std::vector<std::string>& names, const std::vector<int>& values, int value,
                       const std::vector<bool>& enabled) {
        float segW = 0;
        for (const std::string& name : names) segW = std::max(segW, text_.Width(style::kButton, name) + 36);
        segW = std::max(150.0f, segW * static_cast<float>(names.size()));
        const Rect row = rowOf(title, line, segW);
        Rect& rect = rectOf(option);
        rect = {row.Right() - 12 - segW, row.CentreY() - 18, segW, 36};
        int choice = 0;
        for (size_t i = 0; i < values.size(); ++i)
            if (values[i] == value) choice = static_cast<int>(i);
        if (ui_.Segmented(id, rect, names, enabled, choice)) WriteGameSetting(option, values[static_cast<size_t>(choice)]);
    };
    // A row of one on and off.
    auto toggle = [&](GameOption option, const std::string& id, const std::string& title, const std::string& line, bool value) {
        const Rect row = rowOf(title, line, 56);
        Rect& rect = rectOf(option);
        rect = {row.Right() - 12 - 56, row.CentreY() - 16, 56, 32};
        bool on = value;
        if (ui_.Toggle(id, rect, on, playerOk)) WriteGameSetting(option, on ? 1 : 0);
    };
    // A number from 0 to 100, on a slider that follows the pointer and is kept when let go. `onOption` not Count: with its
    // own on and off at the right.
    auto slider = [&](GameOption option, GameOption onOption, const std::string& id, const std::string& title,
                      const std::string& line, int& value, bool on, bool enabled) {
        const bool hasSwitch = onOption != GameOption::Count;
        const float switchW = hasSwitch ? 56 + 12 : 0, sliderW = 170, valueW = 44;
        const Rect row = rowOf(title, line, switchW + sliderW + valueW);
        Rect& rect = rectOf(option);
        rect = {row.Right() - 12 - switchW - sliderW, row.CentreY() - 18, sliderW, 36};
        int position = std::clamp(value, 0, 100);
        const bool kept = ui_.Slider(id, rect, position, 0, 100, enabled);
        if (position != std::clamp(value, 0, 100)) value = position;
        const std::string number = std::to_string(position);
        text_.Draw(style::kButton, rect.x - 8 - text_.Width(style::kButton, number),
                   row.CentreY() - text_.LineHeight(style::kButton) * 0.5f, number, colour::kInk);
        if (hasSwitch) {
            Rect& switchRect = rectOf(onOption);
            switchRect = {row.Right() - 12 - 56, row.CentreY() - 16, 56, 32};
            bool now = on;
            if (ui_.Toggle(id + "-on", switchRect, now, enabled)) {
                WriteGameSetting(onOption, now ? 1 : 0);
                return;
            }
        }
        if (kept) WriteGameSetting(option, position);
    };

    {   // The files are the edition's: its own save folder.
        const float segW = 2 * (text_.Width(style::kButton, "Theme Park World") + 36);
        const Rect row = rowOf("Edition", "The edition these options are for", segW);
        gameEditionRect_ = {row.Right() - 12 - segW, row.CentreY() - 18, segW, 36};
        static const std::vector<std::string> kNames = {"Sim Theme Park", "Theme Park World"};
        int choice = gameEdition_;
        if (ui_.Segmented("game-edition", gameEditionRect_, kNames, {true, true}, choice)) {
            gameEdition_ = choice;
            gamePlayer_.clear();
            gamePlayerOpen_ = false;
            RefreshGameSettings();
        }
    }

    text_.Draw(style::kSmallBold, in.x, y, "For every player of " + editionName, colour::kInk);
    y += text_.LineHeight(style::kSmallBold) + 4;
    const std::vector<bool> configChoices2(2, configOk);

    // Screen resolution: the game's own stops and shapes (GameSizeRules). The slider moves over the stops of the shape, and
    // the number saved is the panel's: the size's own, or the one the file held when the size is the one it held.
    const GameSizeRules& sizes = gameSource_.sizes;
    auto numberOf = [&](GameSize size) { return size == gameSizeOpened_ ? gameResolutionOpened_ : sizes.setting(size); };
    {
        int shape = 0;
        const GameSize size = sizes.chosen(view.config.resolution, shape);
        const std::vector<GameSize> stops = sizes.stops(shape, gameSizeOpened_);
        const int stop = sizes.closest(stops, size);
        const float sliderW = 200, valueW = 110;
        const Rect row = rowOf("Screen resolution", "The size the game draws at", sliderW + valueW);
        Rect& rect = rectOf(GameOption::Resolution);
        rect = {row.Right() - 12 - sliderW, row.CentreY() - 18, sliderW, 36};
        int position = stop;
        const bool kept = ui_.Slider("game-resolution", rect, position, 0, static_cast<int>(stops.size()) - 1, configOk);
        const GameSize stopSize = stops[static_cast<size_t>(position)];
        if (position != stop) gameView_.config.resolution = numberOf(stopSize);
        const std::string shown = std::to_string(stopSize.width) + " x " + std::to_string(stopSize.height);
        text_.Draw(style::kButton, rect.x - 8 - text_.Width(style::kButton, shown),
                   row.CentreY() - text_.LineHeight(style::kButton) * 0.5f, shown, colour::kInk);
        if (kept) WriteGameSetting(GameOption::Resolution, numberOf(stopSize));
    }
    {
        int shape = 0;
        const GameSize size = sizes.chosen(view.config.resolution, shape);
        const float segW = 240;
        const Rect row = rowOf("Screen shape", "The shape of Screen resolution's picture", segW);
        gameShapeRect_ = {row.Right() - 12 - segW, row.CentreY() - 18, segW, 36};
        int choice = shape;
        if (ui_.Segmented("game-shape", gameShapeRect_, sizes.shapes, std::vector<bool>(sizes.shapes.size(), configOk), choice)) {
            const std::vector<GameSize> next = sizes.stops(choice, gameSizeOpened_);
            WriteGameSetting(GameOption::Resolution, numberOf(next[static_cast<size_t>(sizes.closest(next, size))]));
        }
    }
    // Software rendering has no High, as the panel's slider does not reach it there (DoScroll).
    choices(GameOption::GraphicsQuality, "game-detail", "Graphics quality", "Default in Graphics below follows this",
            {"Low", "Medium", "High"}, {0, 1, 2}, view.config.detail, {configOk, configOk, configOk && view.config.renderer != 0});
    choices(GameOption::Rendering, "game-rendering", "Rendering", "For the game's next start",
            {"3D card rendering", "Software rendering"}, {1, 0}, view.config.renderer, configChoices2);
    choices(GameOption::Videocard, "game-videocard", "Videocard", "This system gives the game one device",
            {"Primary", "Secondary"}, {0, 1}, view.config.videoCard, {false, false});
    slider(GameOption::AudioQuality, GameOption::Count, "game-audio", "Audio quality", "From the lowest to the highest quality",
           gameView_.config.soundQuality, true, configOk);

    const bool someone = hasPlayer;
    text_.Draw(style::kSmallBold, in.x, y,
               text_.Fit(style::kSmallBold, someone ? "For " + playerName + " (the game keeps these for each player)"
                                                    : "For each player",
                         in.w, false),
               colour::kInk);
    y += text_.LineHeight(style::kSmallBold) + 4;
    {
        const float fieldW = 260;
        std::string line = "Create a player in the game first";
        if (view.playerState == GameFileState::Damaged) line = playerName + "'s file cannot be read, so it is left as it is";
        else if (someone) line = view.players.size() > 1 ? "Starts on the last one played" : "The edition's only player";
        const Rect row = rowOf("Player", line, fieldW);
        gamePlayerRect_ = {row.Right() - 12 - fieldW, row.CentreY() - 18, fieldW, 36};
        if (ui_.Dropdown("game-player", gamePlayerRect_, someone ? playerName : "No player yet", gamePlayerOpen_,
                         view.players.size() > 1))
            gamePlayerOpen_ = true;
    }
    slider(GameOption::SfxVolume, GameOption::SfxOn, "game-sfx", "Sound effects volume", "The park's sounds",
           gameView_.options.sfx.volume, view.options.sfx.on, playerOk);
    slider(GameOption::MusicVolume, GameOption::MusicOn, "game-music", "Music volume", "The park's music",
           gameView_.options.music.volume, view.options.music.on, playerOk);
    slider(GameOption::SpeechVolume, GameOption::SpeechOn, "game-speech", "Speech volume", "The advisor's voice",
           gameView_.options.speech.volume, view.options.speech.on, playerOk);
    // The movie volume is also the game's own until a player logs in, so with no player it is Config.tcf's alone.
    GameVolume& movie = hasPlayer ? gameView_.options.movie : gameView_.config.movie;
    slider(GameOption::MovieVolume, GameOption::MovieOn, "game-movie", "Movie volume", "Also the one before a player logs in",
           movie.volume, movie.on, movieOk);
    toggle(GameOption::Advisor, "game-advisor", "Advisor", "The advisor's messages", view.options.advisor);
    toggle(GameOption::Tutorial, "game-tutorial", "Tutorial", "The tutorial's messages", view.options.tutorial);
    toggle(GameOption::PopupHelp, "game-popup", "Popup help", "Help under the pointer", view.options.tooltips);
    toggle(GameOption::Confirmations, "game-confirm", "Confirmations", "Asks first before something is deleted",
           view.options.confirmDelete);
    toggle(GameOption::RmbCancel, "game-rmb-cancel", "RMB cancel", "The right mouse button cancels", view.options.rmbCancel);
    const std::vector<bool> playerChoices(2, playerOk);
    choices(GameOption::Rotation, "game-rotation", "Rotation", "How the camera turns", {"Smooth", "90 degs"}, {0, 1},
            view.options.isometric ? 1 : 0, playerChoices);
    choices(GameOption::Scroll, "game-scroll", "Scroll", "How the camera is moved", {"Pushscroll", "Right button"}, {0, 1},
            view.options.rmbScroll ? 1 : 0, playerChoices);
}

void LauncherUi::DrawGamePlayerList(const Rect& w) {
    std::vector<std::string> names;
    for (const GamePlayer& player : gameView_.players) names.push_back(player.name);
    int choice = gameView_.player;
    bool close = false;
    if (ui_.DropList("game-player-list", gamePlayerRect_, w.Inset(8), names, choice, close, &gamePlayerRows_)) {
        gamePlayer_ = names[static_cast<size_t>(choice)];
        RefreshGameSettings();
    }
    if (close) gamePlayerOpen_ = false;
}

}  // namespace tpw::ui
