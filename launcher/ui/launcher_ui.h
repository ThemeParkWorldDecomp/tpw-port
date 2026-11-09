// launcher_ui.h: the launcher's screens (launcher-design REPORT section 5): the drop field with its Browse buttons and
// the three edition tiles (once all three editions are confirmed, two large tiles to play from instead); the checking log with its counter and progress bar; the result; Play with the edition
// switch and the Gold toggles; the Settings and About boxes; and the Customized settings box of an edition whose .sam
// settings files the player edited (its badge on the edition's tile). Driven by SDL3 events: mouse, wheel, Escape and the
// drop events (DROP_BEGIN, DROP_POSITION, DROP_FILE, DROP_COMPLETE; DROP_TEXT is ignored). The window loop is
// window.h's; the screenshot test drives this class on a software renderer with a scripted backend.
#pragma once
#include <SDL3/SDL.h>

#include <future>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "backend.h"
#include "draw.h"
#include "game_settings.h"
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
// launcher.ini's key for the Settings box's "Zoom out further" ("1": the park view zooms out further than the original's
// limit, gPortFarZoom; absent or "0": the original's limit).
inline constexpr const char* kFarZoomKey = "far_zoom";
// launcher.ini's key for the Settings box's "Interface size": 100, 85 or 70, the share of the screen's height the game's
// panels are laid out in (absent: 100, as the original). From tpw-port PR #24 by samueleagle91.
inline constexpr const char* kInterfaceSizeKey = "interface_size";
// launcher.ini's key for the Settings box's "Varied sounds" (absent or "1": each of the sounds' random draws moves the seed,
// as on the PC, so a sound varies among its recordings; "0": the Mac's draws, one recording a sound each launch).
inline constexpr const char* kPcSoundPicksKey = "pc_sound_picks";
// launcher.ini's key for the Settings box's "Show Ride Badges" (absent or "1": the Buy Ride panel's picture of a Gold Edition
// ride the base game does not have has a "GOLD" tag, and of a PlayStation rides mod ride a "PS1" one, include/RideBadges.h;
// "0": no tags, as the original). Display only; on by default (the maintainer's approved exception to settings off by default,
// 2026-11-08). The GOLD tag is TPW_PORT_FIX goldbadge, the PS1 tag the ps1-rides mod's (its ps1_badge hook); this one
// switch shows or hides both.
inline constexpr const char* kRideBadgesKey = "ride_badges";
// Whether the Settings box has the "Show Ride Badges" row: only with the port's fixes, as the badges themselves are; the
// OFF build's launcher has no such switch and its game no tags.
#ifdef TPW_PORT_FIX
inline constexpr bool kRideBadgesRow = true;
#else
inline constexpr bool kRideBadgesRow = false;
#endif
// launcher.ini's key for the Settings box's "Full screen" (absent or "1": the game is a real full-screen window, as it has
// always been; "0": it plays in a window with a title bar, as --windowed starts it). Alt+Enter in the game switches on
// Windows, Cmd+Ctrl+F or the green button on a Mac.
inline constexpr const char* kFullScreenKey = "fullscreen";
// launcher.ini's key for the Settings box's "Renderer" (Linux only): what draws the game, "opengl" (absent: the system's
// OpenGL, the default, the API the original draws with) or "vulkan" (the port's own Vulkan renderer, platform/vulkan/).
// Play asks for it as --renderer does (launcher/main.cpp; --renderer wins when both are given).
inline constexpr const char* kRendererKey = "renderer";
// Whether the Settings box has the Renderer row: on Linux, the one system with a second renderer (platform/gfx.h).
#ifdef __linux__
inline constexpr bool kRendererRow = true;
#else
inline constexpr bool kRendererRow = false;
#endif
// launcher.ini's keys for the "Gold Edition" switch, one an edition: on the play tiles of the all-confirmed home and on the
// Play screen's card. "0": that edition plays plain although the Gold bonus rides are installed; absent or "1": with them
// (the maintainer, 2026-10-25: players who have the Gold data must be able to play without it).
inline constexpr const char* kGoldUsKey = "gold_us";
inline constexpr const char* kGoldEurKey = "gold_eur";
// A Windows edition shares its game's key (IsUsGame).
inline const char* GoldKey(assets::Edition edition) { return assets::IsUsGame(edition) ? kGoldUsKey : kGoldEurKey; }
// launcher.ini's keys for the Settings box's "Language", one an edition (a player may want American on the US edition and
// Danish on the European one): the language of the game's text and the advisor's voice, as the original spells it
// (FS_Registry.cpp's names: "French", "TraditionalChinese"). Absent: the edition's own, English for the European edition
// and American for the US one, the original's default. Play hands it to the game as --language does
// (platform/language_pack.h).
inline constexpr const char* kLanguageUsKey = "language_us";
inline constexpr const char* kLanguageEurKey = "language_eur";
inline const char* LanguageKey(assets::Edition edition) {
    return assets::IsUsGame(edition) ? kLanguageUsKey : kLanguageEurKey;
}
// The edition's own language, as the original spells it: "American" for the US edition, "English" for the European one.
inline const char* OwnLanguage(assets::Edition edition) { return assets::IsUsGame(edition) ? "American" : "English"; }

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
// launcher.ini's key for the Settings box's "Stereo panning" ("1": a sound placed in the park plays louder in the speaker on
// its side of the camera; absent or "0": every sound plays centred, as the original did). From tpw-port PR #19 by samueleagle91.
inline constexpr const char* kSoundPanKey = "sound_panning";
// launcher.ini's key for the Settings box's "Several keys at once" ("1": a key that goes down keeps the keys already down,
// as on the PC, so two arrow keys walk and turn at once in first person; absent or "0": one key at a time, as the original
// did, tpw::gPortSeveralKeys). Reported by Addable13.
inline constexpr const char* kSeveralKeysKey = "several_keys";
// launcher.ini's key for the Settings box's "W A S D move the camera" ("1": the plain W, A, S and D, by their place on the
// keyboard, scroll the park and walk in first person, and do not open the screens or run the debug keys they did; absent or
// "0": the original's keys, gPortWasdCamera in include/port_wasd.h). Asked for by the maintainer.
inline constexpr const char* kWasdCameraKey = "wasd_camera";
// launcher.ini's key for the Settings box's "Online server" ("HOST": the online screens connect to the online server at HOST,
// which speaks the original protocol, include/platform/online_boss.h; absent or empty: the automatic one, updater.h
// ChooseOnlineServer).
inline constexpr const char* kOnlineServerKey = "online_server";
// The longest host the Online server field takes (a DNS name's limit).
inline constexpr size_t kOnlineServerMax = 253;
// launcher.ini's key for the Settings box's "Frame rate limit": absent or "0", Monitor (a frame each refresh of the
// monitor, as the original drew with no limit of its own); else the most frames a second the game draws,
// kFrameRateLowest or more (gfx::gFrameLimit). The row's slider goes from kFrameRateLowest up to the refresh rate of the
// launcher's monitor, whose right end is Monitor.
inline constexpr const char* kFrameRateKey = "frame_rate_limit";
inline constexpr int kFrameRateLowest = 30;
// launcher.ini's Frame rate limit as a number: 0 for Monitor (absent, "0", or not a whole number of at least
// kFrameRateLowest).
int FrameRateSetting(const std::string& value);

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

