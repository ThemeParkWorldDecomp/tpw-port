// launcher_ui.h: the launcher's screens (launcher-design REPORT section 5): the drop field with its Browse buttons and
// the three edition tiles (once all three editions are confirmed, two large tiles to play from instead); the checking log with its counter and progress bar; the result; Play with the edition
// switch and the Gold toggles; and the Settings and About boxes. Driven by SDL3 events: mouse, wheel, Escape and the
// drop events (DROP_BEGIN, DROP_POSITION, DROP_FILE, DROP_COMPLETE; DROP_TEXT is ignored). The window loop is
// window.h's; the screenshot test drives this class on a software renderer with a scripted backend.
#pragma once
#include <SDL3/SDL.h>

#include <future>
#include <memory>
#include <string>
#include <vector>

#include "backend.h"
#include "draw.h"
#include "text.h"
#include "texts.h"
#include "versions.h"
#include "widgets.h"

namespace tpw::ui {

// launcher.ini's key for the Settings box's "Skip the intro films" ("1" skips them).
inline constexpr const char* kSkipIntroKey = "skip_intro";
// launcher.ini's key for the Settings box's "Back to the launcher after playing" ("1": the launcher opens again when the
// game quits).
inline constexpr const char* kBackToLauncherKey = "back_to_launcher";
// launcher.ini's key for the Settings box's "Smooth wheel zoom" ("1": the wheel eases the zoom toward the pointer; absent
// or "0": the original's step toward the middle of the screen).
inline constexpr const char* kSmoothZoomKey = "smooth_wheel_zoom";
// launcher.ini's keys for the Park rules box: "Visitors arriving" (normal, more or many: each bus brings 1, 1.5 or 2 times
// the people) and "Visitor limit" (1500, 3000 or 5000 in an offline park). Absent: normal and 1500, as the original.
inline constexpr const char* kVisitorArrivalsKey = "visitor_arrivals";
inline constexpr const char* kVisitorLimitKey = "visitor_limit";

// What the player chose to start.
struct LaunchChoice {
    assets::Edition edition = assets::kEditionEur;
    bool gold = false;        // with the Gold bonus rides
    bool skipIntro = false;   // without the logo and intro films (Settings)
    bool backToLauncher = false;   // the launcher opens again when the game quits (Settings)
};

class LauncherUi {
public:
    enum class Screen { Home, Checking, Result, Play, Starting };

    LauncherUi(SDL_Renderer* renderer, ImportBackend& backend);
    ~LauncherUi();
    LauncherUi(const LauncherUi&) = delete;
    LauncherUi& operator=(const LauncherUi&) = delete;

    // False when a font could not be read (Error() says why).
    bool Ok() const { return text_.Ok(); }
    std::string Error() const { return text_.Error(); }

    // The window the file dialogs belong to (none in the test: Browse then says so).
    void SetWindow(SDL_Window* window) { window_ = window; }
    // Pointer coordinates are the window's, in points.
    void HandleEvent(const SDL_Event& event);
    // Updates and draws one frame of `width` x `height` points at `scale` pixels per point. Does not present.
    void Frame(float dt, float width, float height, float scale);
    // True while something moves (a spring, an import, a notice): the window loop keeps drawing.
    bool Animating() const;
    // True once, after the Play button: the choice to start.
    bool TakePlay(LaunchChoice& out);
    // After Play (launcher/ui/starting.cpp): the Starting screen, the screen Play was pressed on dimmed under a box with
    // a progress bar. `behind`: a picture of that screen to show instead of drawing it again (the window's, which keeps
    // drawing after its backend is gone); not owned, kept until the Starting screen ends.
    void BeginStarting(assets::Edition edition, SDL_Texture* behind = nullptr);
    // The bar's fraction of 1 (below 0: it moves without a number).
    void SetStartingProgress(float fraction) { startingFraction_ = fraction; }
    // One frame of the Starting screen without asking the backend anything (Frame does). Does not present.
    void FrameStarting(float dt, float width, float height, float scale);

    // Paths to import, as if dropped (Browse results arrive here too).
    void OfferPaths(const std::vector<std::string>& paths);

