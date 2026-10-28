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

// launcher.ini's key for the Settings box's "Skip the intro films" ("1" skips them). From tpw-port PR #10 by samueleagle91.
inline constexpr const char* kSkipIntroKey = "skip_intro";
// launcher.ini's key for the Settings box's "Back to the launcher after playing" ("1": the launcher opens again when the
// game quits). From tpw-port PR #22 by samueleagle91.
inline constexpr const char* kBackToLauncherKey = "back_to_launcher";
// launcher.ini's key for the Settings box's "Smooth wheel zoom" ("1": the wheel eases the zoom toward the pointer; absent
// or "0": the original's step toward the middle of the screen). From tpw-port PR #20 by samueleagle91.
inline constexpr const char* kSmoothZoomKey = "smooth_wheel_zoom";
// launcher.ini's key for the Settings box's "Interface size": 100, 85 or 70, the share of the screen's height the game's
// panels are laid out in (absent: 100, as the original). From tpw-port PR #24 by samueleagle91.
inline constexpr const char* kInterfaceSizeKey = "interface_size";
// launcher.ini's key for the Settings box's "Varied sounds" (absent or "1": each of the sounds' random draws moves the seed,
// as on the PC, so a sound varies among its recordings; "0": the Mac's draws, one recording a sound each launch).
inline constexpr const char* kPcSoundPicksKey = "pc_sound_picks";
// launcher.ini's key for the Settings box's "Full screen" (absent or "1": the game is a real full-screen window, as it has
// always been; "0": it plays in a window with a title bar, as --windowed starts it). Alt+Enter in the game switches on
// Windows, Cmd+Ctrl+F or the green button on a Mac.
inline constexpr const char* kFullScreenKey = "fullscreen";
// launcher.ini's keys for the "Gold Edition" switch, one an edition: on the play tiles of the all-confirmed home and on the
// Play screen's card. "0": that edition plays plain although the Gold bonus rides are installed; absent or "1": with them
// (the maintainer, 2026-10-25: players who have the Gold data must be able to play without it).
inline constexpr const char* kGoldUsKey = "gold_us";
inline constexpr const char* kGoldEurKey = "gold_eur";
inline const char* GoldKey(assets::Edition edition) { return edition == assets::kEditionUs ? kGoldUsKey : kGoldEurKey; }
// launcher.ini's keys for the Settings box's "Language", one an edition (a player may want American on the US edition and
// Danish on the European one): the language of the game's text and the advisor's voice, as the original spells it
// (FS_Registry.cpp's names: "French", "TraditionalChinese"). Absent: the edition's own, English for the European edition
// and American for the US one, the original's default. Play hands it to the game as --language does
// (platform/language_pack.h).
inline constexpr const char* kLanguageUsKey = "language_us";
inline constexpr const char* kLanguageEurKey = "language_eur";
inline const char* LanguageKey(assets::Edition edition) {
    return edition == assets::kEditionUs ? kLanguageUsKey : kLanguageEurKey;
}
// The edition's own language, as the original spells it: "American" for the US edition, "English" for the European one.
inline const char* OwnLanguage(assets::Edition edition) { return edition == assets::kEditionUs ? "American" : "English"; }

// A choice of the Settings box's Language rows: the language as the original spells it and as the list shows it.
struct LanguageChoice {
    std::string name;    // "Spanish"
    std::string shown;   // "Espa\xC3\xB1ol (some speech in English)", UTF-8
};
// The language as the Language list shows it: its own name (Francais, Deutsch, Dansk, ...) where the launcher's fonts can
// draw it, and its English name for Japanese, Korean and both Chinese, which they cannot.
std::string LanguageShownName(const std::string& name);
// The Language list of `edition` from the data folder's languages (ImportBackend::Languages()): the edition's own first,
// then every other one that is available, or partial with only some of its speech missing (shown so: the edition's own
// recordings take those places). One that needs the base edition's date fonts only while they are there.
std::vector<LanguageChoice> LanguageChoices(assets::Edition edition, const std::vector<assets::LanguageStatus>& languages);
// The index in `choices` of launcher.ini's value: 0 (the edition's own) when it is absent, the own name, or not offered.
int LanguageChoiceIndex(const std::vector<LanguageChoice>& choices, const std::string& value);

