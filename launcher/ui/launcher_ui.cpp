// launcher_ui.cpp: the launcher's screens (launcher_ui.h).
#include "launcher_ui.h"
#include "net.h"
#include "updater.h"
#include "versions.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <utility>

namespace tpw::ui {

using assets::Edition;
using assets::EditionStatus;
using assets::ImportProgress;
using assets::ImportReport;

namespace {
constexpr float kBuntingBottom = 24;   // the bunting hangs above this line
constexpr float kTitleTop = 26;
constexpr float kContentTop = 80;      // the screens start below the title
// The version label beside the title: a much smaller face than the title's, kTitleVersionGap points to its right.
constexpr Style kTitleVersionStyle = style::kBodyBold;
constexpr float kTitleVersionGap = 10;
constexpr float kTileHeight = 158;
constexpr float kEditedBadgeH = 26;    // the Customized settings badge at a small tile's bottom (EditedBadge)
constexpr const char* kEditedLabel = "Customized settings";
constexpr size_t kLogLimit = 4000;     // rows kept in the checking log
constexpr float kNoticeSeconds = 4.5f;
constexpr float kStartupNoticeSeconds = 9.0f;   // the startup check's notice: the window may still be opening
constexpr float kBannerHeight = 44;    // the update banner under the title (DrawUpdateBanner)

// The tiles' header bands.
constexpr Rgba kAccents[3] = {colour::kSkyChip, colour::kOrange, colour::kYellow};

Rgba ToneColour(LogRow::Tone tone) {
    switch (tone) {
        case LogRow::Good: return colour::kLimeDeep;
        case LogRow::Same: return colour::kInkSoft;
        case LogRow::Odd: return colour::kOrangeDeep;
        case LogRow::Bad: return colour::kRed;
    }
    return colour::kInk;
}

void Merge(ImportReport& into, const ImportReport& r) {
    into.cancelled = into.cancelled || r.cancelled;
    into.aborted = into.aborted || r.aborted;
    into.entries += r.entries;
    into.containers += r.containers;
    into.hashed += r.hashed;
    into.bytesHashed += r.bytesHashed;
    into.matched += r.matched;
    into.written += r.written;
    into.alreadyThere += r.alreadyThere;
    into.differentVersions.insert(into.differentVersions.end(), r.differentVersions.begin(), r.differentVersions.end());
    std::sort(into.differentVersions.begin(), into.differentVersions.end());
    into.differentVersions.erase(std::unique(into.differentVersions.begin(), into.differentVersions.end()),
                                 into.differentVersions.end());
    into.unknownFiles += r.unknownFiles;
    into.unknownBytes += r.unknownBytes;
    into.errors.insert(into.errors.end(), r.errors.begin(), r.errors.end());
}

// What Play hands the game for an edition's Language (LaunchChoice::language): the kept one while the data folder still
// offers it, else nothing, the edition's own.
std::string PlayLanguage(Edition edition, const std::vector<assets::LanguageStatus>& languages, const std::string& kept) {
    const std::vector<LanguageChoice> choices = LanguageChoices(edition, languages);
    const int i = LanguageChoiceIndex(choices, kept);
    return i > 0 ? choices[static_cast<size_t>(i)].name : std::string();
}

// A cloud: overlapping circles on a flat base, in one opaque colour so the overlaps do not show.
void Cloud(Canvas& canvas, float cx, float cy, float s, Rgba c) {
    canvas.FillRound({cx - 46 * s, cy - 2 * s, 92 * s, 24 * s}, 12 * s, c);
    canvas.FillCircle(cx - 24 * s, cy + 2 * s, 17 * s, c);
    canvas.FillCircle(cx + 2 * s, cy - 8 * s, 24 * s, c);
    canvas.FillCircle(cx + 26 * s, cy + 2 * s, 16 * s, c);
}
}  // namespace

// ---- file dialogs: SDL calls back on a thread of its own; results wait here for the next frame.

struct LauncherUi::DialogInbox {
    struct Result {
        DialogPurpose purpose;
        std::vector<std::string> paths;
        std::string error;
    };
    std::mutex mutex;
    std::vector<Result> results;
};

namespace {
struct DialogRequest {
    std::shared_ptr<void> inbox;   // keeps the inbox alive while a dialog is open
    void (*deliver)(void* inbox, int purpose, std::vector<std::string> paths, std::string error);
    int purpose;
};

void SDLCALL DialogDone(void* userdata, const char* const* filelist, int /*filter*/) {
    std::unique_ptr<DialogRequest> request(static_cast<DialogRequest*>(userdata));
    std::vector<std::string> paths;
    std::string error;
    if (!filelist) {
        error = SDL_GetError();
    } else {
        for (const char* const* p = filelist; *p; ++p) paths.emplace_back(*p);
    }
    request->deliver(request->inbox.get(), request->purpose, std::move(paths), std::move(error));
    SDL_Event wake{};
    wake.type = SDL_EVENT_USER;
    SDL_PushEvent(&wake);
}
}  // namespace

LauncherUi::LauncherUi(SDL_Renderer* renderer, ImportBackend& backend)
    : canvas_(renderer), text_(canvas_), ui_(canvas_, text_), backend_(backend),
      inbox_(std::make_shared<DialogInbox>()) {
    RefreshStatus();
    std::string last = backend_.LastChoice();
    playIndex_ = last == "us" ? 0 : 1;
    for (int i = 0; i < 2; ++i) gold_[i] = backend_.Setting(GoldKey(kEditions[i])) != "0";   // on unless switched off
    for (int i = 0; i < 2; ++i) language_[i] = backend_.Setting(LanguageKey(kEditions[i]));
    RefreshLanguages();
    skipIntro_ = backend_.Setting(kSkipIntroKey) == "1";
    backToLauncher_ = backend_.Setting(kBackToLauncherKey) == "1";
    smoothZoom_ = backend_.Setting(kSmoothZoomKey) == "1";   // tpw-port PR #20 by samueleagle91
    severalKeys_ = backend_.Setting(kSeveralKeysKey) == "1";
    for (const SamOption& option : SamOptions()) samChoices_.push_back(SamChoice(option, backend_.Setting(option.key)) + 1);
    samRects_.resize(SamOptions().size());
    frameRate_ = FrameRateSetting(backend_.Setting(kFrameRateKey));
    pcSoundPicks_ = backend_.Setting(kPcSoundPicksKey) != "0";
    fullScreen_ = backend_.Setting(kFullScreenKey) != "0";
    renderer_ = backend_.Setting(kRendererKey) == "vulkan" ? 1 : 0;   // OpenGL unless Vulkan was chosen
    checkUpdates_ = backend_.Setting(kCheckUpdatesKey) != "0";   // on by default (updater.h says why)
    soundPan_ = backend_.Setting(kSoundPanKey) == "1";   // tpw-port PR #19 by samueleagle91
    onlineServer_ = backend_.Setting(kOnlineServerKey);
    {   // tpw-port PR #24 by samueleagle91
        const std::string size = backend_.Setting(kInterfaceSizeKey);
        interfaceSize_ = size == "85" ? 1 : size == "70" ? 2 : 0;
    }
    // files the startup check (assets/importer.h) found changed and removed
    std::string startup = backend_.StartupNotice();
    if (!startup.empty()) {
        ShowNotice(startup, true);
        noticeLeft_ = kStartupNoticeSeconds;
    }
}

LauncherUi::~LauncherUi() {
    if (verify_.valid()) verify_.wait();
}

void LauncherUi::Browse(bool folder, DialogPurpose purpose) {
    if (!window_) {
        ShowNotice("Choosing files needs a window", true);
        return;
    }
    auto* request = new DialogRequest;
    request->inbox = inbox_;
    request->purpose = static_cast<int>(purpose);
    request->deliver = [](void* inbox, int p, std::vector<std::string> paths, std::string error) {
        auto* box = static_cast<DialogInbox*>(inbox);
        std::lock_guard<std::mutex> lock(box->mutex);
        box->results.push_back({static_cast<DialogPurpose>(p), std::move(paths), std::move(error)});
    };
    if (folder)
        SDL_ShowOpenFolderDialog(DialogDone, request, window_, nullptr, purpose == DialogPurpose::Import);
    else
        SDL_ShowOpenFileDialog(DialogDone, request, window_, nullptr, 0, nullptr, true);
}

void LauncherUi::PollDialogs() {
    std::vector<DialogInbox::Result> results;
    {
        std::lock_guard<std::mutex> lock(inbox_->mutex);
        results.swap(inbox_->results);
    }
    for (DialogInbox::Result& r : results) {
        if (!r.error.empty()) {
            ShowNotice("The file dialog failed: " + r.error, true);
            continue;
        }
        if (r.paths.empty()) continue;   // cancelled
        if (r.purpose == DialogPurpose::Import) {
            OfferPaths(r.paths);
        } else {
            std::string err;
            if (backend_.ChangeDataFolder(r.paths.front(), err)) {
                settingsMessage_ = "The data folder is now the one you chose.";
                RefreshStatus();
            } else {
                settingsMessage_ = "That folder cannot be used: " + err;
            }
        }
    }
}

// ---- events

int LauncherUi::DropTargetAt(float x, float y) const {
    if (tilesShown_)
        for (int i = 0; i < 3; ++i)
            if (tileRects_[i].Contains(x, y)) return i;
    return -1;   // anywhere else in the window takes the drop too
}

void LauncherUi::HandleEvent(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_MOUSE_MOTION:
            input_.mouseX = e.motion.x;
            input_.mouseY = e.motion.y;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button != SDL_BUTTON_LEFT) break;
            input_.mouseX = e.button.x;
            input_.mouseY = e.button.y;
            input_.down = input_.pressed = true;
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button != SDL_BUTTON_LEFT) break;
            input_.mouseX = e.button.x;
            input_.mouseY = e.button.y;
            input_.down = false;
            input_.released = true;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            input_.wheel += e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            input_.mouseX = input_.mouseY = -1e6f;
            break;
        case SDL_EVENT_TEXT_INPUT:
            if (onlineServerEditing_ && e.text.text != nullptr) input_.typed += e.text.text;
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            gameSettingsStale_ = true;   // the window is back: the game may have changed its files (RefreshGameSettings)
            break;
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
            frameRateTopStale_ = true;   // the Frame rate limit slider's top is read again (DrawSettings)
            break;
        case SDL_EVENT_KEY_DOWN:
            if (onlineServerEditing_) {   // the Online server field has the keyboard
                if (e.key.key == SDLK_BACKSPACE) {
                    ++input_.erased;
                } else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER || e.key.key == SDLK_ESCAPE) {
                    input_.entered = true;
                } else if (e.key.key == SDLK_V && (e.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) != 0) {
                    if (char* pasted = SDL_GetClipboardText()) {
                        input_.typed += pasted;
                        SDL_free(pasted);
                    }
                }
                break;
            }
            if (e.key.key == SDLK_ESCAPE) {
                if (languageOpen_ >= 0) languageOpen_ = -1;
                else if (gamePlayerOpen_) gamePlayerOpen_ = false;
                else if (editedOpen_ >= 0) editedOpen_ = -1;
                else if (armedRemove_ >= 0) armedRemove_ = -1;
                else if (aboutOpen_) aboutOpen_ = false;
                else if (settingsOpen_) settingsOpen_ = false;
                else if (modsOpen_) modsOpen_ = false;
                else if (screen_ == Screen::Result || screen_ == Screen::Play) screen_ = Screen::Home;
            }
            break;
        case SDL_EVENT_DROP_BEGIN:
            dragging_ = true;
            dropped_.clear();
            dropTarget_ = -1;
            break;
        case SDL_EVENT_DROP_POSITION:
            dragging_ = true;
            dropTarget_ = DropTargetAt(e.drop.x, e.drop.y);
            break;
        case SDL_EVENT_DROP_FILE:
            // Some systems send the files with no DROP_BEGIN first (macOS "Open With" and Dock drops).
            dragging_ = true;
            if (e.drop.data) dropped_.emplace_back(e.drop.data);
            break;
        case SDL_EVENT_DROP_COMPLETE: {
            // Also sent when a drag leaves the window without dropping anything.
            std::vector<std::string> paths;
            paths.swap(dropped_);
            dragging_ = false;
            dropTarget_ = -2;
            if (!paths.empty()) OfferPaths(paths);
            break;
        }
        case SDL_EVENT_DROP_TEXT:   // text is not a disc
        default:
            break;
    }
}

// ---- the import

void LauncherUi::OfferPaths(const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    if (verify_.valid()) {
        ShowNotice("Wait for the check of an edition to finish", true);
        return;
    }
    aboutOpen_ = settingsOpen_ = modsOpen_ = false;
    editedOpen_ = -1;
    armedRemove_ = -1;
    if (backend_.Running()) {
        queued_.push_back(paths);
        ShowNotice("Added: checked as soon as this one is done", false);
        return;
    }
    StartImport(paths);
}

bool LauncherUi::StartImport(const std::vector<std::string>& paths) {
    if (!roundStarted_) {
        for (int i = 0; i < 6; ++i) before_[i] = all_[i];
        round_ = ImportReport();
        log_.clear();
    }
    counter_ = ImportProgress();
    scanning_ = true;
    std::string err;
    if (!backend_.Start(paths, err)) {
        ShowNotice("Cannot check that: " + err, true);
        return false;
    }
    roundStarted_ = true;
    screen_ = Screen::Checking;
    return true;
}