// The launcher's settings, written to the launcher.ini of `prefPath` again after the importer's last write of the file (the
// choice it saved on Play, an import in the window), which kept the importer's older copy of the keys: the keys above,
// the update check's (updater.h), each of SamOptions(), and `more` (the Mods box's switches, launcher/main.cpp). A key
// with no value is left as the file has it. From tpw-port PRs #10, #19, #20, #22 and #24 by samueleagle91.
void KeepSettings(const ImporterBackend& backend, const std::string& prefPath, const std::vector<std::string>& more = {});

// What the About box's Export logs button did (the launcher's platform/log_export.h): `message` is for the player ("Saved to
// Downloads: <name>", or why nothing was saved) and `folder` is where the copy is, to show (empty: none was made).
struct LogsExport {
    bool ok = false;
    std::string message;
    std::string folder;
};
// Shows the log file (logs/launcher.log) of the logs folder selected in the system's file manager; false when it could
// not (Show logs then opens the folder). launcher/main.cpp supplies it.
using ShowLogsFn = bool (*)(const std::string& logsFolder);
// Saves the recent runs of the logs folder's log file (the launcher's and the game's) as one file in the player's Downloads
// folder (launcher/main.cpp supplies it).
using ExportLogsFn = LogsExport (*)(const std::string& logsFolder);