    // For the screenshot test.
    Screen CurrentScreen() const { return screen_; }
    void ShowScreen(Screen screen);
    void ShowAbout(bool open);
    // The About box's "Made with" rows (versions.h).
    std::vector<MadeWith> AboutRows() const { return MadeWithRows(text_); }
    void ShowSettings(bool open);
    void ArmRemove(assets::Edition edition);
    void ShowNotice(const std::string& message, bool problem);
    // Play's edition switch and Gold toggle, as if clicked.
    void ChoosePlay(assets::Edition edition, bool gold);
    // Where the last frame drew the drop targets, in points (an empty rect when not shown). Index 0 US, 1 European,
    // 2 Gold (empty on the all-confirmed home, whose two tiles are the US and European ones).
    Rect TileRect(int index) const { return tilesShown_ ? tileRects_[index] : Rect{}; }
    // The all-confirmed home's Play button of a tile (0 US, 1 European) in the last frame, in points (empty when not shown).
    Rect ReadyPlayRect(int index) const { return readyShown_ ? readyPlay_[index] : Rect{}; }

private:
    struct DialogInbox;
    enum class DialogPurpose { Import, DataFolder };

    bool StartImport(const std::vector<std::string>& paths);
    void PollImport();
    void FinishImport(const assets::ImportReport& report);
    void PollDialogs();
    void PollVerify();
    void Browse(bool folder, DialogPurpose purpose);
    int DropTargetAt(float x, float y) const;   // 0..2 an edition tile, -1 the drop field, -2 nothing
    void RefreshStatus();
    bool AnyPlayable() const;
    bool AllConfirmed() const;   // US, European and Gold all unlocked: the home becomes two tiles to play from
    void OpenPlay();
    TileView TileFor(int index) const;

    void DrawBackdrop(const Rect& window);
    float DrawHeader(const Rect& window, bool buttons);
    void DrawTiles(const Rect& row, bool dropTargets);
    void DrawHome(const Rect& window);
    void DrawReady(const Rect& window);
    void DrawChecking(const Rect& window);
    void DrawResult(const Rect& window);
    void DrawPlay(const Rect& window);
    void DrawSettings(const Rect& window);
    void DrawParkRules(const Rect& window);
    void DrawAbout(const Rect& window);
    void DrawStarting(const Rect& window);

    Canvas canvas_;
    Text text_;
    Ui ui_;
    ImportBackend& backend_;
    SDL_Window* window_ = nullptr;

    Input input_;              // gathered from events since the last frame
    Screen screen_ = Screen::Home;
    bool aboutOpen_ = false, settingsOpen_ = false, rulesOpen_ = false;
    int arrivals_ = 0;       // the Park rules box's "Visitors arriving": 0 normal, 1 more, 2 many
    int visitorLimit_ = 0;   // its "Visitor limit": 0 1500, 1 3000, 2 5000
    bool skipIntro_ = false;   // the Settings box's "Skip the intro films", as launcher.ini keeps it
    bool backToLauncher_ = false;   // the Settings box's "Back to the launcher after playing", as launcher.ini keeps it
    bool smoothZoom_ = false;       // the Settings box's "Smooth wheel zoom", as launcher.ini keeps it

    // A drag over the window.
    bool dragging_ = false;
    int dropTarget_ = -2;
    std::vector<std::string> dropped_;
    Rect fieldRect_;           // last frame's drop targets
    Rect tileRects_[3];
    bool tilesShown_ = false;
    Rect readyPlay_[2];
    bool readyShown_ = false;

    // The editions' states, refreshed every frame.
    assets::EditionStatus status_[3];

    // The import: its log, counter, the queue of drops made while it runs, and the reports of this round.
    std::vector<LogEntry> log_;
    assets::ImportProgress counter_;
    bool scanning_ = false;
    std::vector<std::vector<std::string>> queued_;
    assets::EditionStatus before_[3];
    assets::ImportReport round_;
    bool roundStarted_ = false;
    ResultText result_;

    // Play.
    int playIndex_ = 1;        // 0 US, 1 European
    bool gold_[2] = {false, false};
    bool playChosen_ = false;
    LaunchChoice choice_;

    // Settings: re-verify on a thread of its own, and the two-step remove.
    struct VerifyResult {
        long removed = 0;
        std::string err;
    };
    std::future<VerifyResult> verify_;
    int verifyIndex_ = -1;
    int armedRemove_ = -1;
    std::string settingsMessage_;

    std::string notice_;
    bool noticeProblem_ = false;
    float noticeLeft_ = 0;

    std::shared_ptr<DialogInbox> inbox_;

    // Starting.
    Screen startingFrom_ = Screen::Home;
    assets::Edition startingEdition_ = assets::kEditionEur;
    float startingFraction_ = -1;
    SDL_Texture* startingBehind_ = nullptr;
};

}  // namespace tpw::ui