void LauncherUi::PollImport() {
    if (screen_ != Screen::Checking) return;
    auto take = [this](const std::vector<ImportProgress>& events) {
        for (const ImportProgress& p : events) {
            if (p.kind == ImportProgress::Kind::Scanning || p.kind == ImportProgress::Kind::Hashing) {
                counter_ = p;
                scanning_ = p.kind == ImportProgress::Kind::Scanning;
            }
            LogRow row;
            if (DescribeProgress(p, row)) log_.push_back({row.path, row.status, ToneColour(row.tone)});
        }
        if (log_.size() > kLogLimit) log_.erase(log_.begin(), log_.begin() + (log_.size() - kLogLimit));
    };
    take(backend_.Drain());
    ImportProgress latest = backend_.Latest();
    if (latest.kind == ImportProgress::Kind::Hashing || latest.kind == ImportProgress::Kind::Scanning) {
        counter_ = latest;
        scanning_ = latest.kind == ImportProgress::Kind::Scanning;
    }
    if (backend_.Running()) return;
    ImportReport report = backend_.Wait();
    take(backend_.Drain());
    FinishImport(report);
}

void LauncherUi::FinishImport(const ImportReport& report) {
    Merge(round_, report);
    // Drops made while it ran, one after the other; the result covers them all.
    while (!queued_.empty() && !report.cancelled && !report.aborted) {
        std::vector<std::string> next = std::move(queued_.front());
        queued_.erase(queued_.begin());
        if (StartImport(next)) return;
    }
    queued_.clear();
    RefreshStatus();
    RefreshLanguages();
    for (int i = 0; i < 6; ++i) round_.status[i] = all_[i];
    result_ = DescribeResult(round_, before_);
    roundStarted_ = false;
    screen_ = Screen::Result;
}

void LauncherUi::RefreshStatus() {
    for (Edition e : {assets::kEditionUs, assets::kEditionEur, assets::kEditionGold, assets::kEditionLanguages,
                      assets::kEditionUsPc, assets::kEditionEurPc})
        all_[StatusIndex(e)] = backend_.Status(e);
    for (int i = 0; i < 3; ++i) status_[i] = SlotStatus(all_, i);
}

void LauncherUi::RefreshLanguages() {
    languages_ = backend_.Languages();
}

std::vector<LanguageChoice> LauncherUi::LanguageChoicesFor(int index) const {
    return LanguageChoices(kEditions[index], languages_);
}

bool LauncherUi::AnyPlayable() const {
    return status_[0].unlocked || status_[1].unlocked;
}

bool LauncherUi::AllConfirmed() const {
    return status_[0].unlocked && status_[1].unlocked && status_[2].unlocked;
}

void LauncherUi::OpenPlay() {
    if (!status_[playIndex_].unlocked) playIndex_ = status_[1].unlocked ? 1 : 0;
    screen_ = Screen::Play;
}

void LauncherUi::StartPlay(int index, bool gold) {
    playIndex_ = index;
    choice_.edition = status_[index].edition;   // the tile's Mac edition, or its Windows one
    choice_.gold = gold;
    choice_.skipIntro = skipIntro_;
    choice_.backToLauncher = backToLauncher_;
    choice_.language = PlayLanguage(kEditions[index], languages_, language_[index]);
    playChosen_ = true;
    std::string err;
    backend_.SetLastChoice(assets::EditionName(choice_.edition), err);
}