// An option of the original's options files (low.sam, med.sam, high.sam) that the Settings box's Graphics section offers:
// launcher.ini's key, its slot in the game's options table (gPortSamOptions, OptionsBalance.h), the row's title and line,
// and the choices the files' own comments list, as the control shows them and as the files write them. The key's value
// is one of `values`, or "default" (or absent) for the detail level's own.
struct SamOption {
    const char* key;
    int slot;
    const char* title;
    const char* line;
    std::vector<std::string> shown;
    std::vector<int> values;
};
const std::vector<SamOption>& SamOptions();
// The index in `values` launcher.ini's value names, or -1 for the detail level's own (default, absent or not a choice).
int SamChoice(const SamOption& option, const std::string& value);

// What the About box's Export logs button did (the launcher's platform/log_export.h): `message` is for the player ("Saved to
// Downloads: <name>", or why nothing was saved) and `folder` is where the copy is, to show (empty: none was made).
struct LogsExport {
    bool ok = false;
    std::string message;
    std::string folder;
};
// Shows the newest log of the logs folder selected in the system's file manager; false when it could not (the About box's
// Show logs then opens the folder). launcher/main.cpp supplies it.
using ShowLogsFn = bool (*)(const std::string& logsFolder);
// Saves the newest logs of the logs folder (and the game's) as one file in the player's Downloads folder (launcher/main.cpp
// supplies it).
using ExportLogsFn = LogsExport (*)(const std::string& logsFolder);

// What the player chose to start.
class Updater;   // updater.h

