// host_ui_screens.cpp: the launcher window's screens, drawn offscreen. SDL's software renderer draws into a plain surface
// (no window, no video subsystem) every screen state, at 1x and 2x, from a scripted import: a fake backend whose
// editions' states are computed from the compiled-in manifest exactly as the importer computes them, with no disc
// anywhere. Each picture is written as a PNG for a look by eye; the test itself checks only that the drawing worked and
// the pictures have the right size, plus the wording functions (texts.h) and a button click.
//
// Usage: tpw_launcher_ui_tests [output folder]   (default: TPW_UI_SCREENS_DIR from the environment, else the build's
// ui-screens/ folder). ctest runs it as "launcher_ui".
#include <SDL3/SDL.h>
#include <zlib.h>

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "backend.h"
#include "launcher_ui.h"
#include "manifest.h"
#include "texts.h"
#include "theme.h"
#include "widgets.h"

#ifndef TPW_UI_SCREENS_DEFAULT
#define TPW_UI_SCREENS_DEFAULT "ui-screens"
#endif

using namespace tpw::ui;
using tpw::assets::Edition;
using tpw::assets::EditionStatus;
using tpw::assets::ImportProgress;
using tpw::assets::ImportReport;
using tpw::assets::ManifestRecord;
namespace assets = tpw::assets;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    }
}

void CheckEq(const std::string& got, const std::string& want, const std::string& what) {
    Check(got == want, what + ": got \"" + got + "\", want \"" + want + "\"");
}

// ---- PNG, written by hand with zlib (SDL 3.2 has no PNG writer)

void Put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<uint8_t>(v >> s));
}

void Chunk(std::vector<uint8_t>& png, const char type[4], const std::vector<uint8_t>& data) {
    Put32(png, static_cast<uint32_t>(data.size()));
    size_t start = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data.begin(), data.end());
    uLong crc = crc32(0, png.data() + start, static_cast<uInt>(png.size() - start));
    Put32(png, static_cast<uint32_t>(crc));
}

// `rgb`: rows of width * 3 bytes, `pitch` apart.
bool WritePng(const std::string& path, int width, int height, const uint8_t* rgb, int pitch) {
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (width * 3 + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);   // no filter
        const uint8_t* row = rgb + static_cast<size_t>(y) * pitch;
        raw.insert(raw.end(), row, row + width * 3);
    }
    uLongf packedSize = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> packed(packedSize);
    if (compress2(packed.data(), &packedSize, raw.data(), static_cast<uLong>(raw.size()), 6) != Z_OK) return false;
    packed.resize(packedSize);

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<uint8_t> header;
    Put32(header, static_cast<uint32_t>(width));
    Put32(header, static_cast<uint32_t>(height));
    header.insert(header.end(), {8, 2, 0, 0, 0});   // 8 bits, RGB, deflate, no filter choice, no interlace
    Chunk(png, "IHDR", header);
    Chunk(png, "IDAT", packed);
    Chunk(png, "IEND", {});
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    return std::fclose(f) == 0 && ok;
}

// The PNG's own header: width and height.
bool PngSize(const std::string& path, int& width, int& height) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    uint8_t head[24];
    bool ok = std::fread(head, 1, sizeof head, f) == sizeof head;
    std::fclose(f);
    if (!ok || head[0] != 0x89 || head[1] != 'P' || std::string(reinterpret_cast<char*>(head + 12), 4) != "IHDR")
        return false;
    auto get = [&](int at) {
        return static_cast<int>((uint32_t(head[at]) << 24) | (uint32_t(head[at + 1]) << 16) |
                                (uint32_t(head[at + 2]) << 8) | uint32_t(head[at + 3]));
    };
    width = get(16);
    height = get(20);
    return true;
}

// ---- the scripted backend

bool SideFile(const ManifestRecord& r) {
    return (r.flags & (assets::kFlagRsrc | assets::kFlagFinfo)) != 0;
}

class FakeBackend : public ImportBackend {
public:
    // What one Start() does: the events it sends, the counter it leaves, and what it changes when it ends.
    struct Script {
        std::vector<ImportProgress> events;
        ImportProgress latest;
        bool holdOpen = false;   // keeps running until Release()
        std::function<void(FakeBackend&, ImportReport&)> finish;
    };

    FakeBackend() : present_(assets::ManifestRecordCount(), 0) {}

    // Marks the records of `edition` that `pick` chooses as present.
    void Fill(Edition edition, const std::function<bool(size_t, const ManifestRecord&)>& pick) {
        const ManifestRecord* records = assets::ManifestRecords();
        for (size_t i = 0; i < assets::ManifestRecordCount(); ++i)
            if ((records[i].editions & edition) && pick(i, records[i])) present_[i] |= edition;
    }
    void FillAll(Edition edition) {
        Fill(edition, [](size_t, const ManifestRecord&) { return true; });
    }
    // Present records of `edition` that are now marked, counted as files of their own.
    size_t CountFiles(Edition edition) const {
        size_t n = 0;
        const ManifestRecord* records = assets::ManifestRecords();
        for (size_t i = 0; i < assets::ManifestRecordCount(); ++i)
            n += (present_[i] & edition) && !SideFile(records[i]);
        return n;
    }

    void Queue(Script script) { scripts_.push_back(std::move(script)); }
    void Release() { hold_ = false; }
    const std::vector<std::vector<std::string>>& Started() const { return started_; }

    bool Start(const std::vector<std::string>& paths, std::string& err) override {
        if (running_) {
            err = "already running";
            return false;
        }
        started_.push_back(paths);
        current_ = scripts_.empty() ? Script() : std::move(scripts_.front());
        if (!scripts_.empty()) scripts_.pop_front();
        pending_ = current_.events;
        latest_ = current_.latest;
        hold_ = current_.holdOpen;
        running_ = true;
        drained_ = false;
        return true;
    }
    bool Running() const override { return running_ && (hold_ || !drained_); }
    void Cancel() override { cancelled_ = true; hold_ = false; }
    std::vector<ImportProgress> Drain() override {
        std::vector<ImportProgress> out;
        out.swap(pending_);
        drained_ = true;
        return out;
    }
    ImportProgress Latest() const override { return latest_; }
    ImportReport Wait() override {
        ImportReport report;
        report.cancelled = cancelled_;
        if (current_.finish && !cancelled_) current_.finish(*this, report);
        for (int i = 0; i < 3; ++i) report.status[i] = Status(kEditions[i]);
        ImportProgress done;
        done.kind = ImportProgress::Kind::Finished;
        done.message = report.Summary();
        pending_.push_back(done);
        running_ = false;
        cancelled_ = false;
        current_ = Script();
        return report;
    }
    // As Importer::StatusLocked (assets/importer.cpp).
    EditionStatus Status(Edition edition) const override {
        EditionStatus s;
        s.edition = edition;
        const ManifestRecord* records = assets::ManifestRecords();
        for (size_t i = 0; i < assets::ManifestRecordCount(); ++i) {
            const ManifestRecord& r = records[i];
            if (!(r.editions & edition)) continue;
            bool have = (present_[i] & edition) != 0;
            bool required = (r.flags & assets::kFlagRequired) != 0;
            bool file = !SideFile(r);
            ++s.records;
            s.required += required;
            s.files += file;
            if (have) {
                ++s.present;
                s.requiredPresent += required;
                s.filesPresent += file;
                s.bytesPresent += assets::RecordHash(r, 0).size;
            } else if (required && r.source < 3) {
                ++s.missing[r.source];
                if (file) ++s.missingFiles[r.source];
            }
        }
        s.unlocked = s.required > 0 && s.requiredPresent == s.required;
        return s;
    }
    std::string DataFolder() const override { return "/home/player/.local/share/" + std::string(kDataFolderName); }
    bool ChangeDataFolder(const std::string&, std::string& err) override {
        err = "not in the test";
        return false;
    }
    long VerifyEdition(Edition, std::string&) override { return 0; }
    bool RemoveEdition(Edition edition, std::string&) override {
        for (uint8_t& p : present_) p &= static_cast<uint8_t>(~edition);
        return true;
    }
    std::string LastChoice() const override { return lastChoice_; }
    bool SetLastChoice(const std::string& choice, std::string&) override {
        lastChoice_ = choice;
        return true;
    }
    std::string StartupNotice() const override { return startupNotice; }
    std::string startupNotice;   // what the startup check "removed"
    std::string Setting(const std::string& key) const override {
        auto it = settings.find(key);
        return it == settings.end() ? std::string() : it->second;
    }
    bool SaveSetting(const std::string& key, const std::string& value, std::string&) override {
        settings[key] = value;
        return true;
    }
    std::map<std::string, std::string> settings;   // launcher.ini's settings, as SaveSetting kept them

private:
    std::vector<uint8_t> present_;
    std::deque<Script> scripts_;
    Script current_;
    std::vector<ImportProgress> pending_;
    ImportProgress latest_;
    bool running_ = false, hold_ = false, drained_ = false, cancelled_ = false;
    std::vector<std::vector<std::string>> started_;
    std::string lastChoice_ = "eur";
};