void LauncherUi::PollVerify() {
    if (!verify_.valid()) return;
    if (verify_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    VerifyResult r = verify_.get();
    const char* title = EditionTitle(status_[verifyIndex_].edition);
    if (r.removed < 0)
        settingsMessage_ = std::string("Checking the ") + title + " again failed: " + r.err;
    else if (r.removed == 0)
        settingsMessage_ = std::string("Checked again: every file of the ") + title + " is still right.";
    else
        settingsMessage_ = std::string("Checked again: ") + Files(static_cast<uint64_t>(r.removed)) +
                           " no longer matched and were removed. Drag the disc in again to replace them.";
    verifyIndex_ = -1;
    RefreshStatus();
    RefreshLanguages();
}

// ---- test hooks

void LauncherUi::ShowScreen(Screen screen) {
    screen_ = screen;
    if (screen == Screen::Play) OpenPlay();
}

void LauncherUi::ShowAbout(bool open) {
    aboutOpen_ = open;
}

void LauncherUi::ShowEdited(int index) {
    editedOpen_ = index >= 0 && index < 3 ? index : -1;
    editedFiles_ = editedOpen_ >= 0 ? backend_.EditedFiles(kEditions[editedOpen_]) : std::vector<assets::EditedFile>{};
}

void LauncherUi::ShowSettings(bool open) {
    settingsOpen_ = open;
    if (!open) onlineServerEditing_ = false;
    armedRemove_ = -1;
    languageOpen_ = -1;
    gamePlayerOpen_ = false;
    if (open) RefreshLanguages();
}

void LauncherUi::ShowMods(bool open) {
    modsOpen_ = open && modsShown_;
}

void LauncherUi::ShowLanguageList(int index) {
    settingsOpen_ = settingsOpen_ || index >= 0;
    languageOpen_ = index;
}

void LauncherUi::ArmRemove(Edition edition) {
    settingsOpen_ = true;
    armedRemove_ = EditionIndex(edition);
}

void LauncherUi::ShowNotice(const std::string& message, bool problem) {
    notice_ = message;
    noticeProblem_ = problem;
    noticeLeft_ = kNoticeSeconds;
}

void LauncherUi::ChoosePlay(Edition edition, bool gold) {
    int i = EditionIndex(edition);
    if (i > 1) return;
    playIndex_ = i;
    SetGold(i, gold);
}

void LauncherUi::SetMods(const std::vector<ModRow>& mods) {
    modsShown_ = true;
    mods_ = mods;
    modOn_.clear();
    for (const ModRow& mod : mods_) modOn_.push_back(backend_.Setting(ModKey(mod.id)) == "1");
}

int LauncherUi::ModsOn() const {
    return static_cast<int>(std::count(modOn_.begin(), modOn_.end(), true));
}

bool LauncherUi::ModAllowed(size_t index, std::string& why) const {
    why.clear();
    const ModRow& mod = mods_[index];
    bool edition = false;
    for (int i = 0; i < 3; ++i) edition = edition || (mod.editions[i] && status_[i].unlocked);
    if (!edition) {
        why = "None of the editions it is for is in the data folder yet.";
        return false;
    }
    for (size_t j = 0; j < mods_.size(); ++j) {
        if (j == index || !modOn_[j]) continue;
        const ModRow& other = mods_[j];
        const bool clash = std::find(mod.conflicts.begin(), mod.conflicts.end(), other.id) != mod.conflicts.end() ||
                           std::find(other.conflicts.begin(), other.conflicts.end(), mod.id) != other.conflicts.end();
        if (clash) {
            why = "It cannot be on with " + other.name + ", which is on.";
            return false;
        }
    }
    return true;
}

void LauncherUi::SetGold(int index, bool on) {
    gold_[index] = on;
    std::string err;
    if (!backend_.SaveSetting(GoldKey(kEditions[index]), on ? "1" : "0", err))
        ShowNotice("Cannot keep the Gold Edition switch: " + err, true);
}

bool LauncherUi::Animating() const {
    return ui_.Animating() || dragging_ || noticeLeft_ > 0 || screen_ == Screen::Checking || verify_.valid() ||
           (updater_ != nullptr && updater_->Moving());
}

void LauncherUi::SetUpdater(Updater* updater) {
    // No line about the check on the first launch: Settings and the README say what it does (the maintainer, 2026-10-25: a popup
    // nobody asked for looks sketchy).
    updater_ = updater;
}

bool LauncherUi::TakeQuit() {
    return updater_ != nullptr && updater_->TakeQuit();
}

bool LauncherUi::TakePlay(LaunchChoice& out) {
    if (!playChosen_) return false;
    playChosen_ = false;
    out = choice_;
    return true;
}

// ---- drawing

TileView LauncherUi::TileFor(int i) const {
    const EditionStatus& s = status_[i];
    TileText t = DescribeSlot(all_, i);
    TileView v;
    v.title = EditionTitle(s.edition);
    v.game = EditionGame(s.edition);
    v.state = t.state;
    v.detail = t.detail;
    v.accent = kAccents[i];
    v.badge = s.unlocked ? Badge::Check : s.present ? Badge::Partial : Badge::Lock;
    v.fraction = s.files ? static_cast<float>(s.filesPresent) / static_cast<float>(s.files) : 0.0f;
    return v;
}

void LauncherUi::DrawBackdrop(const Rect& w) {
    canvas_.VerticalGradient(w, colour::kSkyTop, colour::kSkyBottom);
    auto sky = [&](float y) { return Mix(colour::kSkyTop, colour::kSkyBottom, y / std::max(1.0f, w.h)); };
    Cloud(canvas_, w.w * 0.5f, 48, 0.8f, Mix(sky(48), colour::kPaper, 0.6f));
    Cloud(canvas_, w.w * 0.06f, w.h * 0.6f, 1.3f, Mix(sky(w.h * 0.6f), colour::kPaper, 0.7f));
    Cloud(canvas_, w.w * 0.96f, w.h * 0.36f, 1.1f, Mix(sky(w.h * 0.36f), colour::kPaper, 0.7f));

    // Bunting: a string sagging between posts every ~190 points, with pennants hanging from it.
    static constexpr Rgba kPennants[4] = {colour::kOrange, colour::kYellow, colour::kLime, colour::kRed};
    int spans = std::max(2, static_cast<int>(std::round(w.w / 190)));
    float spanW = w.w / spans;
    int pennant = 0;
    for (int s = 0; s < spans; ++s) {
        float x0 = s * spanW;
        auto at = [&](float t) {
            return Point{x0 + spanW * t, 2 + 8 * 4 * t * (1 - t)};
        };
        std::vector<Point> rope;
        for (int i = 0; i <= 16; ++i) rope.push_back(at(i / 16.0f));
        int count = std::max(3, static_cast<int>(spanW / 30));
        for (int i = 0; i < count; ++i) {
            float t = (i + 0.5f) / count;
            Point a = at(t - 0.32f / count), b = at(t + 0.32f / count), m = at(t);
            Point tip{m.x, m.y + kBuntingBottom - 10};
            canvas_.FillTriangle({a.x - 2, a.y - 1}, {b.x + 2, b.y - 1}, {tip.x, tip.y + 3}, colour::kInk);
            canvas_.FillTriangle(a, b, tip, kPennants[pennant++ % 4]);
        }
        canvas_.Polyline(rope, 2, colour::kInk);
    }
}

// The title row: the product's name large, and the launcher's version just to its right in a much smaller face, on the
// same baseline, in the title's sticker look (white with a dark outline) so it reads on the sky. The header's buttons
// keep to the right of it (TitleRect, TitleVersionRect and HeaderButtonsRect are the test's view of the row).
float LauncherUi::DrawHeader(const Rect& w, bool buttons) {
    const float titleX = metric::kMargin + 2;
    const float titleW = text_.Width(style::kTitle, kProductName);
    text_.DrawOutlined(style::kTitle, titleX, kTitleTop, kProductName, colour::kPaper, colour::kInk, 3);
    titleRect_ = {titleX, kTitleTop, titleW, text_.LineHeight(style::kTitle)};
    const float versionW = text_.Width(kTitleVersionStyle, kVersionLabel);
    const float versionY = kTitleTop + text_.Ascent(style::kTitle) - text_.Ascent(kTitleVersionStyle);
    titleVersionRect_ = {titleRect_.Right() + kTitleVersionGap, versionY, versionW, text_.LineHeight(kTitleVersionStyle)};
    text_.DrawOutlined(kTitleVersionStyle, titleVersionRect_.x, titleVersionRect_.y, kVersionLabel, colour::kPaper,
                       colour::kInk, 2);
    headerButtonsRect_ = Rect{};
    if (buttons) {
        float h = 36, y = kTitleTop + 8;
        float x = w.Right() - metric::kMargin;
        float aw = ui_.ButtonWidth("About");
        x -= aw;
        if (ui_.Button("about", {x, y, aw, h}, "About", ButtonKind::Quiet)) aboutOpen_ = true;
        float sw = ui_.ButtonWidth("Settings");
        x -= sw + 10;
        if (ui_.Button("settings", {x, y, sw, h}, "Settings", ButtonKind::Quiet)) {
            settingsOpen_ = true;
            armedRemove_ = -1;
            RefreshLanguages();
        }
        if (modsShown_) {
            // While a mod is on the button says how many ("1 mod on"), in the yellow of a choice made: the game is not
            // the original then.
            const int on = ModsOn();
            const std::string label = on == 0 ? "Mods" : std::to_string(on) + (on == 1 ? " mod on" : " mods on");
            float mw = ui_.ButtonWidth(label);
            x -= mw + 10;
            modsButtonRect_ = {x, y, mw, h};
            if (on > 0) modsOnRect_ = modsButtonRect_;
            if (ui_.Button("mods", modsButtonRect_, label, on == 0 ? ButtonKind::Quiet : ButtonKind::Plain))
                modsOpen_ = true;
        }
        // The header's Play only where it clears the title and its version (the tiles have their own Play buttons).
        float pw = ui_.ButtonWidth("Play") + 20;
        bool fits = x - pw - 10 > titleVersionRect_.Right() + 16;
        if (screen_ == Screen::Home && AnyPlayable() && fits) {
            x -= pw + 10;
            if (ui_.Button("header-play", {x, y, pw, h}, "Play", ButtonKind::Go)) OpenPlay();
        }
        headerButtonsRect_ ={x, y, w.Right() - metric::kMargin - x, h};
    }
    return kContentTop;
}

// The update banner (updater.h) under the title on both homes, while there is something to say: the version the check
// found, with What's new and Update; the update's steps while it builds; "Restart to update" once it is built; why it did
// not finish. Returns where the screen goes on below it.
float LauncherUi::DrawUpdateBanner(const Rect& w, float top) {
    updateBannerRect_ = Rect{};
    if (updater_ == nullptr || !updater_->Visible()) return top;
    using Phase = Updater::Phase;
    const Phase phase = updater_->CurrentPhase();
    const float m = metric::kMargin, bh = 30;
    const Rect r{m, top, w.w - 2 * m, kBannerHeight};
    updateBannerRect_ = r;
    const Rgba fill = phase == Phase::Failed ? Mix(colour::kRed, colour::kPaper, 0.55f) : colour::kYellowSoft;
    canvas_.Outlined(r, 12, 2.5f, fill, colour::kInk);
    const float by = r.CentreY() - bh * 0.5f;
    float x = r.Right() - 8;   // the right end of what is still free, buttons placed from the right
    auto button = [&](const char* id, const char* label, ButtonKind kind, bool enabled) {
        const float bw = ui_.ButtonWidth(label);
        x -= bw;
        const bool pressed = ui_.Button(id, {x, by, bw, bh}, label, kind, enabled);
        x -= 8;
        return pressed;
    };
    auto openPage = [&](const std::string& url) {
        if (!SDL_OpenURL(url.c_str())) ShowNotice(std::string("Could not open the page: ") + SDL_GetError(), true);
    };
    std::string line;
    switch (phase) {
        case Phase::Probing:
        case Phase::Staging:
            line = phase == Phase::Probing ? std::string("Looking for the update") : "Updating: " + updater_->StepText();
            x -= 140;
            ui_.ProgressBar({x, r.CentreY() - 9, 140, 18}, -1);
            x -= 12;
            break;
        case Phase::Staged:
            line = "Version " + updater_->StagedVersion() + " is ready";
            if (button("update-restart", "Restart to update", ButtonKind::Go, true)) updater_->Restart();
            break;
        case Phase::Failed:
            line = "Not updated: " + updater_->FailReason();
            if (button("update-ok", "OK", ButtonKind::Quiet, true)) updater_->Dismiss();
            if (!logsFolder_.empty() && button("update-logs", "Show logs", ButtonKind::Quiet, true) &&
                !(showLogs_ != nullptr && showLogs_(logsFolder_)) && !SDL_OpenURL(FileUrl(logsFolder_).c_str()))
                ShowNotice(std::string("Could not open the logs folder: ") + SDL_GetError(), true);
            break;
        default: {
            line = "Version " + updater_->Found() + " is out";
            // A launcher installed by the install script updates itself; the Flatpak (Linux) is updated by the themepark
            // command on the host, outside the sandbox, so it says so; any other opens the release page.
            if (button("update-go", "Update", ButtonKind::Go, !updater_->Offline() && phase == Phase::Idle)) {
                if (updater_->ScriptInstall()) updater_->Update();
#ifdef __linux__
                else if (std::getenv("FLATPAK_ID") != nullptr)
                    ShowNotice("To update, open a terminal and type: themepark update", false);
#endif
                else openPage(updater_->WhatsNewUrl());
            }
            if (button("update-whats-new", "What's new", ButtonKind::Quiet, true)) openPage(updater_->WhatsNewUrl());
            if (updater_->Offline()) {
                const char* offline = "No internet connection";
                const float ow = text_.Width(style::kSmallBold, offline);
                x -= ow;
                text_.Draw(style::kSmallBold, x, r.CentreY() - text_.LineHeight(style::kSmallBold) * 0.5f, offline,
                           colour::kInkSoft);
                x -= 12;
            }
            break;
        }
    }
    const float tx = r.x + 14;
    text_.Draw(style::kBodyBold, tx, r.CentreY() - text_.LineHeight(style::kBodyBold) * 0.5f,
               text_.Fit(style::kBodyBold, line, std::max(0.0f, x - tx), false), colour::kInk);
    return r.Bottom() + metric::kGap;
}

float LauncherUi::TileHeight() const {
    for (const EditionStatus& s : status_)
        if (s.edited > 0) return kTileHeight + kEditedBadgeH + 8;
    return kTileHeight;
}

// The edition keeps .sam settings files the player edited (tpw-port #94): a badge that says so and opens the list. It
// only explains: the game reads the files as they are and Play works as normal.
void LauncherUi::EditedBadge(int i, const Rect& r) {
    if (status_[i].edited <= 0) return;
    editedBadge_[i] = r;
    if (ui_.Button(std::string("edited-") + assets::EditionName(kEditions[i]), r, kEditedLabel, ButtonKind::Plain, true,
                   style::kSmallBold))
        ShowEdited(i);
}

void LauncherUi::DrawTiles(const Rect& row, bool dropTargets) {
    float tw = (row.w - 2 * metric::kGap) / 3;
    const float bw = text_.Width(style::kSmallBold, kEditedLabel) + 28;
    for (int i = 0; i < 3; ++i) {
        Rect r{row.x + i * (tw + metric::kGap), row.y, tw, row.h};
        tileRects_[i] = r;
        TileView v = TileFor(i);
        v.highlight = dropTargets && dragging_ && dropTarget_ == i;
        if (status_[i].edited > 0) v.footer = kEditedBadgeH + 8;
        ui_.Tile(std::string("tile-") + assets::EditionName(kEditions[i]), r, v);
        EditedBadge(i, {r.x + 16, r.Bottom() - 16 - kEditedBadgeH, std::min(bw, r.w - 32), kEditedBadgeH});
    }
    tilesShown_ = dropTargets;
}

// The footer of both homes: the descriptive line and the disclaimer, word for word, with the launcher's version in the
// bottom right corner, small and soft, beside the disclaimer's last line (which wraps short of it), out of the way of
// the tiles and their Play buttons above. A player reads it from there for a bug report.
Rect LauncherUi::DrawFooter(const Rect& w) {
    const float m = metric::kMargin;
    const std::string version = kVersionLabel;
    const float vw = text_.Width(style::kSmall, version);
    float footerW = w.w - 2 * m - 24;
    std::vector<std::string> disclaimer = text_.Wrap(style::kSmall, kDisclaimer, footerW - vw - 16);
    std::vector<std::string> tagline = text_.Wrap(style::kSmallBold, kTagline, footerW);
    float lh = text_.LineHeight(style::kSmall);
    float footerH = (disclaimer.size() + tagline.size()) * lh + 18;
    Rect footer{m, w.Bottom() - m - footerH, w.w - 2 * m, footerH};
    canvas_.FillRound(footer, 12, colour::kPaper.WithAlpha(215));
    float y = footer.y + 9;
    for (const std::string& line : tagline) {
        text_.Draw(style::kSmallBold, footer.x + 12, y, line, colour::kInk);
        y += lh;
    }
    for (const std::string& line : disclaimer) {
        text_.Draw(style::kSmall, footer.x + 12, y, line, colour::kInkSoft);
        y += lh;
    }
    versionRect_ = {footer.Right() - 12 - vw, y - lh, vw, lh};
    text_.Draw(style::kSmall, versionRect_.x, versionRect_.y, version, colour::kInkSoft);
    return footer;
}

void LauncherUi::DrawHome(const Rect& w) {
    if (AllConfirmed()) {
        DrawReady(w);
        return;
    }
    float top = DrawUpdateBanner(w, DrawHeader(w, true));
    const float m = metric::kMargin;

    // The footer: the descriptive line and the disclaimer, word for word, and the version.
    const Rect footer = DrawFooter(w);

    Rect tiles{m, footer.y - metric::kGap - TileHeight(), w.w - 2 * m, TileHeight()};
    Rect field{m, top, w.w - 2 * m, tiles.y - metric::kGap - top - 4};
    fieldRect_ = field;
    bool fieldHover = dragging_ && dropTarget_ == -1;

    // The field's content, centred: icon, heading, one line, the two Browse buttons.
    const float iconH = 44, buttonH = 38;
    float headH = text_.LineHeight(style::kHeading), subH = text_.LineHeight(style::kSmall);
    float block = iconH + 10 + headH + 4 + subH + 14 + buttonH;
    // Under the update banner the field can be short: the icon goes, the words and the buttons stay.
    const bool icon = field.h >= block + 28;
    if (!icon) block -= iconH + 10;
    float cy = field.y + std::max(14.0f, (field.h - block) * 0.5f);
    ui_.DropField("field", field, fieldHover, cy, icon);
    if (icon) cy += iconH + 10;
    const char* heading = fieldHover ? "Let go to check them" : "Drop your game discs, images, zips or folders here";
    text_.DrawCentred(style::kHeading, {field.x, cy, field.w, headH},
                      text_.Fit(style::kHeading, heading, field.w - 40, false), colour::kInk);
    cy += headH + 4;
    text_.DrawCentred(style::kSmall, {field.x, cy, field.w, subH},
                      text_.Fit(style::kSmall,
                                "Every file is checked by its content; only the ones the games use are copied.",
                                field.w - 40, false),
                      colour::kInkSoft);
    cy += subH + 14;
    float fw = ui_.ButtonWidth("Choose files..."), dw = ui_.ButtonWidth("Choose a folder...");
    float bx = field.CentreX() - (fw + dw + 12) * 0.5f;
    if (ui_.Button("browse-files", {bx, cy, fw, buttonH}, "Choose files...", ButtonKind::Plain))
        Browse(false, DialogPurpose::Import);
    if (ui_.Button("browse-folder", {bx + fw + 12, cy, dw, buttonH}, "Choose a folder...", ButtonKind::Plain))
        Browse(true, DialogPurpose::Import);

    DrawTiles(tiles, true);

    // A playable edition's own Play, in its tile's header band: there at every window size, where the header's gives way
    // to Mods, Settings and About, and without taking height from the drop field. This home has no Gold Edition switch, so
    // the Gold rides go by the edition's kept switch once their data is in, as on the Play screen.
    const float playW = text_.Width(style::kButton, "Play") + 24, playH = 30;
    for (int i = 0; i < 2; ++i) {
        if (!status_[i].unlocked) continue;
        const Rect& t = tileRects_[i];
        tilePlay_[i] = {t.Right() - metric::kOutline - 6 - playW, t.y + metric::kOutline + 4, playW, playH};
        if (ui_.Button(std::string("tile-play-") + assets::EditionName(kEditions[i]), tilePlay_[i], "Play", ButtonKind::Go))
            StartPlay(i, status_[2].unlocked && gold_[i]);
    }
}

// The home once every edition is confirmed: no drop field, two large tiles (the US edition, then the European one), each
// with its Gold Edition switch and its Play button. The whole window still takes a drop; the tiles light up under it as the small
// ones do, and "Game data" opens Settings.
void LauncherUi::DrawReady(const Rect& w) {
    float top = DrawUpdateBanner(w, DrawHeader(w, true));
    const float m = metric::kMargin;

    // The footer, as on the home with the drop field.
    const Rect footer = DrawFooter(w);

    // The row above the footer: "Game data" on the left; while a drag is over the window, what letting go does.
    const float rowH = 34;
    Rect row{m, footer.y - metric::kGap - rowH, w.w - 2 * m, rowH};
    float gw = ui_.ButtonWidth("Game data");
    if (ui_.Button("game-data", {row.x, row.y, gw, rowH}, "Game data", ButtonKind::Quiet)) {
        settingsOpen_ = true;
        armedRemove_ = -1;
        RefreshLanguages();
    }
    if (dragging_) {
        float tx = row.x + gw + 16;
        text_.Draw(style::kBodyBold, tx, row.y + (rowH - text_.LineHeight(style::kBodyBold)) * 0.5f,
                   text_.Fit(style::kBodyBold, "Let go to check them", row.Right() - tx, false), colour::kInk);
    }

    // The two tiles: US on the left, European on the right (Sim Theme Park is always named first, the maintainer, 2026-10-25).
    Rect area{m, top, w.w - 2 * m, row.y - metric::kGap - top};
    float tw = (area.w - metric::kGap) * 0.5f;
    static constexpr int kOrder[2] = {0, 1};   // status_ index of the left and the right tile
    static constexpr const char* kGames[2] = {"Sim Theme Park", "Theme Park World"};   // by status_ index
    const float pad = 16, playH = 52, pillH = 34;
    tileRects_[2] = Rect{};
    for (int k = 0; k < 2; ++k) {
        int i = kOrder[k];
        Rect r{area.x + k * (tw + metric::kGap), area.y, tw, area.h};
        tileRects_[i] = r;
        TileView v;
        v.title = kGames[i];
        v.game = std::string(EditionTitle(status_[i].edition)) +
                 (assets::IsWindowsEdition(status_[i].edition) ? ", from the Windows disc" : "");
        v.state = "Ready to play";
        v.detail = Files(status_[i].filesPresent) + " confirmed";
        const std::string note = WindowsEditionNote(status_[i].edition);
        if (!note.empty()) v.detail += ". " + note + ".";
        v.accent = kAccents[i];
        v.badge = Badge::Check;
        v.fraction = 1;
        v.highlight = dragging_ && dropTarget_ == i;
        ui_.Tile(std::string("ready-") + assets::EditionName(kEditions[i]), r, v);

        // Inside, at the bottom: the Gold Edition switch (this home shows only once the Gold bonus rides are confirmed
        // too; off plays the plain edition), then the Play button. The switch is the Play screen's card's: one state an
        // edition (gold_), kept in launcher.ini. The pill sits 12 points above the button, so the two never overlap.
        Rect play{r.x + pad, r.Bottom() - pad - playH, r.w - 2 * pad, playH};
        const char* goldText = "Gold Edition";
        const float toggleW = 44, toggleH = pillH - 6;
        float pw = std::min(play.w, text_.Width(style::kSmallBold, goldText) + 14 + 10 + toggleW + 3);
        Rect pill{r.CentreX() - pw * 0.5f, play.y - 12 - pillH, pw, pillH};
        bool& goldOn = gold_[i];
        canvas_.Outlined(pill, pillH * 0.5f, 2, goldOn ? colour::kYellowSoft : colour::kCream, colour::kInk);
        Rect toggle{pill.Right() - 3 - toggleW, pill.CentreY() - toggleH * 0.5f, toggleW, toggleH};
        float tx = pill.x + 14;
        text_.Draw(style::kSmallBold, tx, pill.y + (pillH - text_.LineHeight(style::kSmallBold)) * 0.5f,
                   text_.Fit(style::kSmallBold, goldText, toggle.x - 10 - tx, false), colour::kInk);
        readyGold_[i] = toggle;
        if (ui_.Toggle(std::string("gold-") + assets::EditionName(kEditions[i]), toggle, goldOn)) SetGold(i, goldOn);
        // Above the switch: the Customized settings badge, when the edition keeps edited .sam files.
        const float badgeH = 30, bw = std::min(play.w, text_.Width(style::kSmallBold, kEditedLabel) + 32);
        EditedBadge(i, {r.CentreX() - bw * 0.5f, pill.y - 12 - badgeH, bw, badgeH});
        readyPlay_[i] = play;
        if (ui_.Button(std::string("ready-play-") + assets::EditionName(kEditions[i]), play, "Play", ButtonKind::Go,
                       true, style::kHeadline))
            StartPlay(i, goldOn);
    }
    tilesShown_ = true;
    readyShown_ = true;
}

void LauncherUi::DrawChecking(const Rect& w) {
    float top = DrawHeader(w, false);
    const float m = metric::kMargin, pad = 20;
    Rect panel{m, top, w.w - 2 * m, w.Bottom() - m - top};
    ui_.Panel(panel, colour::kYellowSoft, colour::kYellow);
    Rect in = panel.Inset(pad);
    float y = in.y;
    std::string head = "Checking your files";
    if (!queued_.empty()) head += ", " + FormatCount(queued_.size()) + " more to go";
    text_.Draw(style::kHeadline, in.x, y, text_.Fit(style::kHeadline, head, in.w, false), colour::kInk);
    y += text_.LineHeight(style::kHeadline) + 6;

    std::string count, bytes;
    float fraction = -1;
    if (scanning_) {
        count = "Opening what you dropped: " + Files(counter_.total) + " found";
    } else if (counter_.total) {
        count = FormatCount(counter_.n) + " of " + Files(counter_.total);
        if (counter_.totalBytes) {
            bytes = FormatBytes(counter_.bytes) + " of " + FormatBytes(counter_.totalBytes);
            fraction = static_cast<float>(static_cast<double>(counter_.bytes) / counter_.totalBytes);
        } else {
            fraction = static_cast<float>(counter_.n) / counter_.total;
        }
    } else {
        count = "Starting";
    }
    text_.Draw(style::kBodyBold, in.x, y, count, colour::kInk);
    if (!bytes.empty())
        text_.Draw(style::kBody, in.Right() - text_.Width(style::kBody, bytes), y, bytes, colour::kInk);
    y += text_.LineHeight(style::kBody) + 2;
    text_.Draw(style::kSmall, in.x, y, text_.Fit(style::kSmall, counter_.path, in.w, true), colour::kInkSoft);
    y += text_.LineHeight(style::kSmall) + 8;
    ui_.ProgressBar({in.x, y, in.w, 26}, fraction);
    y += 26 + 12;

    const float buttonH = 38;
    Rect log{in.x, y, in.w, in.Bottom() - buttonH - 12 - y};
    ui_.LogList("log", log, log_);
    float cw = ui_.ButtonWidth("Cancel");
    float by = in.Bottom() - buttonH;
    text_.Draw(style::kSmall, in.x, by + (buttonH - text_.LineHeight(style::kSmall)) * 0.5f,
               text_.Fit(style::kSmall, "Files the games do not use are counted, not listed.", in.w - cw - 16, false),
               colour::kInk);
    if (ui_.Button("cancel", {in.Right() - cw, by, cw, buttonH}, "Cancel", ButtonKind::Danger)) {
        queued_.clear();
        backend_.Cancel();
    }
}

void LauncherUi::DrawResult(const Rect& w) {
    float top = DrawHeader(w, true);
    const float m = metric::kMargin, pad = 20;
    Rect tiles{m, w.Bottom() - m - TileHeight(), w.w - 2 * m, TileHeight()};
    Rect panel{m, top, w.w - 2 * m, tiles.y - metric::kGap - 4 - top};
    ui_.Panel(panel, colour::kYellowSoft, colour::kYellow);
    Rect in = panel.Inset(pad);

    const float badgeR = 24;
    float bx = in.x + badgeR, by = in.y + badgeR;
    if (result_.good) {
        ui_.DrawBadge(Badge::Check, bx, by, badgeR, 1);
    } else {
        canvas_.FillCircle(bx, by, badgeR, colour::kInk);
        canvas_.FillCircle(bx, by, badgeR - 2.5f, colour::kOrange);
        canvas_.Polyline({{bx, by - badgeR * 0.45f}, {bx, by + badgeR * 0.1f}}, 5, colour::kPaper);
        canvas_.FillCircle(bx, by + badgeR * 0.42f, 3, colour::kPaper);
    }
    float tx = in.x + 2 * badgeR + 16, tw = in.Right() - tx;
    float y = in.y + 2;
    y += text_.DrawWrapped(style::kHeadline, tx, y, tw, result_.headline, colour::kInk) + 6;

    const float buttonH = 38;
    float limit = in.Bottom() - buttonH - 8;
    float lh = text_.LineHeight(style::kBody);
    for (const std::string& line : result_.lines) {
        std::vector<std::string> wrapped = text_.Wrap(style::kBody, line, tw - 18);
        if (y + lh * wrapped.size() > limit) break;
        canvas_.FillCircle(tx + 5, y + lh * 0.5f, 3.5f, colour::kOrangeDeep);
        for (const std::string& part : wrapped) {
            text_.Draw(style::kBody, tx + 18, y, part, colour::kInk);
            y += lh;
        }
        y += 2;
    }

    float x = in.Right();
    float by2 = in.Bottom() - buttonH;
    if (AnyPlayable()) {
        float pw = ui_.ButtonWidth("Play") + 30;
        x -= pw;
        if (ui_.Button("result-play", {x, by2, pw, buttonH}, "Play", ButtonKind::Go)) OpenPlay();
        x -= 12;
    }
    float dw = ui_.ButtonWidth("Drop more discs");
    x -= dw;
    if (ui_.Button("result-more", {x, by2, dw, buttonH}, "Drop more discs", ButtonKind::Plain))
        screen_ = Screen::Home;

    DrawTiles(tiles, true);
}

void LauncherUi::DrawPlay(const Rect& w) {
    float top = DrawHeader(w, true);
    const float m = metric::kMargin;
    Rect panel{m, top, w.w - 2 * m, w.Bottom() - m - top};
    ui_.Panel(panel, colour::kYellowSoft, colour::kYellow);
    float colW = std::min(panel.w - 48, 520.0f);
    const float cardH = 66, playH = 56;
    // A Windows edition's line says what it lacks (WindowsEditionNote), wrapped under the edition's line.
    const std::string note = WindowsEditionNote(status_[playIndex_].edition);
    const std::vector<std::string> noteLines =
        note.empty() ? std::vector<std::string>() : text_.Wrap(style::kSmall, note + ".", std::min(panel.w - 48, 520.0f));
    float contentH = text_.LineHeight(style::kHeadline) + 12 + 46 + 8 + 2 * text_.LineHeight(style::kSmall) + 4 + 14 +
                     cardH + 18 + 6 + playH + noteLines.size() * text_.LineHeight(style::kSmall);
    Rect col{panel.CentreX() - colW * 0.5f, panel.y + 20, colW, panel.h - 40};
    float y = col.y + std::max(0.0f, (col.h - 46 - contentH) * 0.5f);   // 46: the Back button's row

    text_.DrawCentred(style::kHeadline, {col.x, y, col.w, text_.LineHeight(style::kHeadline)}, "Ready to play",
                      colour::kInk);
    y += text_.LineHeight(style::kHeadline) + 12;

    std::vector<std::string> options = {EditionTitle(assets::kEditionUs), EditionTitle(assets::kEditionEur)};
    std::vector<bool> enabled = {status_[0].unlocked, status_[1].unlocked};
    int selected = playIndex_;
    if (ui_.Segmented("edition", {col.x, y, col.w, 46}, options, enabled, selected)) playIndex_ = selected;
    y += 46 + 8;
    const EditionStatus& s = status_[playIndex_];
    std::string line = std::string(EditionGame(s.edition)) + ": " +
                       (s.unlocked ? Files(s.filesPresent) + " confirmed" : DescribeTile(s).detail);
    text_.DrawCentred(style::kSmall, {col.x, y, col.w, text_.LineHeight(style::kSmall)},
                      text_.Fit(style::kSmall, line, col.w, false), colour::kInk);
    y += text_.LineHeight(style::kSmall);
    for (const std::string& l : noteLines) {
        text_.DrawCentred(style::kSmall, {col.x, y, col.w, text_.LineHeight(style::kSmall)}, l, colour::kInkSoft);
        y += text_.LineHeight(style::kSmall);
    }
    y += 4;
    int other = 1 - playIndex_;
    if (!status_[other].unlocked) {
        std::string unlock = std::string("The ") + EditionTitle(kEditions[other]) + ": " + DescribeSlot(all_, other).detail;
        text_.DrawCentred(style::kSmall, {col.x, y, col.w, text_.LineHeight(style::kSmall)},
                          text_.Fit(style::kSmall, unlock, col.w, false), colour::kInkSoft);
    }
    y += text_.LineHeight(style::kSmall) + 14;

    // The Gold rides, a toggle per edition.
    const EditionStatus& gold = status_[2];
    Rect card{col.x, y, col.w, cardH};
    canvas_.Outlined(card, 14, 2.5f, colour::kCream, colour::kInk);
    ui_.DrawBadge(gold.unlocked ? Badge::Check : gold.present ? Badge::Partial : Badge::Lock, card.x + 30,
                  card.CentreY(), 15,
                  gold.files ? static_cast<float>(gold.filesPresent) / static_cast<float>(gold.files) : 0.0f);
    float gx = card.x + 56, gw = card.w - 56 - 80;
    text_.Draw(style::kHeading, gx, card.y + 10, EditionTitle(assets::kEditionGold), colour::kInk);
    std::string goldLine = gold.unlocked ? std::string("Adds the rides of the PC Gold bonus CD to this edition")
                                         : DescribeTile(gold).detail;
    text_.Draw(style::kSmall, gx, card.y + 12 + text_.LineHeight(style::kHeading),
               text_.Fit(style::kSmall, goldLine, gw, false), colour::kInkSoft);
    // The switch is the all-confirmed home's tile's: gold_, kept in launcher.ini. Without the Gold data it shows off and
    // cannot be flipped; the kept state waits for the data.
    bool goldOn = gold.unlocked && gold_[playIndex_];
    if (ui_.Toggle(std::string("gold-") + assets::EditionName(kEditions[playIndex_]),
                   {card.Right() - 16 - 56, card.CentreY() - 16, 56, 32}, goldOn, gold.unlocked))
        SetGold(playIndex_, goldOn);
    y += cardH + 18;

    float playW = 220;
    float bottom = std::max(y + playH, col.Bottom());
    Rect play{col.CentreX() - playW * 0.5f, std::min(y + 6, bottom - playH), playW, playH};
    if (ui_.Button("play", play, "Play", ButtonKind::Go, s.unlocked, style::kHeadline)) StartPlay(playIndex_, goldOn);
    float backW = ui_.ButtonWidth("Back");
    if (ui_.Button("play-back", {panel.x + 20, panel.Bottom() - 20 - 38, backW, 38}, "Back", ButtonKind::Quiet))
        screen_ = Screen::Home;
}

// The Graphics section's options: the original's GraphicalOptions that change what the port shows, with the choices the
// options files' comments list. Texture quality's Min and Low look alike here (both half-size textures), as do Medium and
// High, so it offers Low and High; any visitor shadow but Off is High in the game (EngineStartup_SetOptions), so that one
// offers Off and On. Visitor looks is the files' GameOptions NUMKIDS, which the detail level sets like the others and
// which only picks how many of the visitors' sprites load: its choices are the files' 0 to 3, shown as the looks the
// game loads for each (COptions::GetNumKidSprites: 2, 4, 6 or all 8 of them; at 0 the mechanics get one look of their
// two), not as the files' comment's 4, 6, 8 and Max. From tpw-port PR #31 by samueleagle91.
const std::vector<SamOption>& SamOptions() {
    static const std::vector<SamOption> kOptions = {
        {"sam_view_distance", 17, "View distance", "How far first person sees", {"Short", "Med", "Long", "Max"},
         {0, 1, 2, 3}},
        {"sam_sky_quality", 6, "Sky layers", "Layers of cloud in the sky", {"1", "2", "4"}, {1, 2, 4}},
        {"sam_sky_shadow", 8, "Cloud shadows", "Clouds' shadows across the ground", {"Off", "On"}, {0, 1}},
        {"sam_procedural_texturing", 3, "Ground blending", "Detail in the ground", {"Off", "16", "32", "64"},
         {0, 1, 2, 3}},
        {"sam_texture_quality", 1, "Texture quality", "Low draws the textures at half size", {"Low", "High"}, {1, 3}},
        {"sam_lighting", 18, "Lighting", "Light and shade across the park", {"Off", "On"}, {0, 1}},
        {"sam_sprite_shadow", 10, "Visitor shadows", "The shadows the visitors cast", {"Off", "On"}, {0, 3}},
        {"sam_num_kids", 24, "Visitor looks", "How many kinds of visitor the park shows", {"2", "4", "6", "8"},
         {0, 1, 2, 3}},
    };
    return kOptions;
}

int SamChoice(const SamOption& option, const std::string& value) {
    for (size_t i = 0; i < option.values.size(); ++i)
        if (value == std::to_string(option.values[i])) return static_cast<int>(i);
    return -1;
}

int FrameRateSetting(const std::string& value) {
    if (value.empty() || value.size() > 4 || !std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return 0;
    const int rate = std::stoi(value);
    return rate >= kFrameRateLowest ? rate : 0;
}

std::string LanguageShownName(const std::string& name) {
    static const std::pair<const char*, const char*> kShown[] = {
        {"English", "English"}, {"American", "American English"}, {"French", "Fran\xC3\xA7" "ais"},
        {"German", "Deutsch"}, {"Danish", "Dansk"}, {"Swedish", "Svenska"}, {"Spanish", "Espa\xC3\xB1" "ol"},
        {"Italian", "Italiano"}, {"Dutch", "Nederlands"}, {"Polish", "Polski"},
        // The fonts hold Latin-1 and a little punctuation (text.cpp), no CJK or Hebrew: these five keep their English
        // names.
        {"Japanese", "Japanese"}, {"Korean", "Korean"}, {"TraditionalChinese", "Traditional Chinese"},
        {"SimplifiedChinese", "Simplified Chinese"}, {"Hebrew", "Hebrew"},
    };
    for (const auto& shown : kShown)
        if (name == shown.first) return shown.second;
    return name;
}

std::vector<LanguageChoice> LanguageChoices(Edition edition, const std::vector<assets::LanguageStatus>& languages) {
    const std::string own = OwnLanguage(edition);
    std::vector<LanguageChoice> out = {{own, LanguageShownName(own)}};
    for (const assets::LanguageStatus& l : languages) {
        if (l.name == own || l.state == assets::LanguageStatus::State::Absent) continue;
        // Its text, swear filter and meshes all there, and the base edition's date fonts when it takes those; its speech
        // may be short, the edition's own recordings then take the missing ones' places (platform/language_pack.h).
        bool usable = l.textPresent == l.text && l.filterPresent == l.filter && l.meshesPresent == l.meshes &&
                      (!l.baseDateFonts || l.baseDateFontsPresent);
        if (!usable) continue;
        std::string shown = LanguageShownName(l.name);
        if (l.speechPresent == 0 && l.speech > 0) shown += " (speech in English)";
        else if (l.speechPresent < l.speech) shown += " (some speech in English)";
        out.push_back({l.name, shown});
    }
    return out;
}

int LanguageChoiceIndex(const std::vector<LanguageChoice>& choices, const std::string& value) {
    for (size_t i = 0; i < choices.size(); ++i)
        if (choices[i].name == value) return static_cast<int>(i);
    return 0;
}

void LauncherUi::DrawSettings(const Rect& w) {
    canvas_.FillRound(w, 0, colour::kDim);
    const float rowH = 50;
    float pw = std::min(w.w - 2 * metric::kMargin, 640.0f);
    float titleH = text_.LineHeight(style::kHeadline) + 12;
    // Everything below the title, as drawn below: the Game section's 8 rows, the Renderer row (counted last) when shown.
    // A row added below is counted here too; the Settings scroll test checks the sum against what is drawn.
    float bodyH = 2 * (text_.LineHeight(style::kBodyBold) + 4) + 40 + text_.LineHeight(style::kSmall) + 14 + 3 * rowH +
                  4 + 2 * text_.LineHeight(style::kSmallBold) + text_.LineHeight(style::kBodyBold) + 4 + 8 * rowH +
                  text_.LineHeight(style::kBodyBold) + 4 + 2 * rowH +
                  text_.LineHeight(style::kBodyBold) + 4 + static_cast<float>(SamOptions().size() + 1) * rowH +
                  text_.LineHeight(style::kBodyBold) + 4 + 2 * rowH + (rendererRow_ ? rowH : 0.0f) +
                  (gameShown_ ? GameOptionsHeight(rowH) + text_.LineHeight(style::kSmall) + 6 : 0.0f);
    float ph = std::min(w.h - 2 * metric::kMargin, titleH + bodyH + 40);
    Rect panel{w.CentreX() - pw * 0.5f, w.CentreY() - ph * 0.5f, pw, ph};
    // An open Language list (drawn last, over the box) takes the pointer: the box below it draws but does not react,
    // and a press outside the list closes it. One opened in this frame shows from the next.
    const int listOpen = languageOpen_;
    const bool playerListOpen = gamePlayerOpen_;
    const bool interactive = ui_.Interactive();
    if (listOpen >= 0 || playerListOpen) ui_.SetInteractive(false);
    ui_.Panel(panel, colour::kPaper, colour::kCream);
    Rect in = panel.Inset(20);
    float y = in.y;
    text_.Draw(style::kHeadline, in.x, y, "Settings", colour::kInk);
    float cw = ui_.ButtonWidth("Close");
    if (ui_.Button("settings-close", {in.Right() - cw, y, cw, 36}, "Close", ButtonKind::Quiet)) {
        settingsOpen_ = false;
        armedRemove_ = -1;
    }
    y += titleH;

    // A window too short for the rest scrolls it under the title: the wheel anywhere over the box moves it a row a
    // line, and a bar in the right margin shows where it is. It is clipped below the Close button and at the box's
    // outline, so the row below the view shows its top. Whether it scrolls is whether the box was capped, an exact
    // comparison: `bodyH - shown` can come out a hair above 0 in a box that fits.
    const float bodyTop = y, shown = in.Bottom() - bodyTop;
    settingsScrolls_ = ph < titleH + bodyH + 40;
    const float maxOffset = settingsScrolls_ ? std::max(0.0f, bodyH - shown) : 0.0f;
    float offset = 0;
    if (settingsScrolls_) {
        offset = ui_.Scroll("settings", panel, rowH, maxOffset);
        ui_.Clip({panel.x + metric::kOutline, bodyTop, panel.w - 2 * metric::kOutline,
                  panel.Bottom() - metric::kOutline - bodyTop});
        y -= offset;
    }
    bool busy = verify_.valid() || backend_.Running();

    text_.Draw(style::kBodyBold, in.x, y, "Data folder", colour::kInk);
    y += text_.LineHeight(style::kBodyBold) + 4;
    float chw = ui_.ButtonWidth("Change...");
    Rect box{in.x, y, in.w - chw - 12, 36};
    canvas_.Outlined(box, 10, 2.5f, colour::kPaper, colour::kInk);
    text_.Draw(style::kSmall, box.x + 12, box.y + (box.h - text_.LineHeight(style::kSmall)) * 0.5f,
               text_.Fit(style::kSmall, backend_.DataFolder(), box.w - 24, true), colour::kInk);
    if (ui_.Button("change-folder", {box.Right() + 12, y, chw, 36}, "Change...", ButtonKind::Plain, !busy))
        Browse(true, DialogPurpose::DataFolder);
    y += 36 + 4;
    text_.Draw(style::kSmall, in.x, y,
               text_.Fit(style::kSmall, "The checked files are kept here. Files in an old folder stay where they are.",
                         in.w, false),
               colour::kInkSoft);
    y += text_.LineHeight(style::kSmall) + 14;

    text_.Draw(style::kBodyBold, in.x, y, "Editions", colour::kInk);
    y += text_.LineHeight(style::kBodyBold) + 4;
    for (int i = 0; i < 3; ++i) {
        Rect row{in.x, y, in.w, rowH - 6};
        canvas_.FillRound(row, 10, i % 2 ? colour::kCream : colour::kYellowSoft.WithAlpha(120));
        const EditionStatus& s = status_[i];
        // A tile's Mac and Windows editions are removed and checked again together.
        const Edition both[2] = {kEditions[i], WindowsEdition(i)};
        const bool present = s.present > 0 || all_[StatusIndex(both[1])].present > 0;
        float ty = row.y + (row.h - text_.LineHeight(style::kBodyBold) - text_.LineHeight(style::kSmall)) * 0.5f;
        text_.Draw(style::kBodyBold, row.x + 12, ty, EditionTitle(kEditions[i]), colour::kInk);
        text_.Draw(style::kSmall, row.x + 12, ty + text_.LineHeight(style::kBodyBold), DescribeTile(s).state,
                   colour::kInkSoft);
        std::string id = assets::EditionName(kEditions[i]);
        float bh = 32, by = row.CentreY() - bh * 0.5f;
        float x = row.Right() - 8;
        if (armedRemove_ == i) {
            float kw = ui_.ButtonWidth("Keep"), yw = ui_.ButtonWidth("Yes, remove");
            x -= kw;
            if (ui_.Button("keep-" + id, {x, by, kw, bh}, "Keep", ButtonKind::Quiet)) armedRemove_ = -1;
            x -= yw + 8;
            if (ui_.Button("confirm-remove-" + id, {x, by, yw, bh}, "Yes, remove", ButtonKind::Danger, !busy)) {
                std::string err;
                bool removed = backend_.RemoveEdition(both[0], err);
                if (removed && both[1] != both[0]) removed = backend_.RemoveEdition(both[1], err);
                if (removed)
                    settingsMessage_ = std::string("The ") + EditionTitle(kEditions[i]) + " was removed.";
                else
                    settingsMessage_ = "Cannot remove it: " + err;
                armedRemove_ = -1;
                RefreshStatus();
                RefreshLanguages();
            }
            const char* ask = "Remove its files?";
            text_.Draw(style::kSmallBold, x - 12 - text_.Width(style::kSmallBold, ask),
                       row.CentreY() - text_.LineHeight(style::kSmallBold) * 0.5f, ask, colour::kRed);
        } else {
            float rw = ui_.ButtonWidth("Remove"), vw = ui_.ButtonWidth("Check again");
            x -= rw;
            if (ui_.Button("remove-" + id, {x, by, rw, bh}, "Remove", ButtonKind::Danger, !busy && present))
                armedRemove_ = i;
            x -= vw + 8;
            if (ui_.Button("verify-" + id, {x, by, vw, bh}, "Check again", ButtonKind::Plain, !busy && present)) {
                verifyIndex_ = i;
                settingsMessage_.clear();
                ImportBackend* backend = &backend_;
                const Edition mac = both[0], windows = both[1];
                verify_ = std::async(std::launch::async, [backend, mac, windows] {
                    VerifyResult r;
                    r.removed = backend->VerifyEdition(mac, r.err);
                    if (r.removed >= 0 && windows != mac) {
                        const long more = backend->VerifyEdition(windows, r.err);
                        r.removed = more < 0 ? more : r.removed + more;
                    }
                    return r;
                });
            }
        }
        y += rowH;
    }

    text_.Draw(style::kBodyBold, in.x, y, "Game", colour::kInk);
    y += text_.LineHeight(style::kBodyBold) + 4;
    {
        Rect row{in.x, y, in.w, rowH - 6};
        canvas_.FillRound(row, 10, colour::kYellowSoft.WithAlpha(120));
        float ty = row.y + (row.h - text_.LineHeight(style::kBodyBold) - text_.LineHeight(style::kSmall)) * 0.5f;
        text_.Draw(style::kBodyBold, row.x + 12, ty, "Skip the intro films", colour::kInk);
        text_.Draw(style::kSmall, row.x + 12, ty + text_.LineHeight(style::kBodyBold),
                   text_.Fit(style::kSmall, "Play goes straight to the lobby, without the logos and the opening film",
                             row.w - 12 - 56 - 24, false),
                   colour::kInkSoft);
        if (ui_.Toggle("skip-intro", {row.Right() - 12 - 56, row.CentreY() - 16, 56, 32}, skipIntro_)) {
            std::string err;
            if (!backend_.SaveSetting(kSkipIntroKey, skipIntro_ ? "1" : "0", err))
                settingsMessage_ = "Cannot keep the setting: " + err;
        }
        y += rowH;
    }
    {
        Rect row{in.x, y, in.w, rowH - 6};
        canvas_.FillRound(row, 10, colour::kYellowSoft.WithAlpha(120));
        float ty = row.y + (row.h - text_.LineHeight(style::kBodyBold) - text_.LineHeight(style::kSmall)) * 0.5f;
        text_.Draw(style::kBodyBold, row.x + 12, ty, "Back to the launcher after playing", colour::kInk);
        text_.Draw(style::kSmall, row.x + 12, ty + text_.LineHeight(style::kBodyBold),
                   text_.Fit(style::kSmall, "When the game quits, this window opens again", row.w - 12 - 56 - 24, false),
                   colour::kInkSoft);
        if (ui_.Toggle("back-to-launcher", {row.Right() - 12 - 56, row.CentreY() - 16, 56, 32}, backToLauncher_)) {
            std::string err;
            if (!backend_.SaveSetting(kBackToLauncherKey, backToLauncher_ ? "1" : "0", err))
                settingsMessage_ = "Cannot keep the setting: " + err;
        }
        y += rowH;
    }
    {
        Rect row{in.x, y, in.w, rowH - 6};
        canvas_.FillRound(row, 10, colour::kYellowSoft.WithAlpha(120));
        float ty = row.y + (row.h - text_.LineHeight(style::kBodyBold) - text_.LineHeight(style::kSmall)) * 0.5f;
        text_.Draw(style::kBodyBold, row.x + 12, ty, "Full screen", colour::kInk);
#if defined(__APPLE__)
        const char* fullScreenLine = fullScreen_ ? "On, the game fills the screen. Cmd+Ctrl+F switches while playing"
                                                : "Off, the game plays in a window. Cmd+Ctrl+F switches while playing";
#else
        const char* fullScreenLine = fullScreen_ ? "On, the game fills the screen. Alt+Enter switches while playing"
                                                : "Off, the game plays in a window. Alt+Enter switches while playing";
#endif
        text_.Draw(style::kSmall, row.x + 12, ty + text_.LineHeight(style::kBodyBold),
                   text_.Fit(style::kSmall, fullScreenLine, row.w - 12 - 56 - 24, false), colour::kInkSoft);
        if (ui_.Toggle("full-screen", {row.Right() - 12 - 56, row.CentreY() - 16, 56, 32}, fullScreen_)) {
            std::string err;
            if (!backend_.SaveSetting(kFullScreenKey, fullScreen_ ? "1" : "0", err))
                settingsMessage_ = "Cannot keep the setting: " + err;
        }
        y += rowH;
    }
    rendererRect_ = {};
    if (rendererRow_) {   // Linux: the system's OpenGL (the default) or the port's own Vulkan renderer (platform/gfx.h)
        Rect row{in.x, y, in.w, rowH - 6};
        canvas_.FillRound(row, 10, colour::kYellowSoft.WithAlpha(120));
        const float segW = 210;
        float ty = row.y + (row.h - text_.LineHeight(style::kBodyBold) - text_.LineHeight(style::kSmall)) * 0.5f;
        text_.Draw(style::kBodyBold, row.x + 12, ty, "Renderer", colour::kInk);
        const char* rendererLine = renderer_ == 0 ? "The system's OpenGL draws the game"
                                                  : "The port's own Vulkan renderer draws the game";
        text_.Draw(style::kSmall, row.x + 12, ty + text_.LineHeight(style::kBodyBold),
                   text_.Fit(style::kSmall, rendererLine, row.w - 12 - segW - 24, false), colour::kInkSoft);
        static const std::vector<std::string> kRenderers = {"OpenGL", "Vulkan"};
        static const char* const kValues[] = {"opengl", "vulkan"};
        rendererRect_ = {row.Right() - 12 - segW, row.CentreY() - 18, segW, 36};
        if (ui_.Segmented("renderer", rendererRect_, kRenderers, std::vector<bool>(kRenderers.size(), true), renderer_)) {
            std::string err;
            if (!backend_.SaveSetting(kRendererKey, kValues[renderer_], err))
                settingsMessage_ = "Cannot keep the setting: " + err;
        }
        y += rowH;
    }
    {
        Rect row{in.x, y, in.w, rowH - 6};
        canvas_.FillRound(row, 10, colour::kYellowSoft.WithAlpha(120));
        float ty = row.y + (row.h - text_.LineHeight(style::kBodyBold) - text_.LineHeight(style::kSmall)) * 0.5f;
        text_.Draw(style::kBodyBold, row.x + 12, ty, "Smooth wheel zoom", colour::kInk);
        text_.Draw(style::kSmall, row.x + 12, ty + text_.LineHeight(style::kBodyBold),
                   text_.Fit(style::kSmall, "The mouse wheel eases the zoom toward the pointer", row.w - 12 - 56 - 24, false),
                   colour::kInkSoft);
        if (ui_.Toggle("smooth-zoom", {row.Right() - 12 - 56, row.CentreY() - 16, 56, 32}, smoothZoom_)) {
            std::string err;
            if (!backend_.SaveSetting(kSmoothZoomKey, smoothZoom_ ? "1" : "0", err))
                settingsMessage_ = "Cannot keep the setting: " + err;
        }
        y += rowH;
    }
    {
        Rect row{in.x, y, in.w, rowH - 6};
        canvas_.FillRound(row, 10, colour::kYellowSoft.WithAlpha(120));
        float ty = row.y + (row.h - text_.LineHeight(style::kBodyBold) - text_.LineHeight(style::kSmall)) * 0.5f;
        text_.Draw(style::kBodyBold, row.x + 12, ty, "Several keys at once", colour::kInk);
        text_.Draw(style::kSmall, row.x + 12, ty + text_.LineHeight(style::kBodyBold),
                   text_.Fit(style::kSmall, "Hold two arrow keys to walk and turn at once, as on the PC", row.w - 12 - 56 - 24,
                             false),
                   colour::kInkSoft);
        severalKeysRect_ = {row.Right() - 12 - 56, row.CentreY() - 16, 56, 32};
        if (ui_.Toggle("several-keys", severalKeysRect_, severalKeys_)) {
            std::string err;
            if (!backend_.SaveSetting(kSeveralKeysKey, severalKeys_ ? "1" : "0", err))
                settingsMessage_ = "Cannot keep the setting: " + err;
        }
        y += rowH;
    }
    {
        Rect row{in.x, y, in.w, rowH - 6};
        canvas_.FillRound(row, 10, colour::kYellowSoft.WithAlpha(120));
        float ty = row.y + (row.h - text_.LineHeight(style::kBodyBold) - text_.LineHeight(style::kSmall)) * 0.5f;
        text_.Draw(style::kBodyBold, row.x + 12, ty, "Varied sounds", colour::kInk);
        text_.Draw(style::kSmall, row.x + 12, ty + text_.LineHeight(style::kBodyBold),
                   text_.Fit(style::kSmall, "Sounds change between their recordings, as on the PC", row.w - 12 - 56 - 24,
                             false),
                   colour::kInkSoft);
        if (ui_.Toggle("pc-sound-picks", {row.Right() - 12 - 56, row.CentreY() - 16, 56, 32}, pcSoundPicks_)) {
            std::string err;
            if (!backend_.SaveSetting(kPcSoundPicksKey, pcSoundPicks_ ? "1" : "0", err))
                settingsMessage_ = "Cannot keep the setting: " + err;
        }
        y += rowH;
    }
    {
        Rect row{in.x, y, in.w, rowH - 6};
        canvas_.FillRound(row, 10, colour::kYellowSoft.WithAlpha(120));
        float ty = row.y + (row.h - text_.LineHeight(style::kBodyBold) - text_.LineHeight(style::kSmall)) * 0.5f;
        text_.Draw(style::kBodyBold, row.x + 12, ty, "Stereo panning", colour::kInk);
        text_.Draw(style::kSmall, row.x + 12, ty + text_.LineHeight(style::kBodyBold),
                   text_.Fit(style::kSmall, "Park sounds are placed left and right with the camera", row.w - 12 - 56 - 24,
                             false),
                   colour::kInkSoft);
        soundPanRect_ = {row.Right() - 12 - 56, row.CentreY() - 16, 56, 32};
        if (ui_.Toggle("sound-pan", soundPanRect_, soundPan_)) {
            std::string err;
            if (!backend_.SaveSetting(kSoundPanKey, soundPan_ ? "1" : "0", err))
                settingsMessage_ = "Cannot keep the setting: " + err;
        }
        y += rowH;
    }
    {
        Rect row{in.x, y, in.w, rowH - 6};
        canvas_.FillRound(row, 10, colour::kYellowSoft.WithAlpha(120));
        const float segW = 210;
        float ty = row.y + (row.h - text_.LineHeight(style::kBodyBold) - text_.LineHeight(style::kSmall)) * 0.5f;
        text_.Draw(style::kBodyBold, row.x + 12, ty, "Interface size", colour::kInk);
        text_.Draw(style::kSmall, row.x + 12, ty + text_.LineHeight(style::kBodyBold),
                   text_.Fit(style::kSmall, "Smaller panels show more of the park", row.w - 12 - segW - 24, false),
                   colour::kInkSoft);
        static const std::vector<std::string> kSizes = {"100%", "85%", "70%"};
        static const char* const kValues[] = {"100", "85", "70"};
        interfaceSizeRect_ = {row.Right() - 12 - segW, row.CentreY() - 18, segW, 36};
        if (ui_.Segmented("interface-size", interfaceSizeRect_, kSizes, std::vector<bool>(kSizes.size(), true),
                          interfaceSize_)) {
            std::string err;
            if (!backend_.SaveSetting(kInterfaceSizeKey, kValues[interfaceSize_], err))
                settingsMessage_ = "Cannot keep the setting: " + err;
        }
        y += rowH;
    }

    // The game's language for each edition (LanguageKey): its own, or one of the data folder's (LanguageChoices).
    text_.Draw(style::kBodyBold, in.x, y, "Language", colour::kInk);
    y += text_.LineHeight(style::kBodyBold) + 4;
    for (int i = 0; i < 2; ++i) {
        Rect row{in.x, y, in.w, rowH - 6};
        canvas_.FillRound(row, 10, i % 2 ? colour::kCream : colour::kYellowSoft.WithAlpha(120));
        const std::vector<LanguageChoice> choices = LanguageChoicesFor(i);
        int choice = LanguageChoiceIndex(choices, language_[i]);
        const float fieldW = 260;
        std::string line = i == 0 ? "The US edition's text and the advisor's voice"
                                  : "The European edition's text and the advisor's voice";
        if (choice == 0 && !language_[i].empty() && language_[i] != choices[0].name)
            line = LanguageShownName(language_[i]) + " is gone from the data folder";
        else if (choices.size() == 1)
            line = "Drag in a disc in another language to choose it here";
        float ty = row.y + (row.h - text_.LineHeight(style::kBodyBold) - text_.LineHeight(style::kSmall)) * 0.5f;
        text_.Draw(style::kBodyBold, row.x + 12, ty, i == 0 ? "Sim Theme Park" : "Theme Park World", colour::kInk);
        text_.Draw(style::kSmall, row.x + 12, ty + text_.LineHeight(style::kBodyBold),
                   text_.Fit(style::kSmall, line, row.w - 12 - fieldW - 24, false), colour::kInkSoft);
        languageRects_[i] = {row.Right() - 12 - fieldW, row.CentreY() - 18, fieldW, 36};
        if (ui_.Dropdown(std::string("language-") + assets::EditionName(kEditions[i]), languageRects_[i],
                         choices[static_cast<size_t>(choice)].shown, listOpen == i))
            languageOpen_ = i;
        y += rowH;
    }

    if (gameShown_) DrawGameOptions(in, y, rowH);

    // The original's options files' graphics options (SamOptions), each Default (the detail level's own) or a choice.
    text_.Draw(style::kBodyBold, in.x, y, "Graphics", colour::kInk);
    y += text_.LineHeight(style::kBodyBold) + 4;
    if (gameShown_) {
        text_.Draw(style::kSmall, in.x, y,
                   text_.Fit(style::kSmall, "Default keeps what each edition's Graphics quality gives; a choice here replaces it",
                             in.w, false),
                   colour::kInkSoft);
        y += text_.LineHeight(style::kSmall) + 6;
    }
    for (size_t i = 0; i < SamOptions().size(); ++i) {
        const SamOption& option = SamOptions()[i];
        Rect row{in.x, y, in.w, rowH - 6};
        canvas_.FillRound(row, 10, i % 2 ? colour::kCream : colour::kYellowSoft.WithAlpha(120));
        std::vector<std::string> labels = {"Default"};
        labels.insert(labels.end(), option.shown.begin(), option.shown.end());
        const float segW = 80.0f * static_cast<float>(labels.size());
        float ty = row.y + (row.h - text_.LineHeight(style::kBodyBold) - text_.LineHeight(style::kSmall)) * 0.5f;
        text_.Draw(style::kBodyBold, row.x + 12, ty, text_.Fit(style::kBodyBold, option.title, row.w - 12 - segW - 24, false),
                   colour::kInk);
        text_.Draw(style::kSmall, row.x + 12, ty + text_.LineHeight(style::kBodyBold),
                   text_.Fit(style::kSmall, option.line, row.w - 12 - segW - 24, false), colour::kInkSoft);
        int& choice = samChoices_[i];
        samRects_[i] = {row.Right() - 12 - segW, row.CentreY() - 18, segW, 36};
        if (ui_.Segmented(option.key, samRects_[i], labels, std::vector<bool>(labels.size(), true), choice)) {
            std::string err;
            const std::string value = choice == 0 ? "default" : std::to_string(option.values[static_cast<size_t>(choice - 1)]);
            if (!backend_.SaveSetting(option.key, value, err)) settingsMessage_ = "Cannot keep the setting: " + err;
        }
        y += rowH;
    }
    {   // the Frame rate limit (kFrameRateKey): kFrameRateLowest up to the monitor's refresh, which is Monitor (no limit)
        Rect row{in.x, y, in.w, rowH - 6};
        canvas_.FillRound(row, 10, SamOptions().size() % 2 ? colour::kCream : colour::kYellowSoft.WithAlpha(120));
        if (frameRateTopStale_) {   // the monitor's refresh rate, read again only when the window or the mode changed
            frameRateTopStale_ = false;
            frameRateHighest_ = 60;
            const SDL_DisplayID display = window_ != nullptr ? SDL_GetDisplayForWindow(window_) : 0;
            const SDL_DisplayMode* mode = display != 0 ? SDL_GetCurrentDisplayMode(display) : nullptr;
            if (mode != nullptr && mode->refresh_rate > 0)
                frameRateHighest_ = std::max(kFrameRateLowest, static_cast<int>(std::lround(mode->refresh_rate)));
        }
        const float sliderW = 220, valueW = 84;
        float ty = row.y + (row.h - text_.LineHeight(style::kBodyBold) - text_.LineHeight(style::kSmall)) * 0.5f;
        text_.Draw(style::kBodyBold, row.x + 12, ty, "Frame rate limit", colour::kInk);
        const char* frameRateLine = frameRate_ == 0 ? "A frame for every refresh of the monitor"
                                                    : "The most frames a second the game draws";
        text_.Draw(style::kSmall, row.x + 12, ty + text_.LineHeight(style::kBodyBold),
                   text_.Fit(style::kSmall, frameRateLine, row.w - 12 - sliderW - valueW - 24, false), colour::kInkSoft);
        // The slider's right end, the monitor's refresh rate, is Monitor (0).
        const int before = frameRate_ == 0 ? frameRateHighest_ : std::min(frameRate_, frameRateHighest_);
        int position = before;
        frameRateRect_ = {row.Right() - 12 - sliderW, row.CentreY() - 18, sliderW, 36};
        const bool kept = ui_.Slider("frame-rate", frameRateRect_, position, kFrameRateLowest, frameRateHighest_);
        if (position != before) frameRate_ = position >= frameRateHighest_ ? 0 : position;
        const std::string shown = frameRate_ == 0 ? "Monitor" : std::to_string(frameRate_) + " fps";
        text_.Draw(style::kButton, frameRateRect_.x - 12 - text_.Width(style::kButton, shown),
                   row.CentreY() - text_.LineHeight(style::kButton) * 0.5f, shown, colour::kInk);
        if (kept) {
            std::string err;
            if (!backend_.SaveSetting(kFrameRateKey, std::to_string(frameRate_), err))
                settingsMessage_ = "Cannot keep the setting: " + err;
        }
        y += rowH;
    }

    text_.Draw(style::kBodyBold, in.x, y, "Launcher", colour::kInk);
    y += text_.LineHeight(style::kBodyBold) + 4;
    {   // on by default, unlike the others: updater.h's kCheckUpdatesKey says why
        Rect row{in.x, y, in.w, rowH - 6};
        canvas_.FillRound(row, 10, colour::kYellowSoft.WithAlpha(120));
        float ty = row.y + (row.h - text_.LineHeight(style::kBodyBold) - text_.LineHeight(style::kSmall)) * 0.5f;
        text_.Draw(style::kBodyBold, row.x + 12, ty, "Check for updates", colour::kInk);
        text_.Draw(style::kSmall, row.x + 12, ty + text_.LineHeight(style::kBodyBold),
                   text_.Fit(style::kSmall, "At each start, asks GitHub for a new version and the online server's address",
                             row.w - 12 - 56 - 24, false),
                   colour::kInkSoft);
        checkUpdatesRect_ = {row.Right() - 12 - 56, row.CentreY() - 16, 56, 32};
        if (ui_.Toggle("check-updates", checkUpdatesRect_, checkUpdates_)) {
            std::string err;
            if (!backend_.SaveSetting(kCheckUpdatesKey, checkUpdates_ ? "1" : "0", err))
                settingsMessage_ = "Cannot keep the setting: " + err;
            if (updater_ != nullptr) updater_->SetEnabled(checkUpdates_);
        }
        y += rowH;
    }
    {   // empty by default: the automatic server, the one tpw-port's online.txt names (updater.h ChooseOnlineServer)
        Rect row{in.x, y, in.w, rowH - 6};
        canvas_.FillRound(row, 10, colour::kCream);
        const float fieldW = std::min(260.0f, row.w * 0.45f);
        onlineServerRect_ = {row.Right() - 6 - fieldW, row.y + 4, fieldW, row.h - 8};
        float ty = row.y + (row.h - text_.LineHeight(style::kBodyBold) - text_.LineHeight(style::kSmall)) * 0.5f;
        text_.Draw(style::kBodyBold, row.x + 12, ty, "Online server", colour::kInk);
        std::string why;
        const bool bad = !onlineServer_.empty() && !net::ValidHost(onlineServer_, &why);
        const std::string line = bad ? "Not used: " + why : "Empty for automatic, or a server's address";
        text_.Draw(style::kSmall, row.x + 12, ty + text_.LineHeight(style::kBodyBold),
                   text_.Fit(style::kSmall, line, onlineServerRect_.x - row.x - 12 - 12, false),
                   bad ? colour::kRed : colour::kInkSoft);
        onlineServerHint_ = AutomaticOnlineServer(backend_);
        if (ui_.TextField("online-server", onlineServerRect_, onlineServer_, kOnlineServerMax,
                          "Automatic: " + onlineServerHint_, onlineServerEditing_)) {
            std::string err;
            if (!backend_.SaveSetting(kOnlineServerKey, onlineServer_, err))
                settingsMessage_ = "Cannot keep the setting: " + err;
        }
        y += rowH;
    }
    y += 4;
    // The body as drawn, with the two lines bodyH keeps for the message below (SettingsBodyDrawn).
    settingsBodyCounted_ = bodyH;
    settingsBodyDrawn_ = y + offset - bodyTop + 2 * text_.LineHeight(style::kSmallBold);
    if (verify_.valid() && verifyIndex_ >= 0) {
        std::string msg = std::string("Checking every file of the ") + EditionTitle(kEditions[verifyIndex_]) + " again";
        text_.Draw(style::kSmallBold, in.x, y, msg, colour::kInk);
        ui_.ProgressBar({in.x + text_.Width(style::kSmallBold, msg) + 14, y, 160, 18}, -1);
    } else if (!settingsMessage_.empty()) {
        text_.DrawWrapped(style::kSmallBold, in.x, y, in.w, settingsMessage_, colour::kInk);
    }
    if (settingsScrolls_) {
        ui_.Unclip();
        const float barW = 6, margin = panel.Right() - metric::kOutline - in.Right();
        ui_.ScrollBar({in.Right() + std::round((margin - barW) * 0.5f), bodyTop, barW, shown}, shown, bodyH, offset,
                      colour::kYellowSoft);
    }

    if (listOpen >= 0 && languageOpen_ == listOpen) {
        ui_.SetInteractive(interactive);
        const std::vector<LanguageChoice> choices = LanguageChoicesFor(listOpen);
        std::vector<std::string> shownNames;
        for (const LanguageChoice& c : choices) shownNames.push_back(c.shown);
        int choice = LanguageChoiceIndex(choices, language_[listOpen]);
        bool close = false;
        if (ui_.DropList(std::string("language-list-") + assets::EditionName(kEditions[listOpen]), languageRects_[listOpen],
                         w.Inset(8), shownNames, choice, close, &languageRows_)) {
            // The own language is kept by its name too: the key says what the player chose.
            language_[listOpen] = choices[static_cast<size_t>(choice)].name;
            std::string err;
            if (!backend_.SaveSetting(LanguageKey(kEditions[listOpen]), language_[listOpen], err))
                settingsMessage_ = "Cannot keep the setting: " + err;
        }
        if (close) languageOpen_ = -1;
    } else {
        languageRows_.clear();
    }
    if (gameShown_ && playerListOpen && gamePlayerOpen_) {
        ui_.SetInteractive(interactive);
        DrawGamePlayerList(w);
    } else {
        gamePlayerRows_.clear();
    }
}

// The editions of a mod's row, as the player reads them: "For the US edition and the European edition", and those it
// is not for.
static std::string ModEditionsLine(const ModRow& mod) {
    std::vector<std::string> on, off;
    for (int i = 0; i < 3; ++i) (mod.editions[i] ? on : off).push_back(std::string("the ") + EditionTitle(kEditions[i]));
    auto join = [](const std::vector<std::string>& names, const char* last) {
        std::string s;
        for (size_t i = 0; i < names.size(); ++i) s += (i == 0 ? "" : i + 1 == names.size() ? last : ", ") + names[i];
        return s;
    };
    std::string line = "For " + join(on, " and ");
    if (!off.empty()) line += ". Not for " + join(off, " or ");
    return line;
}

void LauncherUi::DrawMods(const Rect& w) {
    canvas_.FillRound(w, 0, colour::kDim);
    float pw = std::min(w.w - 2 * metric::kMargin, 640.0f);
    float titleH = text_.LineHeight(style::kHeadline) + 12;
    // Each mod's row: its name and version, the player's text, the editions, saved games, conflicts and why its switch
    // is off, beside the switch.
    const float textW = pw - 40 - 12 - 56 - 36;
    const float small = text_.LineHeight(style::kSmall), smallBold = text_.LineHeight(style::kSmallBold);
    std::vector<std::vector<std::string>> texts;
    std::vector<std::string> whys;
    std::vector<float> rowHs;
    for (size_t i = 0; i < mods_.size(); ++i) {
        texts.push_back(text_.Wrap(style::kSmall, mods_[i].text, textW));
        std::string why;
        if (modOn_[i] || ModAllowed(i, why)) why.clear();
        whys.push_back(why);
        rowHs.push_back(24 + text_.LineHeight(style::kBodyBold) + 4 + static_cast<float>(texts.back().size()) * small + 4 +
                        3 * small + (why.empty() ? 0.0f : smallBold));
    }
    float bodyH = 0;
    for (float h : rowHs) bodyH += h + 6;
    if (mods_.empty()) bodyH += 50;
    bodyH += 4 + small + (modsMessage_.empty() ? 0.0f : 4 + smallBold);
    float ph = std::min(w.h - 2 * metric::kMargin, titleH + bodyH + 40);
    Rect panel{w.CentreX() - pw * 0.5f, w.CentreY() - ph * 0.5f, pw, ph};
    modsBoxRect_ = panel;
    ui_.Panel(panel, colour::kPaper, colour::kCream);
    Rect in = panel.Inset(20);
    float y = in.y;
    text_.Draw(style::kHeadline, in.x, y, "Mods", colour::kInk);
    float cw = ui_.ButtonWidth("Close");
    if (ui_.Button("mods-close", {in.Right() - cw, y, cw, 36}, "Close", ButtonKind::Quiet)) modsOpen_ = false;
    y += titleH;

    // Scrolls as Settings does, in a window too short for the list.
    const float bodyTop = y, shown = in.Bottom() - bodyTop;
    modsScrolls_ = ph < titleH + bodyH + 40;
    const float maxOffset = modsScrolls_ ? std::max(0.0f, bodyH - shown) : 0.0f;
    float offset = 0;
    if (modsScrolls_) {
        offset = ui_.Scroll("mods", panel, 50, maxOffset);
        ui_.Clip({panel.x + metric::kOutline, bodyTop, panel.w - 2 * metric::kOutline,
                  panel.Bottom() - metric::kOutline - bodyTop});
        y -= offset;
    }
    const bool busy = backend_.Running();

    modSwitchRects_.assign(mods_.size(), Rect{});
    if (mods_.empty()) {
        Rect row{in.x, y, in.w, 44};
        canvas_.FillRound(row, 10, colour::kYellowSoft.WithAlpha(120));
        text_.Draw(style::kBodyBold, row.x + 12, row.CentreY() - text_.LineHeight(style::kBodyBold) * 0.5f,
                   "This build has no mods.", colour::kInk);
        y += 50;
    }
    for (size_t i = 0; i < mods_.size(); ++i) {
        const ModRow& mod = mods_[i];
        Rect row{in.x, y, in.w, rowHs[i]};
        canvas_.FillRound(row, 10, i % 2 ? colour::kCream : colour::kYellowSoft.WithAlpha(120));
        float ty = row.y + 12;
        const float nameW = text_.Draw(style::kBodyBold, row.x + 12, ty, mod.name, colour::kInk);
        text_.Draw(style::kSmall, row.x + 12 + nameW + 10, ty + text_.Ascent(style::kBodyBold) - text_.Ascent(style::kSmall),
                   text_.Fit(style::kSmall, mod.version + ", by " + mod.author, textW - nameW - 10, false),
                   colour::kInkSoft);
        ty += text_.LineHeight(style::kBodyBold) + 4;
        for (const std::string& line : texts[i]) {
            text_.Draw(style::kSmall, row.x + 12, ty, line, colour::kInk);
            ty += small;
        }
        ty += 4;
        text_.Draw(style::kSmall, row.x + 12, ty, text_.Fit(style::kSmall, ModEditionsLine(mod), textW, false),
                   colour::kInkSoft);
        ty += small;
        if (mod.changesSaves)
            text_.Draw(style::kSmallBold, row.x + 12, ty,
                       text_.Fit(style::kSmallBold, "Saved games: a park saved with it on differs from the original's",
                                 textW, false),
                       colour::kRed);
        else
            text_.Draw(style::kSmall, row.x + 12, ty, "Saved games: unchanged", colour::kInkSoft);
        ty += small;
        std::string conflicts = "Conflicts: none";
        if (!mod.conflicts.empty()) {
            conflicts = "Cannot be on with ";
            for (size_t c = 0; c < mod.conflicts.size(); ++c) {
                std::string name = mod.conflicts[c];
                for (const ModRow& other : mods_)
                    if (other.id == name) name = other.name;
                conflicts += (c == 0 ? "" : ", ") + name;
            }
        }
        text_.Draw(style::kSmall, row.x + 12, ty, text_.Fit(style::kSmall, conflicts, textW, false), colour::kInkSoft);
        ty += small;
        if (!whys[i].empty())
            text_.Draw(style::kSmallBold, row.x + 12, ty, text_.Fit(style::kSmallBold, whys[i], textW, false), colour::kRed);

        // A mod that is on can always be switched off; one that is off, only where it can be on.
        bool on = modOn_[i];
        modSwitchRects_[i] = {row.Right() - 12 - 56, row.CentreY() - 16, 56, 32};
        if (ui_.Toggle("mod-" + mod.id, modSwitchRects_[i], on, !busy && (on || whys[i].empty()))) {
            std::string err;
            if (backend_.SaveSetting(ModKey(mod.id), on ? "1" : "0", err)) {
                modOn_[i] = on;
                modsMessage_.clear();
            } else {
                modsMessage_ = "Cannot keep the setting: " + err;
            }
        }
        y += rowHs[i] + 6;
    }
    y += 4;
    text_.Draw(style::kSmall, in.x, y,
               text_.Fit(style::kSmall, "Mods change how the game plays. Off is the original.", in.w, false),
               colour::kInkSoft);
    y += small;
    if (!modsMessage_.empty()) text_.DrawWrapped(style::kSmallBold, in.x, y + 4, in.w, modsMessage_, colour::kInk);
    if (modsScrolls_) {
        ui_.Unclip();
        const float barW = 6, margin = panel.Right() - metric::kOutline - in.Right();
        ui_.ScrollBar({in.Right() + std::round((margin - barW) * 0.5f), bodyTop, barW, shown}, shown, bodyH, offset,
                      colour::kYellowSoft);
    }
}

void LauncherUi::DrawAbout(const Rect& w) {
    const float kLogsRowH = 32;   // the Show logs row, when there is a logs folder
    canvas_.FillRound(w, 0, colour::kDim);
    float pw = std::min(w.w - 2 * metric::kMargin, 640.0f), inW = pw - 40;
    const std::vector<MadeWith> rows = MadeWithRows(text_);   // the "Made with" table below
    const size_t kRows = rows.size();
    float contentH = text_.LineHeight(style::kHeadline) + 10 +
                     text_.LineHeight(style::kBody) * text_.Wrap(style::kBody, kTagline, inW).size() + 6 +
                     text_.LineHeight(style::kSmallBold) * text_.Wrap(style::kSmallBold, kDisclaimer, inW).size() + 12 +
                     (logsFolder_.empty() ? 0.0f : kLogsRowH + 12) +
                     (logsFolder_.empty() || !exported_.ok ? 0.0f : kLogsRowH + 12) +
                     text_.LineHeight(style::kBodyBold) + 4 + kRows * (text_.LineHeight(style::kSmall) + 6);
    float ph = std::min(w.h - 2 * metric::kMargin, contentH + 44);
    Rect panel{w.CentreX() - pw * 0.5f, w.CentreY() - ph * 0.5f, pw, ph};
    ui_.Panel(panel, colour::kPaper, colour::kCream);
    Rect in = panel.Inset(20);
    float y = in.y;
    // The version in the title, as --version and the log file's first line give it.
    std::string title = std::string("About ") + kProductNameWithVersion;
    text_.Draw(style::kHeadline, in.x, y, title, colour::kInk);
    float cw = ui_.ButtonWidth("Close");
    if (ui_.Button("about-close", {in.Right() - cw, y, cw, 36}, "Close", ButtonKind::Quiet)) aboutOpen_ = false;
    y += text_.LineHeight(style::kHeadline) + 10;
    y += text_.DrawWrapped(style::kBody, in.x, y, in.w, kTagline, colour::kInk) + 6;
    y += text_.DrawWrapped(style::kSmallBold, in.x, y, in.w, kDisclaimer, colour::kInk) + 12;

    // The log file (platform/log_file.h, logs/launcher.log): the button shows it in the Finder or Explorer.
    if (!logsFolder_.empty()) {
        const char* label = "Show logs";
        float lw = ui_.ButtonWidth(label);
        showLogsRect_ = {in.x, y, lw, kLogsRowH};
        // The log file selected in the file manager when it can; else its folder.
        if (ui_.Button("about-logs", showLogsRect_, label, ButtonKind::Quiet) &&
            !(showLogs_ != nullptr && showLogs_(logsFolder_)) && !SDL_OpenURL(FileUrl(logsFolder_).c_str()))
            ShowNotice(std::string("Could not open the logs folder: ") + SDL_GetError(), true);
        float tx = in.x + lw + 12;
        if (exportLogs_ != nullptr) {
            // Export logs: the log file's recent runs as one file in the Downloads folder, to attach to a bug report without
            // a hunt for hidden folders.
            const char* exportLabel = "Export logs";
            float ew = ui_.ButtonWidth(exportLabel);
            exportLogsRect_ = {tx, y, ew, kLogsRowH};
            if (ui_.Button("about-export-logs", exportLogsRect_, exportLabel, ButtonKind::Quiet)) {
                exported_ = exportLogs_(logsFolder_);
                ShowNotice(exported_.message, !exported_.ok);
                if (!exported_.ok) exported_ = LogsExport{};
            }
            tx += ew + 12;
        }
        text_.Draw(style::kSmall, tx, y + (kLogsRowH - text_.LineHeight(style::kSmall)) * 0.5f,
                   text_.Fit(style::kSmall,
                             exportLogs_ != nullptr ? "Attach the exported file to a bug report."
                                                    : "One file holds the logs of the recent runs.",
                             in.Right() - tx, false),
                   colour::kInkSoft);
        y += kLogsRowH + 12;
        if (exported_.ok) {   // where the copy went, and a button to show that folder
            const char* openLabel = "Open Downloads";
            float ow = ui_.ButtonWidth(openLabel);
            openDownloadsRect_ = {in.x, y, ow, kLogsRowH};
            if (ui_.Button("about-open-downloads", openDownloadsRect_, openLabel, ButtonKind::Quiet) &&
                !SDL_OpenURL(FileUrl(exported_.folder).c_str()))
                ShowNotice(std::string("Could not open the Downloads folder: ") + SDL_GetError(), true);
            float sx = in.x + ow + 12;
            text_.Draw(style::kSmallBold, sx, y + (kLogsRowH - text_.LineHeight(style::kSmallBold)) * 0.5f,
                       text_.Fit(style::kSmallBold, exported_.message, in.Right() - sx, false), colour::kInk);
            y += kLogsRowH + 12;
        }
    }

    text_.Draw(style::kBodyBold, in.x, y, "Made with", colour::kInk);
    y += text_.LineHeight(style::kBodyBold) + 4;
    float c1 = in.w * 0.46f, c2 = in.w * 0.16f;
    float lh = text_.LineHeight(style::kSmall) + 6;
    for (size_t i = 0; i < kRows; ++i) {
        if (y + lh > in.Bottom()) break;
        if (i % 2 == 0) canvas_.FillRound({in.x, y, in.w, lh}, 6, colour::kYellowSoft.WithAlpha(140));
        float ty = y + 3;
        text_.Draw(style::kSmallBold, in.x + 10, ty, text_.Fit(style::kSmallBold, rows[i].name, c1 - 16, false),
                   colour::kInk);
        text_.Draw(style::kSmall, in.x + c1, ty, rows[i].version, colour::kInk);
        text_.Draw(style::kSmall, in.x + c1 + c2, ty,
                   text_.Fit(style::kSmall, rows[i].license, in.w - c1 - c2 - 10, false), colour::kInk);
        y += lh;
    }
}

// The Customized settings box (tpw-port #94): the edition's .sam settings files the player changed after its disc was
// checked. The game reads them as they are, any value its parser takes, and Play works as normal; the box says which
// they are, warns of a line the game's parser stops at (assets/sam_check.h), and Restore original puts back the copy the
// launcher kept of the disc's file when it checked it (assets/<edition>/originals in the data folder).
void LauncherUi::DrawEdited(const Rect& w) {
    restoreRects_.clear();
    canvas_.FillRound(w, 0, colour::kDim);
    const int index = editedOpen_;
    const char* about = "These settings files were changed after the disc was checked. The game reads them as they "
                        "are, so Play works as normal. Restore original puts back the file from the disc.";
    const char* restoreLabel = "Restore original";
    const float rowButtonH = 32, rw = ui_.ButtonWidth(restoreLabel);
    float pw = std::min(w.w - 2 * metric::kMargin, 640.0f), inW = pw - 40, textW = inW - rw - 16;
    const float boldH = text_.LineHeight(style::kSmallBold), smallH = text_.LineHeight(style::kSmall);
    // Each row: the file, then what the check found, beside its Restore original button.
    std::vector<std::vector<std::string>> notes;
    std::vector<float> rowH;
    for (const assets::EditedFile& f : editedFiles_) {
        std::string note = f.unreadableLine > 0
                               ? "Line " + std::to_string(f.unreadableLine) +
                                     " can't be read; the game may crash or ignore later settings."
                               : "The game reads all of it.";
        if (!f.original) note += " No copy of the original is kept yet: drag the disc in again to make one.";
        notes.push_back(text_.Wrap(style::kSmall, note, textW));
        rowH.push_back(std::max(rowButtonH, boldH + 2 + smallH * notes.back().size()) + 12);
    }
    std::vector<std::string> aboutLines = text_.Wrap(style::kBody, about, inW);
    float contentH = text_.LineHeight(style::kHeadline) + 10 + text_.LineHeight(style::kBody) * aboutLines.size() + 12;
    for (float h : rowH) contentH += h;
    if (editedFiles_.empty()) contentH += smallH;
    float ph = std::min(w.h - 2 * metric::kMargin, contentH + 44);
    Rect panel{w.CentreX() - pw * 0.5f, w.CentreY() - ph * 0.5f, pw, ph};
    ui_.Panel(panel, colour::kPaper, colour::kCream);
    Rect in = panel.Inset(20);
    float y = in.y;
    float cw = ui_.ButtonWidth("Close");
    const std::string title = std::string(kEditedLabel) + ": " + EditionTitle(kEditions[index]);
    text_.Draw(style::kHeadline, in.x, y, text_.Fit(style::kHeadline, title, in.w - cw - 12, false), colour::kInk);
    if (ui_.Button("edited-close", {in.Right() - cw, y, cw, 36}, "Close", ButtonKind::Quiet)) editedOpen_ = -1;
    y += text_.LineHeight(style::kHeadline) + 10;
    for (const std::string& line : aboutLines) {
        text_.Draw(style::kBody, in.x, y, line, colour::kInk);
        y += text_.LineHeight(style::kBody);
    }
    y += 12;
    if (editedFiles_.empty()) text_.Draw(style::kSmall, in.x, y, "No edited settings files are left.", colour::kInkSoft);

    // A restore changes the list: it is read again after the rows are drawn.
    int restore = -1;
    const bool canRestore = !backend_.Running() && !verify_.valid();
    for (size_t k = 0; k < editedFiles_.size(); ++k) {
        const assets::EditedFile& f = editedFiles_[k];
        if (y + rowH[k] > in.Bottom() + 12) {   // no room for the rest: say how many, the log names them all
            text_.Draw(style::kSmallBold, in.x, y,
                       "And " + std::to_string(editedFiles_.size() - k) + " more: the launcher's log names them all.",
                       colour::kInkSoft);
            break;
        }
        if (k % 2 == 0) canvas_.FillRound({in.x - 8, y - 2, in.w + 16, rowH[k] - 4}, 8, colour::kYellowSoft.WithAlpha(140));
        float ty = y + 4;
        text_.Draw(style::kSmallBold, in.x, ty, text_.Fit(style::kSmallBold, f.dest, textW, false), colour::kInk);
        ty += boldH + 2;
        const Rgba noteColour = f.unreadableLine > 0 ? Mix(colour::kRed, colour::kInk, 0.35f) : colour::kInkSoft;
        for (const std::string& line : notes[k]) {
            text_.Draw(style::kSmall, in.x, ty, line, noteColour);
            ty += smallH;
        }
        Rect button{in.Right() - rw, y + (rowH[k] - 12 - rowButtonH) * 0.5f, rw, rowButtonH};
        restoreRects_.push_back(button);
        if (ui_.Button("edited-restore-" + f.dest, button, restoreLabel, ButtonKind::Plain, f.original && canRestore))
            restore = static_cast<int>(k);
        y += rowH[k];
    }
    if (restore >= 0) {
        const std::string dest = editedFiles_[static_cast<size_t>(restore)].dest;
        const std::string name = dest.substr(dest.find_last_of('/') + 1);
        std::string err;
        if (backend_.RestoreOriginal(kEditions[index], dest, err))
            ShowNotice("Restored the original " + name, false);
        else
            ShowNotice("Could not restore " + name + ": " + err, true);
        editedFiles_ = backend_.EditedFiles(kEditions[index]);
        if (editedFiles_.empty()) editedOpen_ = -1;
    }
}

void LauncherUi::Frame(float dt, float width, float height, float scale) {
    PollDialogs();
    PollVerify();
    PollImport();
    RefreshStatus();
    if (updater_ != nullptr) {
        updater_->Poll();
        const std::string notice = updater_->TakeNotice();
        if (!notice.empty()) ShowNotice(notice, false);
    }
    if (noticeLeft_ > 0) noticeLeft_ = std::max(0.0f, noticeLeft_ - dt);

    canvas_.Begin(scale);
    Rect window{0, 0, width, height};
    ui_.Begin(dt, input_);
    input_.pressed = input_.released = false;
    input_.wheel = 0;
    input_.typed.clear();
    input_.erased = 0;
    input_.entered = false;
    const bool wasEditing = onlineServerEditing_;

    bool overlay = aboutOpen_ || settingsOpen_ || modsOpen_ || editedOpen_ >= 0;
    if (!settingsOpen_) languageOpen_ = -1;
    if (!settingsOpen_) gamePlayerOpen_ = false;
    if (settingsOpen_ && !settingsWereOpen_) {   // opened: the edition chosen to play and its last player, as they are now
        gameEdition_ = -1;
        gamePlayer_.clear();
    }
    if (settingsOpen_ && (!settingsWereOpen_ || gameSettingsStale_)) RefreshGameSettings();
    settingsWereOpen_ = settingsOpen_;
    gameSettingsStale_ = false;
    ui_.SetInteractive(!overlay && !dragging_);
    tilesShown_ = false;
    readyShown_ = false;
    versionRect_ = showLogsRect_ = titleRect_ = titleVersionRect_ = headerButtonsRect_ = updateBannerRect_ = Rect{};
    exportLogsRect_ = openDownloadsRect_ = modsButtonRect_ = modsOnRect_ = Rect{};
    for (Rect& r : editedBadge_) r = Rect{};
    for (Rect& r : tilePlay_) r = Rect{};
    if (editedOpen_ < 0) restoreRects_.clear();
    if (!aboutOpen_) exported_ = LogsExport{};
    DrawBackdrop(window);
    switch (screen_) {
        case Screen::Home: DrawHome(window); break;
        case Screen::Checking: DrawChecking(window); break;
        case Screen::Result: DrawResult(window); break;
        case Screen::Play: DrawPlay(window); break;
        case Screen::Starting: DrawStarting(window); break;
    }
    if (overlay) {
        tilesShown_ = false;   // drops on an open box go to the field
        readyShown_ = false;
        ui_.SetInteractive(!dragging_);
        if (settingsOpen_) DrawSettings(window);
        if (modsOpen_) DrawMods(window);
        if (aboutOpen_) DrawAbout(window);
        if (editedOpen_ >= 0) DrawEdited(window);
    }
    if (!settingsOpen_) onlineServerEditing_ = false;
    if (onlineServerEditing_ != wasEditing && window_ != nullptr) {   // the system sends typed text only while it is on
        if (onlineServerEditing_) SDL_StartTextInput(window_);
        else SDL_StopTextInput(window_);
    }
    if (noticeLeft_ > 0 && !notice_.empty())
        ui_.Notice(window, notice_, noticeProblem_ ? Mix(colour::kRed, colour::kPaper, 0.55f) : colour::kYellowSoft);
    ui_.End();
}

}  // namespace tpw::ui