struct LaunchChoice {
    assets::Edition edition = assets::kEditionEur;
    bool gold = false;        // with the Gold bonus rides
    bool skipIntro = false;   // without the logo and intro films (Settings)
    bool backToLauncher = false;   // the launcher opens again when the game quits (Settings)
    std::string language;     // the Settings box's Language for the edition, as --language takes it; empty: its own
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
    // The folder of the launcher's log files (launcher/main.cpp): the About box's Show logs opens it. Empty: no button.
    void SetLogsFolder(const std::string& folder) { logsFolder_ = folder; }
    // What the About box's Show logs button calls first (null: it opens the folder).
    void SetShowLogs(ShowLogsFn showLogs) { showLogs_ = showLogs; }
    // What the About box's Export logs button calls (with a logs folder). Null: no such button.
    void SetExportLogs(ExportLogsFn exportLogs) { exportLogs_ = exportLogs; }
    // The update check (updater.h), owned by the window: its banner on the homes, the Settings box's "Check for
    // updates". Null: none of these.
    void SetUpdater(Updater* updater);
    // Pointer coordinates are the window's, in points.
    void HandleEvent(const SDL_Event& event);
    // Updates and draws one frame of `width` x `height` points at `scale` pixels per point. Does not present.
    void Frame(float dt, float width, float height, float scale);
    // True while something moves (a spring, an import, a notice): the window loop keeps drawing.
    bool Animating() const;
    // True once, after the Play button: the choice to start.
    bool TakePlay(LaunchChoice& out);
    // True once, after "Restart to update": the update waits for this launcher to quit, and the window closes.
    bool TakeQuit();
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
    // Play's edition switch and Gold Edition toggle, as if clicked (the toggle's state is kept in launcher.ini, GoldKey).
    void ChoosePlay(assets::Edition edition, bool gold);
    // Where the last frame drew the drop targets, in points (an empty rect when not shown). Index 0 US, 1 European,
    // 2 Gold (empty on the all-confirmed home, whose two tiles are the US and European ones).
    Rect TileRect(int index) const { return tilesShown_ ? tileRects_[index] : Rect{}; }
    // The all-confirmed home's Play button of a tile (0 US, 1 European) in the last frame, in points (empty when not shown).
    Rect ReadyPlayRect(int index) const { return readyShown_ ? readyPlay_[index] : Rect{}; }
    // The all-confirmed home's Gold Edition switch of a tile (0 US, 1 European) in the last frame, in points (empty when
    // not shown).
    Rect ReadyGoldRect(int index) const { return readyShown_ ? readyGold_[index] : Rect{}; }
    // The Settings box's Interface size switch, the last row above the Graphics section, where the last frame drew it
    // (scrolled with the box), in points (empty when the box is closed).
    Rect InterfaceSizeRect() const { return settingsOpen_ ? interfaceSizeRect_ : Rect{}; }
    // The Graphics section's switch of SamOptions()[index] in the last frame, in points (empty when the box is closed),
    // and the choice it shows: 0 Default (the detail level's own), else 1 + the index of its choice.
    Rect SamOptionRect(size_t index) const { return settingsOpen_ ? samRects_[index] : Rect{}; }
    int SamOptionChoice(size_t index) const { return samChoices_[index]; }
    // The Settings box's "Check for updates" switch in the last frame, in points (empty when the box is closed).
    Rect CheckUpdatesRect() const { return settingsOpen_ ? checkUpdatesRect_ : Rect{}; }
    // The Settings box's Language field of an edition (0 US, 1 European) in the last frame, in points (empty when the box
    // is closed), and the list's choices for it.
    Rect LanguageRect(int index) const { return settingsOpen_ ? languageRects_[index] : Rect{}; }
    std::vector<LanguageChoice> LanguageChoicesFor(int index) const;
    // Opens an edition's Language list (0 US, 1 European; -1 closes it), as if its field was clicked.
    void ShowLanguageList(int index);
    // A row of the open Language list in the last frame, in points (empty when no list is shown or no such row).
    Rect LanguageListRow(int row) const {
        return languageOpen_ >= 0 && row >= 0 && row < static_cast<int>(languageRows_.size())
                   ? languageRows_[static_cast<size_t>(row)] : Rect{};
    }
    // The update banner in the last frame, in points (empty when not shown).
    Rect UpdateBannerRect() const { return updateBannerRect_; }
    // Whether the last frame scrolled the Settings box, with its clip and its bar (false when the box is closed).
    bool SettingsScrolls() const { return settingsOpen_ && settingsScrolls_; }
    // Where the last frame drew the launcher's version ("v" and the version, the footer's bottom right corner), in
    // points; empty when not shown.
    Rect VersionRect() const { return versionRect_; }
    // The About box's Show logs button in the last frame, in points (empty when not shown).
    Rect ShowLogsRect() const { return showLogsRect_; }
    // The About box's Export logs button, and the Open Downloads button that follows a saved export, in the last frame, in
    // points (empty when not shown).
    Rect ExportLogsRect() const { return exportLogsRect_; }
    Rect OpenDownloadsRect() const { return openDownloadsRect_; }
    // The title row in the last frame, in points: the product's name, the version label drawn to its right, and the
    // header's buttons (About, Settings and the header's Play when shown; empty on screens without them).
    Rect TitleRect() const { return titleRect_; }
    Rect TitleVersionRect() const { return titleVersionRect_; }
    Rect HeaderButtonsRect() const { return headerButtonsRect_; }

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
    // Flips an edition's Gold Edition switch (0 US, 1 European) and keeps it in launcher.ini (GoldKey).
    void SetGold(int index, bool on);
    TileView TileFor(int index) const;
    // Reads the data folder's languages again (ImportBackend::Languages() reads the folder; not every frame): at the
    // start, after an import, a check or a remove, and when Settings opens.
    void RefreshLanguages();