// Records of an edition that are files of their own, from one disc.
std::vector<size_t> FileRecords(Edition edition, assets::DiscSource source) {
    std::vector<size_t> out;
    const ManifestRecord* records = assets::ManifestRecords();
    for (size_t i = 0; i < assets::ManifestRecordCount(); ++i)
        if ((records[i].editions & edition) && records[i].source == source && !SideFile(records[i])) out.push_back(i);
    return out;
}

// The European edition as an older pressing of the PC CD leaves it: every sixth PC CD file has other content.
bool OlderPressingMiss(size_t nth) {
    return nth % 6 == 5;
}

void DropOlderPressing(FakeBackend& b, ImportReport& report) {
    std::vector<size_t> pc = FileRecords(assets::kEditionEur, assets::kSourcePcCd);
    std::vector<bool> miss(assets::ManifestRecordCount(), false);
    for (size_t k = 0; k < pc.size(); ++k) {
        if (!OlderPressingMiss(k)) continue;
        miss[pc[k]] = true;
        report.differentVersions.push_back(assets::ManifestRecords()[pc[k]].dest);
    }
    // A side file goes with the file before it.
    const ManifestRecord* records = assets::ManifestRecords();
    bool last = false;
    for (size_t i = 0; i < miss.size(); ++i) {
        if (!(records[i].editions & assets::kEditionEur)) continue;
        if (SideFile(records[i])) miss[i] = last;
        else last = miss[i];
    }
    size_t before = b.CountFiles(assets::kEditionEur);
    b.Fill(assets::kEditionEur,
           [&](size_t i, const ManifestRecord& r) { return r.source == assets::kSourcePcCd && !miss[i]; });
    report.written = b.CountFiles(assets::kEditionEur) - before;
    report.entries = report.hashed = report.matched = pc.size();
    report.unknownFiles = 212;
}

// Some log rows: real destinations from the manifest, so the log looks as it will.
std::vector<ImportProgress> LogEvents(size_t count) {
    std::vector<ImportProgress> out;
    std::vector<size_t> pc = FileRecords(assets::kEditionEur, assets::kSourcePcCd);
    for (size_t k = 0; k < count && k < pc.size(); ++k) {
        ImportProgress p;
        p.kind = k % 9 == 4 ? ImportProgress::Kind::AlreadyThere
                 : k % 13 == 7 ? ImportProgress::Kind::DifferentVersion
                               : ImportProgress::Kind::Written;
        p.path = std::string("eur/") + assets::ManifestRecords()[pc[k]].dest;
        out.push_back(p);
    }
    return out;
}

// ---- drawing a state

struct Shot {
    std::string name;
    int width = metric::kStartWidth, height = metric::kStartHeight;
    std::function<void(FakeBackend&)> before;                    // the folder before the screen opens
    std::function<void(LauncherUi&, FakeBackend&, const std::function<void(int)>&)> drive;   // frames(n)
    LauncherUi::Screen screen = LauncherUi::Screen::Home;
};

constexpr float kDt = 1.0f / 60;

SDL_Event DropEvent(SDL_EventType type, float x = 0, float y = 0, const char* data = nullptr) {
    SDL_Event e{};
    e.type = type;
    e.drop.x = x;
    e.drop.y = y;
    e.drop.data = data;
    return e;
}

SDL_Event MotionEvent(float x, float y) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = x;
    e.motion.y = y;
    return e;
}

SDL_Event ButtonEvent(SDL_EventType type, float x, float y) {
    SDL_Event e{};
    e.type = type;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = x;
    e.button.y = y;
    return e;
}

// `lines` of the wheel; positive: towards the top.
SDL_Event WheelEvent(float lines) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_WHEEL;
    e.wheel.y = lines;
    e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    return e;
}

void FillEverything(FakeBackend& b) {
    b.FillAll(assets::kEditionUs);
    b.FillAll(assets::kEditionEur);
    b.FillAll(assets::kEditionGold);
}

std::vector<std::string> g_written;

