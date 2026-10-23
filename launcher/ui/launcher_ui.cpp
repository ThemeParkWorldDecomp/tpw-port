// launcher_ui.cpp: the launcher's screens (launcher_ui.h).
#include "launcher_ui.h"
#include "versions.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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
constexpr float kTileHeight = 158;
constexpr size_t kLogLimit = 4000;     // rows kept in the checking log
constexpr float kNoticeSeconds = 4.5f;
constexpr float kStartupNoticeSeconds = 9.0f;   // the startup check's notice: the window may still be opening

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
    skipIntro_ = backend_.Setting(kSkipIntroKey) == "1";
    backToLauncher_ = backend_.Setting(kBackToLauncherKey) == "1";
    smoothZoom_ = backend_.Setting(kSmoothZoomKey) == "1";
    {
        const std::string size = backend_.Setting(kInterfaceSizeKey);
        interfaceSize_ = size == "85" ? 1 : size == "70" ? 2 : 0;
    }
    {
        const std::string arrivals = backend_.Setting(kVisitorArrivalsKey);
        arrivals_ = arrivals == "more" ? 1 : arrivals == "many" ? 2 : 0;
        const std::string limit = backend_.Setting(kVisitorLimitKey);
        visitorLimit_ = limit == "3000" ? 1 : limit == "5000" ? 2 : 0;
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
        case SDL_EVENT_KEY_DOWN:
            if (e.key.key == SDLK_ESCAPE) {
                if (armedRemove_ >= 0) armedRemove_ = -1;
                else if (aboutOpen_) aboutOpen_ = false;
                else if (settingsOpen_) settingsOpen_ = false;
                else if (rulesOpen_) rulesOpen_ = false;
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
    aboutOpen_ = settingsOpen_ = rulesOpen_ = false;
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
        for (int i = 0; i < 3; ++i) before_[i] = status_[i];
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
    for (int i = 0; i < 3; ++i) round_.status[i] = status_[i];
    result_ = DescribeResult(round_, before_);
    roundStarted_ = false;
    screen_ = Screen::Result;
}

void LauncherUi::RefreshStatus() {
    for (int i = 0; i < 3; ++i) status_[i] = backend_.Status(kEditions[i]);
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

void LauncherUi::PollVerify() {
    if (!verify_.valid()) return;
    if (verify_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    VerifyResult r = verify_.get();
    const char* title = EditionTitle(kEditions[verifyIndex_]);
    if (r.removed < 0)
        settingsMessage_ = std::string("Checking the ") + title + " again failed: " + r.err;
    else if (r.removed == 0)
        settingsMessage_ = std::string("Checked again: every file of the ") + title + " is still right.";
    else
        settingsMessage_ = std::string("Checked again: ") + Files(static_cast<uint64_t>(r.removed)) +
                           " no longer matched and were removed. Drag the disc in again to replace them.";
    verifyIndex_ = -1;
    RefreshStatus();
}

// ---- test hooks

void LauncherUi::ShowScreen(Screen screen) {
    screen_ = screen;
    if (screen == Screen::Play) OpenPlay();
}

void LauncherUi::ShowAbout(bool open) {
    aboutOpen_ = open;
}

void LauncherUi::ShowSettings(bool open) {
    settingsOpen_ = open;
    armedRemove_ = -1;
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
    gold_[i] = gold;
}

bool LauncherUi::Animating() const {
    return ui_.Animating() || dragging_ || noticeLeft_ > 0 || screen_ == Screen::Checking || verify_.valid();
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
    TileText t = DescribeTile(s);
    TileView v;
    v.title = EditionTitle(kEditions[i]);
    v.game = EditionGame(kEditions[i]);
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

float LauncherUi::DrawHeader(const Rect& w, bool buttons) {
    text_.DrawOutlined(style::kTitle, metric::kMargin + 2, kTitleTop, kProductName, colour::kPaper, colour::kInk, 3);
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
        }
        float rw = ui_.ButtonWidth("Park rules");
        x -= rw + 10;
        if (ui_.Button("park-rules", {x, y, rw, h}, "Park rules", ButtonKind::Quiet)) rulesOpen_ = true;
        // The header's Play only where it clears the title (the tiles have their own Play buttons).
        float pw = ui_.ButtonWidth("Play") + 20;
        bool fits = x - pw - 10 > metric::kMargin + 2 + text_.Width(style::kTitle, kProductName) + 16;
        if (screen_ == Screen::Home && AnyPlayable() && fits) {
            x -= pw + 10;
            if (ui_.Button("header-play", {x, y, pw, h}, "Play", ButtonKind::Go)) OpenPlay();
        }
    }
    return kContentTop;
}

void LauncherUi::DrawTiles(const Rect& row, bool dropTargets) {
    float tw = (row.w - 2 * metric::kGap) / 3;
    for (int i = 0; i < 3; ++i) {
        Rect r{row.x + i * (tw + metric::kGap), row.y, tw, row.h};
        tileRects_[i] = r;
        TileView v = TileFor(i);
        v.highlight = dropTargets && dragging_ && dropTarget_ == i;
        ui_.Tile(std::string("tile-") + assets::EditionName(kEditions[i]), r, v);
    }
    tilesShown_ = dropTargets;
}

void LauncherUi::DrawHome(const Rect& w) {
    if (AllConfirmed()) {
        DrawReady(w);
        return;
    }
    float top = DrawHeader(w, true);
    const float m = metric::kMargin;

    // The footer: the descriptive line and the disclaimer, word for word.
    float footerW = w.w - 2 * m - 24;
    std::vector<std::string> disclaimer = text_.Wrap(style::kSmall, kDisclaimer, footerW);
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

    Rect tiles{m, footer.y - metric::kGap - kTileHeight, w.w - 2 * m, kTileHeight};
    Rect field{m, top, w.w - 2 * m, tiles.y - metric::kGap - top - 4};
    fieldRect_ = field;
    bool fieldHover = dragging_ && dropTarget_ == -1;

    // The field's content, centred: icon, heading, one line, the two Browse buttons.
    const float iconH = 44, buttonH = 38;
    float headH = text_.LineHeight(style::kHeading), subH = text_.LineHeight(style::kSmall);
    float block = iconH + 10 + headH + 4 + subH + 14 + buttonH;
    float cy = field.y + std::max(14.0f, (field.h - block) * 0.5f);
    ui_.DropField("field", field, fieldHover, cy);
    cy += iconH + 10;
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
}

// The home once every edition is confirmed: no drop field, two large tiles (the European edition, then the US one), each
// with its Gold badge and its Play button. The whole window still takes a drop; the tiles light up under it as the small
// ones do, and "Game data" opens Settings.
void LauncherUi::DrawReady(const Rect& w) {
    float top = DrawHeader(w, true);
    const float m = metric::kMargin;

    // The footer, as on the home with the drop field.
    float footerW = w.w - 2 * m - 24;
    std::vector<std::string> disclaimer = text_.Wrap(style::kSmall, kDisclaimer, footerW);
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

    // The row above the footer: "Game data" on the left; while a drag is over the window, what letting go does.
    const float rowH = 34;
    Rect row{m, footer.y - metric::kGap - rowH, w.w - 2 * m, rowH};
    float gw = ui_.ButtonWidth("Game data");
    if (ui_.Button("game-data", {row.x, row.y, gw, rowH}, "Game data", ButtonKind::Quiet)) {
        settingsOpen_ = true;
        armedRemove_ = -1;
    }
    if (dragging_) {
        float tx = row.x + gw + 16;
        text_.Draw(style::kBodyBold, tx, row.y + (rowH - text_.LineHeight(style::kBodyBold)) * 0.5f,
                   text_.Fit(style::kBodyBold, "Let go to check them", row.Right() - tx, false), colour::kInk);
    }

    // The two tiles: European on the left, US on the right.
    Rect area{m, top, w.w - 2 * m, row.y - metric::kGap - top};
    float tw = (area.w - metric::kGap) * 0.5f;
    static constexpr int kOrder[2] = {1, 0};   // status_ index of the left and the right tile
    static constexpr const char* kGames[2] = {"Sim Theme Park", "Theme Park World"};   // by status_ index
    const float pad = 16, playH = 52, pillH = 30;
    tileRects_[2] = Rect{};
    for (int k = 0; k < 2; ++k) {
        int i = kOrder[k];
        Rect r{area.x + k * (tw + metric::kGap), area.y, tw, area.h};
        tileRects_[i] = r;
        TileView v;
        v.title = kGames[i];
        v.game = EditionTitle(kEditions[i]);
        v.state = "Ready to play";
        v.detail = Files(status_[i].filesPresent) + " confirmed";
        v.accent = kAccents[i];
        v.badge = Badge::Check;
        v.fraction = 1;
        v.highlight = dragging_ && dropTarget_ == i;
        ui_.Tile(std::string("ready-") + assets::EditionName(kEditions[i]), r, v);

        // Inside, at the bottom: the Gold badge, then the Play button.
        Rect play{r.x + pad, r.Bottom() - pad - playH, r.w - 2 * pad, playH};
        const char* goldText = "Gold Edition included";
        float pw = std::min(play.w, text_.Width(style::kSmallBold, goldText) + 2 * 12 + 22);
        Rect pill{r.CentreX() - pw * 0.5f, play.y - 12 - pillH, pw, pillH};
        canvas_.Outlined(pill, pillH * 0.5f, 2, colour::kYellowSoft, colour::kInk);
        ui_.DrawBadge(Badge::Check, pill.x + 12 + 8, pill.CentreY(), 9, 1);
        float tx = pill.x + 12 + 22;
        text_.Draw(style::kSmallBold, tx, pill.y + (pillH - text_.LineHeight(style::kSmallBold)) * 0.5f,
                   text_.Fit(style::kSmallBold, goldText, pill.Right() - 10 - tx, false), colour::kInk);
        readyPlay_[i] = play;
        if (ui_.Button(std::string("ready-play-") + assets::EditionName(kEditions[i]), play, "Play", ButtonKind::Go,
                       true, style::kHeadline)) {
            playIndex_ = i;
            choice_.edition = kEditions[i];
            choice_.gold = status_[2].unlocked;
            choice_.skipIntro = skipIntro_;
            choice_.backToLauncher = backToLauncher_;
            playChosen_ = true;
            std::string err;
            backend_.SetLastChoice(assets::EditionName(choice_.edition), err);
        }
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
    Rect tiles{m, w.Bottom() - m - kTileHeight, w.w - 2 * m, kTileHeight};
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
    float contentH = text_.LineHeight(style::kHeadline) + 12 + 46 + 8 + 2 * text_.LineHeight(style::kSmall) + 4 + 14 +
                     cardH + 18 + 6 + playH;
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
    std::string line = std::string(EditionGame(kEditions[playIndex_])) + ": " +
                       (s.unlocked ? Files(s.filesPresent) + " confirmed" : DescribeTile(s).detail);
    text_.DrawCentred(style::kSmall, {col.x, y, col.w, text_.LineHeight(style::kSmall)},
                      text_.Fit(style::kSmall, line, col.w, false), colour::kInk);
    y += text_.LineHeight(style::kSmall) + 4;
    int other = 1 - playIndex_;
    if (!status_[other].unlocked) {
        std::string unlock = std::string("The ") + EditionTitle(kEditions[other]) + ": " + DescribeTile(status_[other]).detail;
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
    bool& goldOn = gold_[playIndex_];
    if (!gold.unlocked) goldOn = false;
    ui_.Toggle(std::string("gold-") + assets::EditionName(kEditions[playIndex_]),
               {card.Right() - 16 - 56, card.CentreY() - 16, 56, 32}, goldOn, gold.unlocked);
    y += cardH + 18;

    float playW = 220;
    float bottom = std::max(y + playH, col.Bottom());
    Rect play{col.CentreX() - playW * 0.5f, std::min(y + 6, bottom - playH), playW, playH};
    if (ui_.Button("play", play, "Play", ButtonKind::Go, s.unlocked, style::kHeadline)) {
        choice_.edition = kEditions[playIndex_];
        choice_.gold = goldOn;
        choice_.skipIntro = skipIntro_;
        choice_.backToLauncher = backToLauncher_;
        playChosen_ = true;
        std::string err;
        backend_.SetLastChoice(assets::EditionName(choice_.edition), err);
    }
    float backW = ui_.ButtonWidth("Back");
    if (ui_.Button("play-back", {panel.x + 20, panel.Bottom() - 20 - 38, backW, 38}, "Back", ButtonKind::Quiet))
        screen_ = Screen::Home;
}

void LauncherUi::DrawSettings(const Rect& w) {
    canvas_.FillRound(w, 0, colour::kDim);
    float pw = std::min(w.w - 2 * metric::kMargin, 640.0f);
    float contentH = text_.LineHeight(style::kHeadline) + 12 + 2 * (text_.LineHeight(style::kBodyBold) + 4) + 40 +
                     text_.LineHeight(style::kSmall) + 14 + 3 * 50 + 4 + 2 * text_.LineHeight(style::kSmallBold) +
                     text_.LineHeight(style::kBodyBold) + 4 + 4 * 50;
    float ph = std::min(w.h - 2 * metric::kMargin, contentH + 40);
    Rect panel{w.CentreX() - pw * 0.5f, w.CentreY() - ph * 0.5f, pw, ph};
    ui_.Panel(panel, colour::kPaper, colour::kCream);
    Rect in = panel.Inset(20);
    float y = in.y;
    text_.Draw(style::kHeadline, in.x, y, "Settings", colour::kInk);
    float cw = ui_.ButtonWidth("Close");
    if (ui_.Button("settings-close", {in.Right() - cw, y, cw, 36}, "Close", ButtonKind::Quiet)) {
        settingsOpen_ = false;
        armedRemove_ = -1;
    }
    y += text_.LineHeight(style::kHeadline) + 12;
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
    const float rowH = 50;
    for (int i = 0; i < 3; ++i) {
        Rect row{in.x, y, in.w, rowH - 6};
        canvas_.FillRound(row, 10, i % 2 ? colour::kCream : colour::kYellowSoft.WithAlpha(120));
        const EditionStatus& s = status_[i];
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
                if (backend_.RemoveEdition(kEditions[i], err))
                    settingsMessage_ = std::string("The ") + EditionTitle(kEditions[i]) + " was removed.";
                else
                    settingsMessage_ = "Cannot remove it: " + err;
                armedRemove_ = -1;
                RefreshStatus();
            }
            const char* ask = "Remove its files?";
            text_.Draw(style::kSmallBold, x - 12 - text_.Width(style::kSmallBold, ask),
                       row.CentreY() - text_.LineHeight(style::kSmallBold) * 0.5f, ask, colour::kRed);
        } else {
            float rw = ui_.ButtonWidth("Remove"), vw = ui_.ButtonWidth("Check again");
            x -= rw;
            if (ui_.Button("remove-" + id, {x, by, rw, bh}, "Remove", ButtonKind::Danger, !busy && s.present > 0))
                armedRemove_ = i;
            x -= vw + 8;
            if (ui_.Button("verify-" + id, {x, by, vw, bh}, "Check again", ButtonKind::Plain, !busy && s.present > 0)) {
                verifyIndex_ = i;
                settingsMessage_.clear();
                ImportBackend* backend = &backend_;
                Edition edition = kEditions[i];
                verify_ = std::async(std::launch::async, [backend, edition] {
                    VerifyResult r;
                    r.removed = backend->VerifyEdition(edition, r.err);
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
        const float segW = 210;
        float ty = row.y + (row.h - text_.LineHeight(style::kBodyBold) - text_.LineHeight(style::kSmall)) * 0.5f;
        text_.Draw(style::kBodyBold, row.x + 12, ty, "Interface size", colour::kInk);
        text_.Draw(style::kSmall, row.x + 12, ty + text_.LineHeight(style::kBodyBold),
                   text_.Fit(style::kSmall, "Smaller panels show more of the park", row.w - 12 - segW - 24, false),
                   colour::kInkSoft);
        static const std::vector<std::string> kSizes = {"100%", "85%", "70%"};
        static const char* const kValues[] = {"100", "85", "70"};
        if (ui_.Segmented("interface-size", {row.Right() - 12 - segW, row.CentreY() - 18, segW, 36}, kSizes,
                          std::vector<bool>(kSizes.size(), true), interfaceSize_)) {
            std::string err;
            if (!backend_.SaveSetting(kInterfaceSizeKey, kValues[interfaceSize_], err))
                settingsMessage_ = "Cannot keep the setting: " + err;
        }
        y += rowH;
    }
    y += 4;
    if (verify_.valid() && verifyIndex_ >= 0) {
        std::string msg = std::string("Checking every file of the ") + EditionTitle(kEditions[verifyIndex_]) + " again";
        text_.Draw(style::kSmallBold, in.x, y, msg, colour::kInk);
        ui_.ProgressBar({in.x + text_.Width(style::kSmallBold, msg) + 14, y, 160, 18}, -1);
    } else if (!settingsMessage_.empty()) {
        text_.DrawWrapped(style::kSmallBold, in.x, y, in.w, settingsMessage_, colour::kInk);
    }
}

// The Park rules box: how busy the parks get, kept in launcher.ini and given to the game on Play (launcher/main.cpp).
void LauncherUi::DrawParkRules(const Rect& w) {
    canvas_.FillRound(w, 0, colour::kDim);
    const float rowH = 72;
    float pw = std::min(w.w - 2 * metric::kMargin, 640.0f);
    float contentH = text_.LineHeight(style::kHeadline) + 10 + text_.LineHeight(style::kSmall) + 14 + 2 * rowH;
    float ph = std::min(w.h - 2 * metric::kMargin, contentH + 40);
    Rect panel{w.CentreX() - pw * 0.5f, w.CentreY() - ph * 0.5f, pw, ph};
    ui_.Panel(panel, colour::kPaper, colour::kCream);
    Rect in = panel.Inset(20);
    float y = in.y;
    text_.Draw(style::kHeadline, in.x, y, "Park rules", colour::kInk);
    float cw = ui_.ButtonWidth("Close");
    if (ui_.Button("rules-close", {in.Right() - cw, y, cw, 36}, "Close", ButtonKind::Quiet)) rulesOpen_ = false;
    y += text_.LineHeight(style::kHeadline) + 10;
    text_.Draw(style::kSmall, in.x, y,
               text_.Fit(style::kSmall, "For every park you play, from the next Play. Busier parks need a faster computer.", in.w,
                         false),
               colour::kInkSoft);
    y += text_.LineHeight(style::kSmall) + 14;
    struct Rule {
        const char* id;
        const char* title;
        const char* detail;
        std::vector<std::string> options;
        int* selected;
        const char* key;
        std::vector<std::string> values;
    };
    const Rule rules[] = {
        {"rules-arrivals", "Visitors arriving", "How many guests each bus brings", {"Normal", "More", "Many"}, &arrivals_,
         kVisitorArrivalsKey, {"normal", "more", "many"}},
        {"rules-limit", "Visitor limit", "The most guests in a park at once", {"1,500", "3,000", "5,000"},
         &visitorLimit_, kVisitorLimitKey, {"1500", "3000", "5000"}},
    };
    const float segW = std::min(300.0f, in.w * 0.48f);
    for (const Rule& rule : rules) {
        Rect row{in.x, y, in.w, rowH - 8};
        canvas_.FillRound(row, 10, colour::kYellowSoft.WithAlpha(120));
        float ty = row.y + (row.h - text_.LineHeight(style::kBodyBold) - text_.LineHeight(style::kSmall)) * 0.5f;
        text_.Draw(style::kBodyBold, row.x + 12, ty, rule.title, colour::kInk);
        text_.Draw(style::kSmall, row.x + 12, ty + text_.LineHeight(style::kBodyBold),
                   text_.Fit(style::kSmall, rule.detail, row.w - 12 - segW - 24, false), colour::kInkSoft);
        const std::vector<bool> enabled(rule.options.size(), true);
        if (ui_.Segmented(rule.id, {row.Right() - 12 - segW, row.CentreY() - 20, segW, 40}, rule.options, enabled,
                          *rule.selected)) {
            std::string err;
            if (!backend_.SaveSetting(rule.key, rule.values[static_cast<size_t>(*rule.selected)], err))
                ShowNotice("Cannot keep the setting: " + err, true);
        }
        y += rowH;
    }
}

void LauncherUi::DrawAbout(const Rect& w) {
    canvas_.FillRound(w, 0, colour::kDim);
    float pw = std::min(w.w - 2 * metric::kMargin, 640.0f), inW = pw - 40;
    const std::vector<MadeWith> rows = MadeWithRows(text_);   // the "Made with" table below
    const size_t kRows = rows.size();
    float contentH = text_.LineHeight(style::kHeadline) + 10 +
                     text_.LineHeight(style::kBody) * text_.Wrap(style::kBody, kTagline, inW).size() + 6 +
                     text_.LineHeight(style::kSmallBold) * text_.Wrap(style::kSmallBold, kDisclaimer, inW).size() + 12 +
                     text_.LineHeight(style::kBodyBold) + 4 + kRows * (text_.LineHeight(style::kSmall) + 6);
    float ph = std::min(w.h - 2 * metric::kMargin, contentH + 44);
    Rect panel{w.CentreX() - pw * 0.5f, w.CentreY() - ph * 0.5f, pw, ph};
    ui_.Panel(panel, colour::kPaper, colour::kCream);
    Rect in = panel.Inset(20);
    float y = in.y;
    std::string title = std::string("About ") + kProductName;
    text_.Draw(style::kHeadline, in.x, y, title, colour::kInk);
    float cw = ui_.ButtonWidth("Close");
    if (ui_.Button("about-close", {in.Right() - cw, y, cw, 36}, "Close", ButtonKind::Quiet)) aboutOpen_ = false;
    y += text_.LineHeight(style::kHeadline) + 10;
    y += text_.DrawWrapped(style::kBody, in.x, y, in.w, kTagline, colour::kInk) + 6;
    y += text_.DrawWrapped(style::kSmallBold, in.x, y, in.w, kDisclaimer, colour::kInk) + 12;

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

void LauncherUi::Frame(float dt, float width, float height, float scale) {
    PollDialogs();
    PollVerify();
    PollImport();
    RefreshStatus();
    if (noticeLeft_ > 0) noticeLeft_ = std::max(0.0f, noticeLeft_ - dt);

    canvas_.Begin(scale);
    Rect window{0, 0, width, height};
    ui_.Begin(dt, input_);
    input_.pressed = input_.released = false;
    input_.wheel = 0;

    bool overlay = aboutOpen_ || settingsOpen_ || rulesOpen_;
    ui_.SetInteractive(!overlay && !dragging_);
    tilesShown_ = false;
    readyShown_ = false;
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
        if (rulesOpen_) DrawParkRules(window);
        if (aboutOpen_) DrawAbout(window);
    }
    if (noticeLeft_ > 0 && !notice_.empty())
        ui_.Notice(window, notice_, noticeProblem_ ? Mix(colour::kRed, colour::kPaper, 0.55f) : colour::kYellowSoft);
    ui_.End();
}

}  // namespace tpw::ui