    void DrawBackdrop(const Rect& window);
    float DrawHeader(const Rect& window, bool buttons);
    float DrawUpdateBanner(const Rect& window, float top);   // returns where the screen goes on
    void DrawTiles(const Rect& row, bool dropTargets);
    Rect DrawFooter(const Rect& window);   // the tagline, the disclaimer and the version; returns where it drew
    void DrawHome(const Rect& window);
    void DrawReady(const Rect& window);
    void DrawChecking(const Rect& window);
    void DrawResult(const Rect& window);
    void DrawPlay(const Rect& window);
    void DrawSettings(const Rect& window);
    void DrawAbout(const Rect& window);
    void DrawStarting(const Rect& window);

    Canvas canvas_;
    Text text_;
    Ui ui_;
    ImportBackend& backend_;
    SDL_Window* window_ = nullptr;

    Input input_;              // gathered from events since the last frame
    Screen screen_ = Screen::Home;
    bool aboutOpen_ = false, settingsOpen_ = false;
    bool skipIntro_ = false;   // the Settings box's "Skip the intro films", as launcher.ini keeps it
    bool backToLauncher_ = false;   // the Settings box's "Back to the launcher after playing", as launcher.ini keeps it
    bool pcSoundPicks_ = true;      // the Settings box's "Varied sounds", as launcher.ini keeps it (on when absent)
    bool fullScreen_ = true;        // the Settings box's "Full screen", as launcher.ini keeps it (on when absent)
    bool smoothZoom_ = false;       // the Settings box's "Smooth wheel zoom", as launcher.ini keeps it
    int interfaceSize_ = 0;         // the Settings box's "Interface size": 0 100%, 1 85%, 2 70%
    std::vector<int> samChoices_;   // the Graphics section: for each of SamOptions(), 0 the detail level's own, else 1 + its choice
    bool checkUpdates_ = true;      // the Settings box's "Check for updates", as launcher.ini keeps it (on when absent)
    std::vector<assets::LanguageStatus> languages_;   // the data folder's languages (RefreshLanguages)
    int languageOpen_ = -1;         // the edition whose Language list is open (0 US, 1 European), -1 none
    std::string language_[2];       // the Language an edition (0 US, 1 European), as launcher.ini keeps it (LanguageKey)
    Updater* updater_ = nullptr;

    // A drag over the window.
    bool dragging_ = false;
    int dropTarget_ = -2;
    std::vector<std::string> dropped_;
    Rect fieldRect_;           // last frame's drop targets
    Rect tileRects_[3];
    bool tilesShown_ = false;
    Rect readyPlay_[2];
    Rect readyGold_[2];        // the all-confirmed home's Gold Edition switches
    bool readyShown_ = false;
    Rect versionRect_;         // last frame's version text (VersionRect)
    Rect showLogsRect_;        // last frame's Show logs button
    Rect exportLogsRect_;      // last frame's Export logs button
    Rect openDownloadsRect_;   // last frame's Open Downloads button (after a saved export)
    Rect titleRect_;           // last frame's title, its version label and the header's buttons
    Rect titleVersionRect_;
    Rect headerButtonsRect_;
    Rect updateBannerRect_;    // last frame's update banner
    std::string logsFolder_;
    ShowLogsFn showLogs_ = nullptr;
    ExportLogsFn exportLogs_ = nullptr;
    LogsExport exported_;      // the last Export logs that saved a copy (ok), shown in the About box until it closes

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
    bool gold_[2] = {true, true};   // the Gold Edition switch an edition (0 US, 1 European), as launcher.ini keeps it: on when absent
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
    Rect interfaceSizeRect_;   // last frame's (InterfaceSizeRect)
    Rect checkUpdatesRect_;    // last frame's (CheckUpdatesRect)
    Rect languageRects_[2];    // last frame's (LanguageRect)
    std::vector<Rect> languageRows_;   // last frame's open list (LanguageListRow)
    std::vector<Rect> samRects_;   // last frame's (SamOptionRect), one for each of SamOptions()
    bool settingsScrolls_ = false;   // last frame's (SettingsScrolls)

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