void Render(const std::string& dir, const Shot& shot, int scale) {
    std::string label = shot.name + " @" + std::to_string(scale) + "x";
    int pw = shot.width * scale, ph = shot.height * scale;
    SDL_Surface* surface = SDL_CreateSurface(pw, ph, SDL_PIXELFORMAT_XRGB8888);
    Check(surface != nullptr, label + ": SDL_CreateSurface: " + SDL_GetError());
    if (!surface) return;
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    Check(renderer != nullptr, label + ": SDL_CreateSoftwareRenderer: " + SDL_GetError());
    if (!renderer) {
        SDL_DestroySurface(surface);
        return;
    }
    {
        FakeBackend backend;
        if (shot.before) shot.before(backend);
        LauncherUi ui(renderer, backend);
        Check(ui.Ok(), label + ": fonts: " + ui.Error());
        auto step = [&](float dt) {
            ui.Frame(dt, static_cast<float>(shot.width), static_cast<float>(shot.height), static_cast<float>(scale));
        };
        auto frames = [&](int n) {
            for (int i = 0; i < n; ++i) step(kDt);
        };
        frames(1);
        if (shot.drive) shot.drive(ui, backend, frames);
        // Let the springs settle: two seconds in long steps (the springs integrate in small ones of their own), as
        // every frame costs a full software drawing.
        for (int i = 0; i < 20; ++i) step(0.1f);
        Check(ui.CurrentScreen() == shot.screen, label + ": not on the expected screen");
        Check(SDL_FlushRenderer(renderer), label + ": SDL_FlushRenderer: " + SDL_GetError());
        SDL_Surface* rgb = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGB24);
        Check(rgb != nullptr, label + ": SDL_ConvertSurface: " + SDL_GetError());
        if (rgb) {
            // Drawn at all: more than a handful of distinct colours.
            const uint8_t* px = static_cast<const uint8_t*>(rgb->pixels);
            std::vector<uint32_t> seen;
            for (int y = 0; y < rgb->h && seen.size() < 64; y += 7)
                for (int x = 0; x < rgb->w && seen.size() < 64; x += 5) {
                    const uint8_t* p = px + static_cast<size_t>(y) * rgb->pitch + x * 3;
                    uint32_t c = (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
                    bool known = false;
                    for (uint32_t s : seen) known = known || s == c;
                    if (!known) seen.push_back(c);
                }
            Check(seen.size() >= 16, label + ": the picture is nearly blank");
            std::string path = dir + "/" + shot.name + "@" + std::to_string(scale) + "x.png";
            Check(WritePng(path, rgb->w, rgb->h, px, rgb->pitch), label + ": cannot write " + path);
            int w = 0, h = 0;
            Check(PngSize(path, w, h) && w == pw && h == ph, label + ": the PNG has the wrong size");
            g_written.push_back(path);
            SDL_DestroySurface(rgb);
        }
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

// ---- the states

void PartialUsEur(FakeBackend& b) {
    b.FillAll(assets::kEditionUs);
    b.Fill(assets::kEditionEur, [](size_t, const ManifestRecord& r) { return r.source == assets::kSourcePcCd; });
}

// The About box's "Made with" rows: every one has a version and a licence, and the versions are the libraries' own
// (SDL's run-time version equals the headers the build compiled, zlib's likewise), none written into the launcher.
void CheckAboutRows(LauncherUi& ui) {
    static bool done = false;   // the shot is drawn at two scales; once is enough
    if (done) return;
    done = true;
    std::vector<MadeWith> rows = ui.AboutRows();
    auto find = [&](const std::string& prefix) -> const MadeWith* {
        for (const MadeWith& r : rows)
            if (r.name.compare(0, prefix.size(), prefix) == 0) return &r;
        return nullptr;
    };
    for (const MadeWith& r : rows) {
        Check(!r.version.empty(), "About: " + r.name + " has a version");
        Check(!r.license.empty(), "About: " + r.name + " has a licence");
        Check(r.version.find_first_not_of("0123456789.ab, ") == std::string::npos,
              "About: " + r.name + "'s version \"" + r.version + "\" is a version number");
    }
    std::string sdl = std::to_string(SDL_MAJOR_VERSION) + "." + std::to_string(SDL_MINOR_VERSION) + "." +
                      std::to_string(SDL_MICRO_VERSION);
    const MadeWith* row = find("SDL");
    Check(row != nullptr, "About lists SDL");
    if (row) CheckEq(row->version, sdl, "About: SDL's version is the headers' (SDL_VERSION)");
    row = find("zlib");
    Check(row != nullptr, "About lists zlib");
    if (row) CheckEq(row->version, ZLIB_VERSION, "About: zlib's version is the headers' (ZLIB_VERSION)");
    row = find("libjpeg");
    Check(row != nullptr, "About lists libjpeg");
    if (row) CheckEq(row->license, "IJG License", "About: libjpeg's licence");
    Check(find("stb_truetype") != nullptr, "About lists stb");
    Check(find("Fredoka") != nullptr && find("Nunito") != nullptr, "About lists both fonts");
#if TPW_TEST_FFMPEG
    row = find("FFmpeg");
    Check(row != nullptr, "About lists FFmpeg (the build plays the films)");
    if (row) CheckEq(row->license, "GNU LGPL 2.1 or later", "About: FFmpeg's licence");
#else
    Check(find("FFmpeg") == nullptr, "About leaves FFmpeg out (the build skips the films)");
#endif
}

std::vector<Shot> Shots() {
    using Screen = LauncherUi::Screen;
    std::vector<Shot> shots;

    shots.push_back({"01-home-empty", metric::kStartWidth, metric::kStartHeight, nullptr, nullptr, Screen::Home});

    shots.push_back({"02-home-drag-over-field", metric::kStartWidth, metric::kStartHeight, nullptr,
                     [](LauncherUi& ui, FakeBackend&, const std::function<void(int)>&) {
                         ui.HandleEvent(DropEvent(SDL_EVENT_DROP_BEGIN));
                         ui.HandleEvent(DropEvent(SDL_EVENT_DROP_POSITION, 400, 200));
                     },
                     Screen::Home});

    shots.push_back({"03-home-drag-over-eu-tile", metric::kStartWidth, metric::kStartHeight, nullptr,
                     [](LauncherUi& ui, FakeBackend&, const std::function<void(int)>&) {
                         Rect t = ui.TileRect(1);
                         ui.HandleEvent(DropEvent(SDL_EVENT_DROP_BEGIN));
                         ui.HandleEvent(DropEvent(SDL_EVENT_DROP_POSITION, t.CentreX(), t.CentreY()));
                     },
                     Screen::Home});

    shots.push_back({"04-checking", metric::kStartWidth, metric::kStartHeight, nullptr,
                     [](LauncherUi& ui, FakeBackend& b, const std::function<void(int)>& frames) {
                         FakeBackend::Script s;
                         s.events = LogEvents(40);
                         size_t total = FileRecords(assets::kEditionEur, assets::kSourcePcCd).size() + 212;
                         s.latest.kind = ImportProgress::Kind::Hashing;
                         s.latest.n = total * 2 / 5;
                         s.latest.total = total;
                         s.latest.totalBytes = 412ull << 20;
                         s.latest.bytes = s.latest.totalBytes * 2 / 5;
                         s.latest.path = "Theme Park World.iso/a folder/another folder/a file being read";
                         s.holdOpen = true;
                         b.Queue(s);
                         ui.OfferPaths({"/home/player/Theme Park World.iso"});
                         frames(2);
                         ui.OfferPaths({"/home/player/SimTheme Park.dmg"});   // queued, with a notice
                     },
                     Screen::Checking});

    shots.push_back({"05-result-older-pressing", metric::kStartWidth, metric::kStartHeight, nullptr,
                     [](LauncherUi& ui, FakeBackend& b, const std::function<void(int)>&) {
                         FakeBackend::Script s;
                         s.events = LogEvents(12);
                         s.finish = DropOlderPressing;
                         b.Queue(s);
                         ui.OfferPaths({"/media/PC CD"});
                     },
                     Screen::Result});

    shots.push_back({"06-result-all-confirmed", metric::kStartWidth, metric::kStartHeight,
                     [](FakeBackend& b) {
                         b.Fill(assets::kEditionEur,
                                [](size_t, const ManifestRecord& r) { return r.source == assets::kSourcePcCd; });
                     },
                     [](LauncherUi& ui, FakeBackend& b, const std::function<void(int)>&) {
                         FakeBackend::Script s;
                         s.finish = [](FakeBackend& fb, ImportReport& r) {
                             size_t before = fb.CountFiles(assets::kEditionUs) + fb.CountFiles(assets::kEditionEur);
                             fb.FillAll(assets::kEditionUs);
                             fb.FillAll(assets::kEditionEur);
                             r.written = fb.CountFiles(assets::kEditionUs) + fb.CountFiles(assets::kEditionEur) - before;
                             r.unknownFiles = 31;
                         };
                         b.Queue(s);
                         ui.OfferPaths({"/media/Mac CD"});
                     },
                     Screen::Result});

    shots.push_back({"07-result-mac-files-missing", metric::kStartWidth, metric::kStartHeight, nullptr,
                     [](LauncherUi& ui, FakeBackend& b, const std::function<void(int)>&) {
                         FakeBackend::Script s;
                         s.events = LogEvents(30);
                         s.finish = [](FakeBackend& fb, ImportReport& r) {
                             fb.Fill(assets::kEditionEur,
                                     [](size_t, const ManifestRecord& rec) { return rec.source == assets::kSourcePcCd; });
                             r.written = fb.CountFiles(assets::kEditionEur);
                             r.unknownFiles = 212;
                         };
                         b.Queue(s);
                         ui.OfferPaths({"/media/PC CD"});
                     },
                     Screen::Result});

    shots.push_back({"08-home-mixed-hover-tile", metric::kStartWidth, metric::kStartHeight,
                     [](FakeBackend& b) {
                         b.FillAll(assets::kEditionUs);
                         b.Fill(assets::kEditionEur, [](size_t i, const ManifestRecord& r) {
                             return r.source == assets::kSourcePcCd && i % 3 != 0;
                         });
                     },
                     [](LauncherUi& ui, FakeBackend&, const std::function<void(int)>&) {
                         Rect t = ui.TileRect(1);
                         ui.HandleEvent(MotionEvent(t.CentreX(), t.CentreY()));
                     },
                     Screen::Home});

    shots.push_back({"09-play-two-editions-gold", metric::kStartWidth, metric::kStartHeight,
                     [](FakeBackend& b) {
                         b.FillAll(assets::kEditionUs);
                         b.FillAll(assets::kEditionEur);
                         b.FillAll(assets::kEditionGold);
                     },
                     [](LauncherUi& ui, FakeBackend&, const std::function<void(int)>&) {
                         ui.ShowScreen(LauncherUi::Screen::Play);
                         ui.ChoosePlay(assets::kEditionEur, true);
                     },
                     Screen::Play});

    shots.push_back({"10-play-one-edition", metric::kStartWidth, metric::kStartHeight, PartialUsEur,
                     [](LauncherUi& ui, FakeBackend&, const std::function<void(int)>&) {
                         ui.ShowScreen(LauncherUi::Screen::Play);
                     },
                     Screen::Play});

    shots.push_back({"11-settings-remove-confirm", metric::kStartWidth, metric::kStartHeight, PartialUsEur,
                     [](LauncherUi& ui, FakeBackend&, const std::function<void(int)>&) {
                         ui.ArmRemove(assets::kEditionUs);
                     },
                     Screen::Home});

    shots.push_back({"12-about", metric::kStartWidth, metric::kStartHeight, nullptr,
                     [](LauncherUi& ui, FakeBackend&, const std::function<void(int)>&) {
                         ui.SetLogsFolder("/Users/player/Library/Application Support/STP-TPW Launcher/logs");
                         ui.ShowAbout(true);
                         CheckAboutRows(ui);
                     },
                     Screen::Home});

    shots.push_back({"13-home-wide", 1100, 760, PartialUsEur, nullptr, Screen::Home});

    shots.push_back({"14-home-smallest", metric::kMinWidth, metric::kMinHeight, nullptr, nullptr, Screen::Home});

    shots.push_back({"15-result-older-pressing-smallest", metric::kMinWidth, metric::kMinHeight, nullptr,
                     [](LauncherUi& ui, FakeBackend& b, const std::function<void(int)>&) {
                         FakeBackend::Script s;
                         s.finish = DropOlderPressing;
                         b.Queue(s);
                         ui.OfferPaths({"/media/PC CD"});
                     },
                     Screen::Result});

    shots.push_back({"16-checking-smallest", metric::kMinWidth, metric::kMinHeight, nullptr,
                     [](LauncherUi& ui, FakeBackend& b, const std::function<void(int)>&) {
                         FakeBackend::Script s;
                         s.events = LogEvents(200);
                         s.latest.kind = ImportProgress::Kind::Scanning;
                         s.latest.total = 1250;
                         s.latest.path = "SimTheme Park.dmg/SimTheme Park/Data";
                         s.holdOpen = true;
                         b.Queue(s);
                         ui.OfferPaths({"/home/player/SimTheme Park.dmg"});
                     },
                     Screen::Checking});

    shots.push_back({"17-result-nothing-new", metric::kStartWidth, metric::kStartHeight, PartialUsEur,
                     [](LauncherUi& ui, FakeBackend& b, const std::function<void(int)>&) {
                         FakeBackend::Script s;
                         s.finish = [](FakeBackend& fb, ImportReport& r) {
                             r.alreadyThere = fb.CountFiles(assets::kEditionUs);
                         };
                         b.Queue(s);
                         ui.OfferPaths({"/media/Mac CD"});
                     },
                     Screen::Result});

    shots.push_back({"19-result-damaged", metric::kStartWidth, metric::kStartHeight, nullptr,
                     [](LauncherUi& ui, FakeBackend& b, const std::function<void(int)>&) {
                         FakeBackend::Script s;
                         s.finish = [](FakeBackend&, ImportReport& r) {
                             r.errors.push_back("Mac CD.zip/damaged.dat: zip: checksum mismatch in damaged.dat");
                         };
                         b.Queue(s);
                         ui.OfferPaths({"/media/Mac CD.zip"});
                     },
                     Screen::Result});

    shots.push_back({"18-play-wide", 1100, 760, PartialUsEur,
                     [](LauncherUi& ui, FakeBackend&, const std::function<void(int)>&) {
                         ui.ShowScreen(LauncherUi::Screen::Play);
                     },
                     Screen::Play});
    // After Play (launcher/ui/starting.cpp): the Play screen dimmed under the Starting box, with the bar at 42% of the
    // last start's total, and with no total yet (the bar moves without a number).
    shots.push_back({"16-starting-eur-42-percent", metric::kStartWidth, metric::kStartHeight, PartialUsEur,
                     [](LauncherUi& ui, FakeBackend&, const std::function<void(int)>&) {
                         ui.ShowScreen(LauncherUi::Screen::Play);
                         ui.BeginStarting(assets::kEditionEur);
                         ui.SetStartingProgress(0.42f);
                     },
                     Screen::Starting});
    shots.push_back({"17-starting-indeterminate", metric::kStartWidth, metric::kStartHeight, PartialUsEur,
                     [](LauncherUi& ui, FakeBackend&, const std::function<void(int)>&) {
                         ui.ShowScreen(LauncherUi::Screen::Play);
                         ui.BeginStarting(assets::kEditionEur);
                         ui.SetStartingProgress(-1.0f);
                     },
                     Screen::Starting});

    // Every edition confirmed: two large tiles with their Gold badges and Play buttons, no drop field.
    shots.push_back({"12-home-all-confirmed", metric::kStartWidth, metric::kStartHeight, FillEverything, nullptr,
                     Screen::Home});

    shots.push_back({"13-home-all-confirmed-drag-over", metric::kStartWidth, metric::kStartHeight, FillEverything,
                     [](LauncherUi& ui, FakeBackend&, const std::function<void(int)>&) {
                         Rect t = ui.TileRect(1);
                         ui.HandleEvent(DropEvent(SDL_EVENT_DROP_BEGIN));
                         ui.HandleEvent(DropEvent(SDL_EVENT_DROP_POSITION, t.CentreX(), t.CentreY()));
                     },
                     Screen::Home});

    // Settings taller than the window: scrolled to its end with the wheel (the pointer in the box's left margin), and
    // in the smallest window at its top, the last row cut off under the bar.
    shots.push_back({"20-settings-scrolled-down", metric::kStartWidth, metric::kStartHeight, PartialUsEur,
                     [](LauncherUi& ui, FakeBackend&, const std::function<void(int)>& frames) {
                         ui.ShowSettings(true);
                         frames(1);
                         ui.HandleEvent(MotionEvent(90, metric::kStartHeight * 0.5f));
                         ui.HandleEvent(WheelEvent(-10));
                     },
                     Screen::Home});
    shots.push_back({"21-settings-smallest", metric::kMinWidth, metric::kMinHeight, PartialUsEur,
                     [](LauncherUi& ui, FakeBackend&, const std::function<void(int)>&) { ui.ShowSettings(true); },
                     Screen::Home});
    return shots;
}

// ---- checks that need no pictures

void CheckTexts() {
    CheckEq(FileUrl("/Users/a b/Library/Application Support/STP-TPW Launcher/logs"),
            "file:///Users/a%20b/Library/Application%20Support/STP-TPW%20Launcher/logs", "FileUrl: spaces");
    CheckEq(FileUrl("/Users/Ren\xC3\xA9" "e/logs#1"), "file:///Users/Ren%C3%A9e/logs%231", "FileUrl: UTF-8 and '#'");
    CheckEq(FormatCount(0), "0", "FormatCount 0");
    CheckEq(FormatCount(999), "999", "FormatCount 999");
    CheckEq(FormatCount(1683), "1,683", "FormatCount 1683");
    CheckEq(FormatCount(1234567), "1,234,567", "FormatCount 1234567");
    CheckEq(FormatBytes(512), "512 bytes", "FormatBytes 512");
    CheckEq(FormatBytes(1536), "1.5 KB", "FormatBytes 1536");
    CheckEq(FormatBytes(412ull << 20), "412 MB", "FormatBytes 412 MB");
    CheckEq(Files(1), "1 file", "Files 1");
    CheckEq(Files(12), "12 files", "Files 12");

    EditionStatus s;
    s.missingFiles[assets::kSourceMacCd] = 12;
    s.missing[assets::kSourceMacCd] = 14;
    CheckEq(MissingText(s), std::string("12 files still missing: they are on ") +
                                assets::SourceDiscName(assets::kSourceMacCd), "MissingText one disc");
    s.missingFiles[assets::kSourcePcCd] = 83;
    s.missing[assets::kSourcePcCd] = 83;
    CheckEq(MissingText(s), std::string("95 files still missing: 83 on ") + assets::SourceDiscName(assets::kSourcePcCd) +
                                " and 12 on " + assets::SourceDiscName(assets::kSourceMacCd),
            "MissingText two discs");
    CheckEq(DiscsNeeded(s), std::string(assets::SourceDiscName(assets::kSourcePcCd)) + " and " +
                                assets::SourceDiscName(assets::kSourceMacCd), "DiscsNeeded");

    // The tiles and the older pressing's headline, from the backend's own Status counts.
    FakeBackend b;
    EditionStatus empty = b.Status(assets::kEditionEur);
    Check(empty.required > 0 && empty.files > 0, "the manifest lists European files");
    TileText t = DescribeTile(empty);
    CheckEq(t.state, Files(empty.files) + " to find", "empty tile state");
    CheckEq(t.detail, "Drag in " + DiscsNeeded(empty) + " to unlock", "empty tile detail");

    EditionStatus before[3];
    for (int i = 0; i < 3; ++i) before[i] = b.Status(kEditions[i]);
    ImportReport report;
    DropOlderPressing(b, report);
    for (int i = 0; i < 3; ++i) report.status[i] = b.Status(kEditions[i]);
    const EditionStatus& eur = report.status[1];
    Check(LooksLikeOlderPressing(report), "an older pressing is recognised");
    ResultText r = DescribeResult(report, before);
    CheckEq(r.headline, "Older pressing, " + FormatCount(eur.filesPresent) + " of " + FormatCount(eur.files) + " confirmed",
            "older pressing headline");
    std::printf("older pressing (scripted): %s\n", r.headline.c_str());
    CheckEq(DescribeTile(eur).state, FormatCount(eur.filesPresent) + " of " + FormatCount(eur.files) + " confirmed",
            "partial tile state");

    b.FillAll(assets::kEditionEur);
    EditionStatus full = b.Status(assets::kEditionEur);
    Check(full.unlocked, "a full European folder unlocks");
    CheckEq(DescribeTile(full).state, "Ready to play", "unlocked tile state");
    CheckEq(DescribeTile(full).detail, Files(full.filesPresent) + " confirmed", "unlocked tile detail");

    // a damaged container: the result names the file and the reason, and is not "nothing new"
    ImportReport damaged;
    damaged.errors.push_back("Mac CD.zip/damaged.dat: zip: checksum mismatch in damaged.dat");
    for (int i = 0; i < 3; ++i) damaged.status[i] = before[i];
    ResultText dr = DescribeResult(damaged, before);
    CheckEq(dr.headline, "A file could not be read", "damaged container headline");
    Check(!dr.good && !dr.lines.empty() && dr.lines.front() == damaged.errors.front(), "damaged container names the file and the reason");
    damaged.alreadyThere = 5;
    Check(DescribeResult(damaged, before).headline == "A file could not be read", "an error beats \"nothing new\"");
    damaged.errors.push_back("second.zip: zip: bad deflate data in x");
    ResultText two = DescribeResult(damaged, before);
    CheckEq(two.headline, "Some files could not be read", "several damaged containers headline");
    bool counted = false;
    for (const std::string& line : two.lines) counted = counted || line == "2 problems in all";
    Check(counted, "the count of problems is shown");

    LogRow row;
    ImportProgress hashing;
    hashing.kind = ImportProgress::Kind::Hashing;
    Check(!DescribeProgress(hashing, row), "hashing events make no log row");
    ImportProgress written;
    written.kind = ImportProgress::Kind::Written;
    written.path = "eur/x";
    Check(DescribeProgress(written, row) && row.tone == LogRow::Good && row.path == "eur/x", "written log row");
}

// A press and a release on a button click it, once; a press off it does not.
void CheckButton(SDL_Renderer* renderer) {
    Canvas canvas(renderer);
    Text text(canvas);
    Check(text.Ok(), "fonts load: " + text.Error());
    Ui ui(canvas, text);
    Rect r{100, 100, 120, 40};
    auto frame = [&](Input in) {
        canvas.Begin(1);
        ui.Begin(kDt, in);
        bool clicked = ui.Button("b", r, "Button", ButtonKind::Go);
        ui.End();
        return clicked;
    };
    Input in;
    in.mouseX = 150;
    in.mouseY = 120;
    Check(!frame(in), "hover is no click");
    in.down = in.pressed = true;
    Check(!frame(in), "a press alone is no click");
    in.pressed = false;
    in.down = false;
    in.released = true;
    Check(frame(in), "press and release click");
    in.released = false;
    Check(!frame(in), "one click only");
    in.mouseX = 10;
    in.down = in.pressed = true;
    frame(in);
    in.mouseX = 150;
    in.down = in.pressed = false;
    in.released = true;
    Check(!frame(in), "a press elsewhere then a release on the button is no click");
    in.released = false;
    in.down = in.pressed = true;
    ui.SetInteractive(false);
    canvas.Begin(1);
    ui.Begin(kDt, in);
    ui.SetInteractive(false);
    Check(!ui.Button("b", r, "Button", ButtonKind::Go), "no click while an overlay covers it");
    ui.End();
}

// Drops: DROP_TEXT is ignored, a file drop starts an import with the dropped paths, a drop while one runs waits.
void CheckDrops(SDL_Renderer* renderer) {
    FakeBackend b;
    LauncherUi ui(renderer, b);
    auto frames = [&](int n) {
        for (int i = 0; i < n; ++i) ui.Frame(kDt, metric::kStartWidth, metric::kStartHeight, 1);
    };
    frames(1);
    ui.HandleEvent(DropEvent(SDL_EVENT_DROP_BEGIN));
    ui.HandleEvent(DropEvent(SDL_EVENT_DROP_TEXT, 10, 10, "some text"));
    ui.HandleEvent(DropEvent(SDL_EVENT_DROP_COMPLETE));
    frames(1);
    Check(b.Started().empty(), "dropped text starts nothing");
    Check(ui.CurrentScreen() == LauncherUi::Screen::Home, "dropped text stays home");

    FakeBackend::Script s;
    s.holdOpen = true;
    b.Queue(s);
    ui.HandleEvent(DropEvent(SDL_EVENT_DROP_BEGIN));
    ui.HandleEvent(DropEvent(SDL_EVENT_DROP_POSITION, 300, 200));
    ui.HandleEvent(DropEvent(SDL_EVENT_DROP_FILE, 300, 200, "/a/one.iso"));
    ui.HandleEvent(DropEvent(SDL_EVENT_DROP_FILE, 300, 200, "/a/two.zip"));
    ui.HandleEvent(DropEvent(SDL_EVENT_DROP_COMPLETE));
    frames(1);
    Check(b.Started().size() == 1 && b.Started()[0] == std::vector<std::string>{"/a/one.iso", "/a/two.zip"},
          "a file drop starts one import with both paths");
    Check(ui.CurrentScreen() == LauncherUi::Screen::Checking, "a file drop shows the checking screen");

    ui.HandleEvent(DropEvent(SDL_EVENT_DROP_FILE, 0, 0, "/a/three.dmg"));
    ui.HandleEvent(DropEvent(SDL_EVENT_DROP_COMPLETE));
    frames(1);
    Check(b.Started().size() == 1, "a drop during an import waits");
    b.Queue(FakeBackend::Script());
    b.Release();
    frames(3);
    Check(b.Started().size() == 2 && b.Started()[1] == std::vector<std::string>{"/a/three.dmg"},
          "the waiting drop runs next");
    Check(ui.CurrentScreen() == LauncherUi::Screen::Result, "both done: the result");

    // Play reports the choice once.
    b.FillAll(assets::kEditionUs);
    ui.ShowScreen(LauncherUi::Screen::Play);
    frames(1);
    LaunchChoice choice;
    Check(!ui.TakePlay(choice), "no play before the button");
}

// The all-confirmed home: the two tiles are the drop targets (the Gold one is gone), each Play button starts its
// edition with the Gold rides, and the startup check's notice is shown.
void CheckReady(SDL_Renderer* renderer) {
    FakeBackend b;
    FillEverything(b);
    b.startupNotice = "3 files of the European edition had changed and were removed; drop its disc to restore them.";
    LauncherUi ui(renderer, b);
    auto frames = [&](int n) {
        for (int i = 0; i < n; ++i) ui.Frame(kDt, metric::kStartWidth, metric::kStartHeight, 1);
    };
    frames(1);
    Check(ui.Animating(), "all confirmed: the startup notice shows");
    Rect eu = ui.TileRect(1), us = ui.TileRect(0);
    Check(eu.w > 300 && us.w > 300 && eu.x < us.x, "all confirmed: two large tiles, the European one on the left");
    Check(ui.TileRect(2).w == 0, "all confirmed: no Gold tile");
    Rect euPlay = ui.ReadyPlayRect(1), usPlay = ui.ReadyPlayRect(0);
    Check(euPlay.w > 0 && eu.Contains(euPlay.CentreX(), euPlay.CentreY()), "all confirmed: the European Play is in its tile");
    Check(usPlay.w > 0 && us.Contains(usPlay.CentreX(), usPlay.CentreY()), "all confirmed: the US Play is in its tile");
    std::printf("all confirmed at %dx%d points: European Play centre %.0f,%.0f; US Play centre %.0f,%.0f\n",
                metric::kStartWidth, metric::kStartHeight, euPlay.CentreX(), euPlay.CentreY(), usPlay.CentreX(),
                usPlay.CentreY());

    // a drag over the European tile names it; anywhere else is still the window's drop
    b.Queue(FakeBackend::Script());
    ui.HandleEvent(DropEvent(SDL_EVENT_DROP_BEGIN));
    ui.HandleEvent(DropEvent(SDL_EVENT_DROP_POSITION, eu.CentreX(), eu.CentreY()));
    frames(1);
    ui.HandleEvent(DropEvent(SDL_EVENT_DROP_FILE, eu.CentreX(), eu.CentreY(), "/a/eu.iso"));
    ui.HandleEvent(DropEvent(SDL_EVENT_DROP_COMPLETE));
    frames(3);
    Check(b.Started().size() == 1 && b.Started()[0] == std::vector<std::string>{"/a/eu.iso"},
          "all confirmed: a drop on a tile is imported");
    ui.ShowScreen(LauncherUi::Screen::Home);
    frames(1);

    auto click = [&](const Rect& r) {
        ui.HandleEvent(MotionEvent(r.CentreX(), r.CentreY()));
        frames(1);
        ui.HandleEvent(ButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, r.CentreX(), r.CentreY()));
        frames(1);
        ui.HandleEvent(ButtonEvent(SDL_EVENT_MOUSE_BUTTON_UP, r.CentreX(), r.CentreY()));
        frames(1);
    };
    LaunchChoice choice;
    Check(!ui.TakePlay(choice), "all confirmed: no play before a click");
    click(ui.ReadyPlayRect(1));
    Check(ui.TakePlay(choice) && choice.edition == assets::kEditionEur && choice.gold,
          "all confirmed: the European Play starts the European edition with the Gold rides");
    Check(b.LastChoice() == "eur", "all confirmed: the European choice is kept");
    click(ui.ReadyPlayRect(0));
    Check(ui.TakePlay(choice) && choice.edition == assets::kEditionUs && choice.gold,
          "all confirmed: the US Play starts the US edition with the Gold rides");
    Check(b.LastChoice() == "us", "all confirmed: the US choice is kept");

    // without the Gold rides it is the home with the drop field again
    std::string err;
    b.RemoveEdition(assets::kEditionGold, err);
    frames(1);
    Check(ui.ReadyPlayRect(0).w == 0 && ui.TileRect(2).w > 0, "Gold missing: the three tiles again");
}

// Scrolling: the wheel over the area moves the offset a step a line and stops at both ends, the wheel elsewhere does
// nothing, small steps add up, and content not drawn for a frame starts at its top again. A clip keeps the pointer too:
// a button outside it is not clicked.
void CheckScroll(SDL_Renderer* renderer) {
    Canvas canvas(renderer);
    Text text(canvas);
    Ui ui(canvas, text);
    const Rect area{100, 100, 200, 100};
    auto frame = [&](const Input& in, bool drawn = true) {
        canvas.Begin(1);
        ui.Begin(kDt, in);
        float offset = drawn ? ui.Scroll("s", area, 50, 120) : -1;
        ui.End();
        return offset;
    };
    Input in;
    in.mouseX = in.mouseY = 150;
    Check(frame(in) == 0, "scroll: starts at the top");
    in.wheel = -1;
    Check(frame(in) == 50, "scroll: a line down is a step");
    in.wheel = -5;
    Check(frame(in) == 120, "scroll: stops at the end");
    in.wheel = 1;
    Check(frame(in) == 70, "scroll: a line up");
    in.mouseX = 10;
    in.wheel = -1;
    Check(frame(in) == 70, "scroll: the wheel away from the area does nothing");
    in.mouseX = 150;
    in.wheel = 5;
    Check(frame(in) == 0, "scroll: stops at the top");
    in.wheel = -0.01f;
    for (int i = 0; i < 3; ++i) frame(in);
    Check(frame(in) == 2, "scroll: a touchpad's small steps add up");
    in.wheel = -1;
    frame(in);
    in.wheel = 0;
    frame(in, false);
    Check(frame(in) == 0, "scroll: drawn again after a frame without it, it starts at the top");

    const Rect button{100, 100, 120, 40};
    auto click = [&](const Rect& clip) {
        Input press;
        press.mouseX = 150;
        press.mouseY = 120;
        press.down = press.pressed = true;
        Input release = press;
        release.down = release.pressed = false;
        release.released = true;
        bool clicked = false;
        for (const Input& i : {press, release}) {
            canvas.Begin(1);
            ui.Begin(kDt, i);
            ui.Clip(clip);
            clicked = ui.Button("clipped", button, "Button", ButtonKind::Go) || clicked;
            ui.Unclip();
            ui.End();
        }
        return clicked;
    };
    Check(!click({0, 130, 320, 110}), "scroll: a button under the pointer but outside the clip is not clicked");
    Check(click({0, 110, 320, 130}), "scroll: inside the clip it is");
}

// The Settings box in the smallest window: its last row (Interface size) starts below the box, the wheel over the box
// brings it into view, a click on it there keeps the setting, and the wheel back up (or opening the box again) puts it
// where it was. In a window tall enough for the box nothing scrolls.
void CheckSettingsScroll(SDL_Renderer* renderer) {
    FakeBackend b;
    LauncherUi ui(renderer, b);
    float width = metric::kMinWidth, height = metric::kMinHeight;
    auto frames = [&](int n) {
        for (int i = 0; i < n; ++i) ui.Frame(kDt, width, height, 1);
    };
    ui.ShowSettings(true);
    frames(1);
    const Rect start = ui.InterfaceSizeRect();
    Check(start.w > 0 && start.Bottom() > height - metric::kMargin,
          "settings: in the smallest window the last row starts below the box");
    Check(ui.SettingsScrolls(), "settings: in the smallest window the box scrolls");

    ui.HandleEvent(MotionEvent(10, height * 0.5f));   // beside the box
    ui.HandleEvent(WheelEvent(-10));
    frames(1);
    Check(ui.InterfaceSizeRect().y == start.y, "settings: the wheel beside the box does not scroll it");

    ui.HandleEvent(MotionEvent(width * 0.5f, height * 0.5f));
    ui.HandleEvent(WheelEvent(-10));
    frames(1);
    const Rect end = ui.InterfaceSizeRect();
    Check(end.y < start.y && end.y > metric::kMargin && end.Bottom() <= height - metric::kMargin,
          "settings: scrolled down, the last row is in the box");
    std::printf("settings at %dx%d points: the Interface size switch at y %.0f, scrolled down %.0f\n",
                metric::kMinWidth, metric::kMinHeight, start.y, end.y);

    // a click on its third choice, 70%
    float x = end.Right() - end.w / 6, y = end.CentreY();
    ui.HandleEvent(MotionEvent(x, y));
    frames(1);
    ui.HandleEvent(ButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y));
    frames(1);
    ui.HandleEvent(ButtonEvent(SDL_EVENT_MOUSE_BUTTON_UP, x, y));
    frames(1);
    CheckEq(b.Setting(kInterfaceSizeKey), "70", "settings: a click on the scrolled-in row keeps the setting");

    ui.HandleEvent(WheelEvent(10));
    frames(1);
    Check(ui.InterfaceSizeRect().y == start.y, "settings: the wheel back up puts the row where it was");
    ui.HandleEvent(WheelEvent(-10));
    frames(1);
    ui.ShowSettings(false);
    frames(1);
    ui.ShowSettings(true);
    frames(1);
    Check(ui.InterfaceSizeRect().y == start.y, "settings: opened again, the box starts at its top");

    // Tall windows, the box only just fitting at some heights: no clip and no bar, whatever the rounding.
    width = 1100;
    for (const float tallHeight : {760.0f, 761.0f, 800.0f}) {
        height = tallHeight;
        frames(1);
        const Rect tall = ui.InterfaceSizeRect();
        ui.HandleEvent(MotionEvent(width * 0.5f, height * 0.5f));
        ui.HandleEvent(WheelEvent(-10));
        frames(1);
        Check(tall.Bottom() < height - metric::kMargin && ui.InterfaceSizeRect().y == tall.y,
              "settings: in a tall window the box fits and the wheel moves nothing");
        Check(!ui.SettingsScrolls(), "settings: in a tall window the box does not scroll, so no bar is drawn");
    }
}