// A mod of the launcher's build (platform/mods.h's catalog, which launcher/main.cpp gives the window), as the Mods box
// shows it. Its switch is launcher.ini's ModKey(id): "1" on, absent or "0" off (the original game).
struct ModRow {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string text;              // what it does, for the player
    bool editions[3] = {};         // the editions it supports: 0 US, 1 European, 2 Gold (the Gold bonus rides)
    bool changesSaves = false;     // a game saved with it on differs from the original's
    std::vector<std::string> conflicts;   // the ids of mods it cannot be on with
    std::string locked;            // why it cannot be on yet (a content mod before the player's own disc is converted:
                                   // launcher/main.cpp), shown in its row; empty when it can. A locked mod shows off and
                                   // its switch cannot be turned on, and launcher.ini's key is kept for when it unlocks
    std::string content;           // mod.json's "content" ("ps1": the PlayStation rides), empty for a mod without
    std::vector<std::string> rides;   // its content folder's rides as the Game data box names them (Aztec Bounce, ...)
};
inline std::string ModKey(const std::string& id) { return "mod." + id; }

// A row of the Game data box: one of the five things the player's own discs unlock, in the box's order (Sim Theme Park,
// Theme Park World, Gold Edition, the PlayStation rides mod, Languages), and whether its data is in the data folder.
struct GameDataRow {
    std::string name;     // "Sim Theme Park"
    std::string tag;      // what it is, beside the name: "US edition"
    bool unlocked = false;
    float fraction = 0;   // how much of it is there, for the badge: 0 none (a lock), 1 all (a check)
    std::string line;     // unlocked: what is there ("Mac release, 1,683 files confirmed"); locked: how to unlock it
};
// The five rows from the editions' six statuses (StatusIndex order), the data folder's languages
// (ImportBackend::Languages()) and the build's mods (the PlayStation rides are the mod whose content is "ps1").
std::vector<GameDataRow> GameDataRows(const assets::EditionStatus all[6],
                                      const std::vector<assets::LanguageStatus>& languages,
                                      const std::vector<ModRow>& mods);

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
    void SetWindow(SDL_Window* window) {
        window_ = window;
        frameRateTopStale_ = true;
    }
    // The folder of the launcher's log file (launcher/main.cpp): the About box's Show logs opens it. Empty: no button.
    void SetLogsFolder(const std::string& folder) { logsFolder_ = folder; }
    // What the Show logs buttons call first, to select the log file (null: they open the folder).
    void SetShowLogs(ShowLogsFn showLogs) { showLogs_ = showLogs; }
    // What the About box's Export logs button calls (with a logs folder). Null: no such button.
    void SetExportLogs(ExportLogsFn exportLogs) { exportLogs_ = exportLogs; }
    // The mods of the build (a build with the port's fixes, launcher/main.cpp): the header gets a Mods button, which
    // opens the Mods box and, while any mod is on, says how many ("1 mod on"). Never called (a build without the fixes): no button.
    void SetMods(const std::vector<ModRow>& mods);
    // Reread content locks after an import or a change of data folder, so a disc drop unlocks its mod immediately.
    void SetModsReader(std::function<std::vector<ModRow>()> reader);
    // The game's own settings (a build with the port's fixes, launcher/main.cpp): the Settings box gets the Game options
    // section, every setting of the game's Options panel, read from and written to the game's files (game_settings.h).
    // Never called (a build without the fixes): no such section.
    void SetGameSettings(const GameSettingsSource& source);
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
    // Opens the Customized settings box of an edition (0 US, 1 European, 2 Gold; -1 closes it), as its badge does.
    void ShowEdited(int index);
    // The edition whose Customized settings box is open (0 US, 1 European, 2 Gold), -1 none.
    int EditedOpen() const { return editedOpen_; }
    // The Customized settings badge of an edition's tile (0 US, 1 European, 2 Gold) in the last frame, in points (empty
    // when not shown: the edition has no edited .sam file, or no tile is shown).
    Rect EditedBadgeRect(int index) const { return editedBadge_[index]; }
    // The Restore original button of a row of the open Customized settings box in the last frame, in points (empty when
    // the box is closed or has no such row).
    Rect RestoreRect(int row) const {
        return editedOpen_ >= 0 && row >= 0 && row < static_cast<int>(restoreRects_.size())
                   ? restoreRects_[static_cast<size_t>(row)] : Rect{};
    }
    void ShowMods(bool open);
    // The Game data box: open or closed, as the homes' Game data button does; whether it is open; its rows as the last
    // frame drew them (GameDataRows); and where the last frame drew the homes' Game data button (in the header where it
    // fits, else in the home's body: GameDataInHeader), the box and its Settings button, in points (empty when not shown).
    void ShowGameData(bool open);
    bool GameDataOpen() const { return gameDataOpen_; }
    std::vector<GameDataRow> GameData() const { return GameDataRows(all_, languages_, mods_); }
    Rect GameDataButtonRect() const { return gameDataButtonRect_; }
    bool GameDataInHeader() const { return gameDataInHeader_; }
    Rect GameDataBoxRect() const { return gameDataOpen_ ? gameDataBoxRect_ : Rect{}; }
    Rect GameDataSettingsRect() const { return gameDataOpen_ ? gameDataSettingsRect_ : Rect{}; }
    void ArmRemove(assets::Edition edition);
    void ShowNotice(const std::string& message, bool problem);
    // Play's edition switch and Gold Edition toggle, as if clicked (the toggle's state is kept in launcher.ini, GoldKey).
    void ChoosePlay(assets::Edition edition, bool gold);
    // Where the last frame laid out the edition tiles, in points (an empty rect when not shown). Index 0 US, 1
    // European, 2 Gold (empty on the all-confirmed home, whose two tiles are the US and European ones).
    Rect TileRect(int index) const { return tilesShown_ ? tileRects_[index] : Rect{}; }
    // Where the last frame drew that tile: its TileRect, a little grown only while the pointer is over it (never while
    // a drag is over the window).
    Rect TileDrawnRect(int index) const { return tilesShown_ ? tileDrawn_[index] : Rect{}; }
    // The card the last frame showed while a drag was over the window (empty when none: no drag, or it left or
    // dropped).
    Rect DropOverlayRect() const { return dropOverlay_; }
    // The home with the drop field's Play button of a small tile (0 US, 1 European; never 2 Gold) in the last frame, in points
    // (empty when not shown: that edition is not playable, or this is another screen).
    Rect TilePlayRect(int index) const { return tilesShown_ ? tilePlay_[index] : Rect{}; }
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
    // The Graphics section's Frame rate limit slider in the last frame, in points (empty when the box is closed), its top
    // (the launcher monitor's refresh rate) and the limit it shows: 0 Monitor, else frames a second.
    Rect FrameRateRect() const { return settingsOpen_ ? frameRateRect_ : Rect{}; }
    int FrameRateHighest() const { return frameRateHighest_; }
    int FrameRateChoice() const { return frameRate_; }
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
    // The Settings box's Stereo panning switch where the last frame drew it, in points (empty when the box is closed).
    Rect SoundPanRect() const { return settingsOpen_ ? soundPanRect_ : Rect{}; }
    // The Settings box's Show Ride Badges switch where the last frame drew it, in points (empty when the box is closed or
    // the build has no such row, kRideBadgesRow).
    Rect RideBadgesRect() const { return settingsOpen_ ? rideBadgesRect_ : Rect{}; }
    // The Settings box's Several keys at once switch where the last frame drew it, in points (empty when the box is closed).
    Rect SeveralKeysRect() const { return settingsOpen_ ? severalKeysRect_ : Rect{}; }
    // The Settings box's W A S D move the camera switch where the last frame drew it, in points (empty when the box is closed).
    Rect WasdCameraRect() const { return settingsOpen_ ? wasdCameraRect_ : Rect{}; }
    // The Settings box's "Online server" field where the last frame drew it, in points (empty when the box is closed),
    // whether it is being edited, and the greyed automatic server it shows while empty.
    Rect OnlineServerRect() const { return settingsOpen_ ? onlineServerRect_ : Rect{}; }
    bool OnlineServerEditing() const { return onlineServerEditing_; }
    const std::string& OnlineServerHint() const { return onlineServerHint_; }
    // The Settings box's Renderer switch where the last frame drew it, in points (empty when the box is closed or has no
    // such row, kRendererRow), and the choice it shows: 0 OpenGL, 1 Vulkan.
    Rect RendererRect() const { return settingsOpen_ ? rendererRect_ : Rect{}; }
    int RendererChoice() const { return renderer_; }
    // For the tests: the Renderer row shown or not, whatever the system (the launcher's own is kRendererRow).
    void ShowRendererRow(bool shown) { rendererRow_ = shown; }
    // Whether the last frame scrolled the Settings box, with its clip and its bar (false when the box is closed).
    bool SettingsScrolls() const { return settingsOpen_ && settingsScrolls_; }
    // The Settings box's body (everything below its title) in the last frame, in points: the height counted before it
    // was drawn, which sizes the box and how far it scrolls, and the height then drawn. The two must agree (0 when the
    // box is closed).
    float SettingsBodyCounted() const { return settingsOpen_ ? settingsBodyCounted_ : 0.0f; }
    float SettingsBodyDrawn() const { return settingsOpen_ ? settingsBodyDrawn_ : 0.0f; }
    // The About box's Show logs button in the last frame, in points (empty when not shown).
    Rect ShowLogsRect() const { return showLogsRect_; }
    // The About box's Export logs button, and the Open Downloads button that follows a saved export, in the last frame, in
    // points (empty when not shown).
    Rect ExportLogsRect() const { return exportLogsRect_; }
    Rect OpenDownloadsRect() const { return openDownloadsRect_; }
    // The title row in the last frame, in points: the product's name, the version label drawn to its right, and the
    // header's buttons (About, Settings, Mods and the homes' Game data where it fits; empty on screens without them).
    Rect TitleRect() const { return titleRect_; }
    Rect TitleVersionRect() const { return titleVersionRect_; }
    Rect HeaderButtonsRect() const { return headerButtonsRect_; }
    // The header's Mods button in the last frame, and the same while it says "N mods on" (a mod is on), in points
    // (empty when not shown).
    Rect ModsButtonRect() const { return modsButtonRect_; }
    Rect ModsOnRect() const { return modsOnRect_; }
    // The Mods box and the switch of the mod of row `index` where the last frame drew them (scrolled with the box), in
    // points (empty when the box is closed), and whether the box scrolled.
    Rect ModsBoxRect() const { return modsOpen_ ? modsBoxRect_ : Rect{}; }
    Rect ModSwitchRect(size_t index) const {
        return modsOpen_ && index < modSwitchRects_.size() ? modSwitchRects_[index] : Rect{};
    }
    bool ModsScrolls() const { return modsOpen_ && modsScrolls_; }
    // How many mods are on (launcher.ini's mod.<id> keys), and whether the switch of row `index` is.
    int ModsOn() const;
    bool ModOn(size_t index) const { return index < modOn_.size() && modOn_[index]; }
    // The Game options section as the last read found it, and where the last frame drew its controls, in points (empty when
    // the box is closed or has no such section): an option's own, Screen resolution's slider for GameOption::Resolution,
    // and the edition switch, the shape switch and the player's field. The volumes' switches are their On options.
    const GameSettingsView& GameView() const { return gameView_; }
    int GameEdition() const { return gameEdition_; }
    const std::string& GameMessage() const { return gameMessage_; }
    Rect GameOptionRect(GameOption option) const { return settingsOpen_ && gameShown_ ? gameRects_[static_cast<int>(option)] : Rect{}; }
    Rect GameEditionRect() const { return settingsOpen_ && gameShown_ ? gameEditionRect_ : Rect{}; }
    Rect GameShapeRect() const { return settingsOpen_ && gameShown_ ? gameShapeRect_ : Rect{}; }
    Rect GamePlayerRect() const { return settingsOpen_ && gameShown_ ? gamePlayerRect_ : Rect{}; }
    // A row of the open player list in the last frame, in points (empty when no list is shown or no such row).
    Rect GamePlayerRow(int row) const {
        return gamePlayerOpen_ && row >= 0 && row < static_cast<int>(gamePlayerRows_.size())
                   ? gamePlayerRows_[static_cast<size_t>(row)] : Rect{};
    }