bool Overlaps(const Rect& a, const Rect& b) {
    return a.w > 0 && b.w > 0 && a.x < b.Right() && b.x < a.Right() && a.y < b.Bottom() && b.y < a.Bottom();
}

// The launcher's version in the footer's corner, clear of the tiles and their Play buttons at the start size and the
// smallest, on both homes; and the About box's Show logs button, there only when the window has a logs folder.
void CheckVersionAndLogs(SDL_Renderer* renderer) {
    for (bool ready : {false, true}) {
        for (int size = 0; size < 2; ++size) {
            const float w = size == 0 ? metric::kStartWidth : metric::kMinWidth;
            const float h = size == 0 ? metric::kStartHeight : metric::kMinHeight;
            const std::string what = std::string(ready ? "all-confirmed home" : "home") + " at " +
                                     std::to_string(static_cast<int>(w)) + "x" + std::to_string(static_cast<int>(h));
            FakeBackend b;
            if (ready) FillEverything(b);
            LauncherUi ui(renderer, b);
            ui.Frame(kDt, w, h, 1);
            const Rect v = ui.VersionRect();
            Check(v.w > 0 && v.h > 0, what + ": the version is drawn");
            Check(v.x >= 0 && v.Right() <= w && v.Bottom() <= h && v.y > h * 0.75f,
                  what + ": the version is in the bottom corner");
            for (int i = 0; i < 3; ++i) Check(!Overlaps(v, ui.TileRect(i)), what + ": the version is clear of tile " + std::to_string(i));
            for (int i = 0; i < 2; ++i)
                Check(!Overlaps(v, ui.ReadyPlayRect(i)), what + ": the version is clear of Play " + std::to_string(i));
            if (ready) Check(ui.ReadyPlayRect(0).w > 0, what + ": the Play buttons are drawn");
        }
    }
    FakeBackend b;
    LauncherUi ui(renderer, b);
    ui.ShowAbout(true);
    ui.Frame(kDt, metric::kStartWidth, metric::kStartHeight, 1);
    Check(ui.ShowLogsRect().w == 0, "About: no Show logs without a logs folder");
    ui.SetLogsFolder("/Users/player/Library/Application Support/STP-TPW Launcher/logs");
    ui.Frame(kDt, metric::kStartWidth, metric::kStartHeight, 1);
    const Rect logs = ui.ShowLogsRect();
    Check(logs.w > 0 && logs.Bottom() < metric::kStartHeight, "About: Show logs is drawn with a logs folder");
    ui.SetLogsFolder("/Users/player/Library/Application Support/STP-TPW Launcher/logs");
    ui.Frame(kDt, metric::kMinWidth, metric::kMinHeight, 1);
    Check(ui.ShowLogsRect().w > 0 && ui.ShowLogsRect().Bottom() < metric::kMinHeight,
          "About: Show logs fits the smallest window");
    ui.ShowAbout(false);
    ui.Frame(kDt, metric::kStartWidth, metric::kStartHeight, 1);
    Check(ui.ShowLogsRect().w == 0, "About closed: no Show logs");
}

// The launcher's version beside the large title on both homes, at the smallest window, the start size and two large
// ones: drawn to the title's right with a small gap, much smaller than the title, on its line, and clear of the header's
// buttons. And the application's name as the window title and the menu bar give it: the product's name, " v", the
// version from launcher/VERSION.txt.
void CheckTitleVersion(SDL_Renderer* renderer) {
    CheckEq(std::string(kProductNameWithVersion), std::string(kProductName) + " v" + kLauncherVersion,
            "the application's name carries the version");
    CheckEq(std::string(kVersionLabel), std::string("v") + kLauncherVersion, "the version label");
    Check(std::string(kLauncherVersion) != "unknown", "the version comes from launcher/VERSION.txt");
    const float sizes[4][2] = {{metric::kMinWidth, metric::kMinHeight}, {metric::kStartWidth, metric::kStartHeight},
                               {1600, 1000}, {2560, 1440}};
    for (bool ready : {false, true}) {
        for (const auto& size : sizes) {
            const float w = size[0], h = size[1];
            const std::string what = std::string(ready ? "all-confirmed home" : "home") + " at " +
                                     std::to_string(static_cast<int>(w)) + "x" + std::to_string(static_cast<int>(h));
            FakeBackend b;
            if (ready) FillEverything(b);
            LauncherUi ui(renderer, b);
            ui.Frame(kDt, w, h, 1);
            const Rect t = ui.TitleRect(), v = ui.TitleVersionRect(), buttons = ui.HeaderButtonsRect();
            Check(t.w > 0 && v.w > 0 && v.h > 0, what + ": the title and its version are drawn");
            Check(v.x >= t.Right() + 6 && v.x <= t.Right() + 16, what + ": the version is just right of the title");
            Check(v.h < t.h * 0.7f, what + ": the version is much smaller than the title");
            Check(v.y >= t.y && v.Bottom() <= t.Bottom(), what + ": the version is on the title's line");
            Check(buttons.w > 0, what + ": the header's buttons are drawn");
            Check(v.Right() + 10 <= buttons.x, what + ": the version is clear of the header's buttons");
            Check(buttons.Right() <= w, what + ": the header's buttons fit the window");
            std::printf("  %s: title %.0f..%.0f, version %.0f..%.0f (y %.0f..%.0f), buttons %.0f..%.0f\n", what.c_str(),
                        t.x, t.Right(), v.x, v.Right(), v.y, v.Bottom(), buttons.x, buttons.Right());
        }
    }
}