private:
    struct DialogInbox;
    enum class DialogPurpose { Import, DataFolder };

    bool StartImport(const std::vector<std::string>& paths);
    void PollImport();
    void FinishImport(const assets::ImportReport& report);
    void PollDialogs();
    void PollVerify();
    void Browse(bool folder, DialogPurpose purpose);
    void RefreshStatus();
    bool AnyPlayable() const;
    bool AllConfirmed() const;   // US, European and Gold all unlocked: the home becomes two tiles to play from
    void OpenPlay();
    // Every Play button's start: the game of a tile (status_ index), with the Gold rides or without.
    void StartPlay(int index, bool gold);
    // Flips an edition's Gold Edition switch (0 US, 1 European) and keeps it in launcher.ini (GoldKey).
    void SetGold(int index, bool on);
    TileView TileFor(int index) const;
    // The small tiles' height: taller while one of them shows the Customized settings badge, which sits at its bottom.
    float TileHeight() const;
    // The Customized settings badge of an edition (status_ index) in `r`, when it has edited .sam files; opens its box.
    void EditedBadge(int index, const Rect& r);
    // Reads the data folder's languages again (ImportBackend::Languages() reads the folder; not every frame): at the
    // start, after an import, a check or a remove, and when Settings opens.
    void RefreshLanguages();

    void DrawBackdrop(const Rect& window);
    float DrawHeader(const Rect& window, bool buttons);
    float DrawUpdateBanner(const Rect& window, float top);   // returns where the screen goes on
    void DrawTiles(const Rect& row);
    // While a drag is over the window: one state for all of it (Ui::DropOverlay). Returns its card, or an empty rect.
    Rect DrawDropOverlay(const Rect& window);
    void DrawHome(const Rect& window);
    void DrawReady(const Rect& window);
    void DrawChecking(const Rect& window);
    void DrawResult(const Rect& window);
    void DrawPlay(const Rect& window);
    void DrawSettings(const Rect& window);
    void DrawAbout(const Rect& window);
    void DrawEdited(const Rect& window);
    void DrawMods(const Rect& window);
    void DrawGameData(const Rect& window);
    // The Game options section of the Settings box (settings_game.cpp): its height, and drawing it at `y` in `in`.
    float GameOptionsHeight(float rowH);
    void DrawGameOptions(const Rect& in, float& y, float rowH);
    void DrawGamePlayerList(const Rect& window);
    // Reads the shown edition's files again: when the box opens, when the window comes back, and after each change.
    void RefreshGameSettings();
    void WriteGameSetting(GameOption option, int value);
    // Whether row `index`'s switch can be turned on: the mod supports an edition of the data folder and no mod it
    // conflicts with is on. Empty `why` when it can, else what the box says.
    bool ModAllowed(size_t index, std::string& why) const;
    void DrawStarting(const Rect& window);

    Canvas canvas_;
    Text text_;
    Ui ui_;
    ImportBackend& backend_;
    SDL_Window* window_ = nullptr;

    Input input_;              // gathered from events since the last frame
    Screen screen_ = Screen::Home;
    bool aboutOpen_ = false, settingsOpen_ = false, modsOpen_ = false;
    int editedOpen_ = -1;      // the edition whose Customized settings box is open (status_ index), -1 none
    std::vector<assets::EditedFile> editedFiles_;   // its edited .sam files, read when it opens and after a restore
    Rect editedBadge_[3];      // last frame's (EditedBadgeRect)
    std::vector<Rect> restoreRects_;   // last frame's (RestoreRect)
    bool skipIntro_ = false;   // the Settings box's "Skip the intro films", as launcher.ini keeps it
    bool backToLauncher_ = false;   // the Settings box's "Back to the launcher after playing", as launcher.ini keeps it
    bool pcSoundPicks_ = true;      // the Settings box's "Varied sounds", as launcher.ini keeps it (on when absent)
    bool rideBadges_ = true;        // the Settings box's "Show Ride Badges", as launcher.ini keeps it (on when absent)
    bool fullScreen_ = true;        // the Settings box's "Full screen", as launcher.ini keeps it (on when absent)
    bool rendererRow_ = kRendererRow;   // whether the Settings box has the Renderer row
    int renderer_ = 0;              // the Settings box's "Renderer": 0 OpenGL (also when absent), 1 Vulkan
    bool soundPan_ = false;         // the Settings box's "Stereo panning", as launcher.ini keeps it
    std::string onlineServer_;      // the Settings box's "Online server", as launcher.ini keeps it (empty: automatic)
    bool onlineServerEditing_ = false;   // the field has the keyboard (text input is on while it does)
    std::string onlineServerHint_;  // last frame's greyed automatic server
    bool smoothZoom_ = false;       // the Settings box's "Smooth wheel zoom", as launcher.ini keeps it
    bool farZoom_ = false;          // the Settings box's "Zoom out further", as launcher.ini keeps it
    bool severalKeys_ = false;      // the Settings box's "Several keys at once", as launcher.ini keeps it
    bool wasdCamera_ = false;       // the Settings box's "W A S D move the camera", as launcher.ini keeps it
    int interfaceSize_ = 0;         // the Settings box's "Interface size": 0 100%, 1 85%, 2 70%
    std::vector<int> samChoices_;   // the Graphics section: for each of SamOptions(), 0 the detail level's own, else 1 + its choice
    int frameRate_ = 0;             // the Settings box's "Frame rate limit": 0 Monitor, else frames a second (FrameRateSetting)
    int frameRateHighest_ = 60;     // the slider's top: the refresh rate of the launcher's monitor (60 when unknown)
    bool frameRateTopStale_ = true; // frameRateHighest_ is read again (the window moved to another monitor, its mode changed)
    bool checkUpdates_ = true;      // the Settings box's "Check for updates", as launcher.ini keeps it (on when absent)
    std::vector<assets::LanguageStatus> languages_;   // the data folder's languages (RefreshLanguages)
    int languageOpen_ = -1;         // the edition whose Language list is open (0 US, 1 European), -1 none
    std::string language_[2];       // the Language an edition (0 US, 1 European), as launcher.ini keeps it (LanguageKey)
    Updater* updater_ = nullptr;

    // A drag over the window. It goes to the window as a whole: the importer sorts each file by its content, so no tile
    // or box under the pointer is its target.
    bool dragging_ = false;
    std::vector<std::string> dropped_;
    Rect dropOverlay_;         // last frame's drag-over card (DropOverlayRect)
    Rect fieldRect_;           // last frame's drop field and tiles
    Rect tileRects_[3];
    Rect tileDrawn_[3];        // where the last frame drew them (TileDrawnRect)
    bool tilesShown_ = false;
    Rect tilePlay_[3];         // last frame's (TilePlayRect)
    Rect readyPlay_[2];
    Rect readyGold_[2];        // the all-confirmed home's Gold Edition switches
    bool readyShown_ = false;
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

    // The editions' states, refreshed every frame: all six (StatusIndex order), and what each tile shows (SlotStatus).
    assets::EditionStatus all_[6];
    assets::EditionStatus status_[3];

    // The import: its log, counter, the queue of drops made while it runs, and the reports of this round.
    std::vector<LogEntry> log_;
    assets::ImportProgress counter_;
    bool scanning_ = false;
    std::vector<std::vector<std::string>> queued_;
    assets::EditionStatus before_[6];   // all_ when the round started
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
    Rect frameRateRect_;       // last frame's (FrameRateRect)
    Rect soundPanRect_;        // last frame's (SoundPanRect)
    Rect rideBadgesRect_;      // last frame's (RideBadgesRect)
    Rect severalKeysRect_;     // last frame's (SeveralKeysRect)
    Rect wasdCameraRect_;      // last frame's (WasdCameraRect)
    Rect onlineServerRect_;    // last frame's (OnlineServerRect)
    Rect rendererRect_;        // last frame's (RendererRect)
    bool settingsScrolls_ = false;   // last frame's (SettingsScrolls)
    float settingsBodyCounted_ = 0, settingsBodyDrawn_ = 0;   // last frame's (SettingsBodyCounted, SettingsBodyDrawn)

    // Mods (SetMods): the rows, their switches as launcher.ini keeps them, and the last frame's rects.
    std::function<std::vector<ModRow>()> modsReader_;
    bool modsShown_ = false;
    std::vector<ModRow> mods_;
    std::vector<bool> modOn_;
    std::string modsMessage_;
    Rect modsButtonRect_, modsOnRect_, modsBoxRect_;
    std::vector<Rect> modSwitchRects_;
    bool modsScrolls_ = false;

    // The Game data box: open, to open again once a drop made while it was open is done (back on the home), and the
    // last frame's rects.
    bool gameDataOpen_ = false;
    bool gameDataReturn_ = false;
    bool gameDataInHeader_ = false;   // this frame's homes have the button in the header (a wide enough window)
    Rect gameDataButtonRect_, gameDataBoxRect_, gameDataSettingsRect_;

    // The Game options section (SetGameSettings): the files as last read, the edition and the player they are read for
    // (empty player: the last one played), and the last frame's rects.
    bool gameShown_ = false;
    GameSettingsSource gameSource_;
    GameSettingsView gameView_;
    GameSize gameSizeOpened_;  // the size the file held when the box read it, and its number: a stop while the box is open,
    int gameResolutionOpened_ = 0;   // and coming back to it keeps the number, as the panel's slider does
    int gameEdition_ = -1;     // 0 US, 1 European; -1 until the box first opens: the edition Play shows
    std::string gamePlayer_;
    bool gamePlayerOpen_ = false;
    bool settingsWereOpen_ = false;
    bool gameSettingsStale_ = false;
    std::string gameMessage_;  // why the last change was not kept, until the next read
    Rect gameRects_[static_cast<int>(GameOption::Count)];
    Rect gameEditionRect_, gameShapeRect_, gamePlayerRect_;
    std::vector<Rect> gamePlayerRows_;

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