// The real backend over an empty data folder: the European edition counts the US Mac program as one more required
// file from the US Mac CD until a file of the program's size is in place (the program's content is not needed here: the
// importer, too, counts a file of an accepted size as present). The folder is made under the screens folder and
// removed again.
void CheckProgramGate(const std::string& screens) {
    namespace fs = std::filesystem;
    const fs::path pref = fs::path(screens) / "program-gate";
    std::error_code ec;
    fs::remove_all(pref, ec);
    fs::create_directories(pref, ec);
    {
        ImporterBackend backend(pref.string());
        std::string err;
        Check(backend.Open(err), "program gate: the data folder opens: " + err);
        if (backend.importer()) {
            const EditionStatus raw = backend.importer()->Status(assets::kEditionEur);
            EditionStatus eur = backend.Status(assets::kEditionEur);
            Check(!backend.HasProgram(), "program gate: no program in an empty data folder");
            Check(eur.required == raw.required + 1, "program gate: the European edition needs one more file");
            Check(eur.missingFiles[assets::kSourceMacCd] == raw.missingFiles[assets::kSourceMacCd] + 1,
                  "program gate: the missing file is on the US Mac CD");
            Check(!eur.unlocked, "program gate: the European edition is not ready");
            const EditionStatus us = backend.Status(assets::kEditionUs);
            const EditionStatus usRaw = backend.importer()->Status(assets::kEditionUs);
            Check(us.required == usRaw.required, "program gate: the US edition is counted as the importer counts it");
            const std::string program = backend.ProgramPath();
            Check(program.find("/assets/us/") != std::string::npos, "program gate: the program is in the US folder");
            uint64_t size = 0;
            for (size_t i = 0; i < assets::ManifestRecordCount(); ++i)
                if (assets::ManifestRecords()[i].group == assets::kGroupProgram) size = assets::RecordHash(assets::ManifestRecords()[i], 0).size;
            fs::create_directories(fs::path(program).parent_path(), ec);
            {
                std::ofstream out(program, std::ios::binary);
                out.seekp(static_cast<std::streamoff>(size) - 1);
                out.put('\0');
            }
            Check(backend.HasProgram(), "program gate: a file of the program's size counts");
            eur = backend.Status(assets::kEditionEur);
            Check(eur.required == raw.required, "program gate: with the program, nothing more is needed");
        }
    }
    fs::remove_all(pref, ec);
}

}  // namespace

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "";
    if (dir.empty()) {
        const char* env = std::getenv("TPW_UI_SCREENS_DIR");
        dir = env && *env ? env : TPW_UI_SCREENS_DEFAULT;
    }
    if (!SDL_CreateDirectory(dir.c_str())) {
        std::fprintf(stderr, "cannot create %s: %s\n", dir.c_str(), SDL_GetError());
        return 1;
    }

    CheckTexts();
    CheckProgramGate(dir);

    SDL_Surface* scratch = SDL_CreateSurface(320, 240, SDL_PIXELFORMAT_XRGB8888);
    SDL_Renderer* renderer = scratch ? SDL_CreateSoftwareRenderer(scratch) : nullptr;
    Check(renderer != nullptr, std::string("software renderer: ") + SDL_GetError());
    if (renderer) {
        CheckButton(renderer);
        CheckDrops(renderer);
        CheckReady(renderer);
        CheckScroll(renderer);
        CheckSettingsScroll(renderer);
        CheckVersionAndLogs(renderer);
        CheckTitleVersion(renderer);
        SDL_DestroyRenderer(renderer);
    }
    SDL_DestroySurface(scratch);

    for (const Shot& shot : Shots())
        for (int scale : {1, 2}) Render(dir, shot, scale);

    for (const std::string& path : g_written) std::printf("wrote %s\n", path.c_str());
    std::printf("launcher_ui: %d checks, %d failed, %zu pictures\n", g_checks, g_failures, g_written.size());
    SDL_Quit();
    return g_failures ? 1 : 0;
}
