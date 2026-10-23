// window.cpp: the launcher's own window (window.h).
#include "window.h"

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

#include "theme.h"

namespace tpw::ui {

namespace {

// WindowPlacement::log of the window running (null: stderr).
void (*gLog)(const char* line) = nullptr;

// One report line of the window, printf style, to WindowPlacement::log or stderr.
__attribute__((format(printf, 1, 2))) void Report(const char* format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    if (gLog != nullptr) gLog(text);
    else std::fprintf(stderr, "%s\n", text);
}

// The frame just drawn (before it is presented), saved as frame-NNNN.bmp when it differs from the one before it, as the
// game's window does (platform/window.cpp DumpFrame): every `every`'th of the differing frames, at most `limit` pictures.
class FrameDumper {
public:
    FrameDumper(const WindowPlacement& placement)
        : dir_(placement.dumpDir), every_(std::max(placement.dumpEvery, 1)), limit_(std::max(placement.dumpLimit, 0)) {}

    void Dump(SDL_Renderer* renderer) {
        if (dir_.empty() || count_ >= limit_) return;
        SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr);
        if (!shot) return;
        SDL_Surface* rgb = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGB24);
        SDL_DestroySurface(shot);
        if (!rgb) return;
        const size_t rowBytes = static_cast<size_t>(rgb->w) * 3;
        std::vector<uint8_t> bytes(rowBytes * static_cast<size_t>(rgb->h));
        for (int row = 0; row < rgb->h; ++row)
            std::memcpy(&bytes[rowBytes * static_cast<size_t>(row)], static_cast<const uint8_t*>(rgb->pixels) + static_cast<size_t>(row) * rgb->pitch, rowBytes);
        if (bytes != last_ && seen_++ % every_ == 0) {
            char name[32];
            // (launcher-: the game's own frames, dumped to the same folder after Play, are frame-NNNN.bmp)
            std::snprintf(name, sizeof name, "launcher-frame-%04d.bmp", count_++);
            std::error_code ec;
            std::filesystem::create_directories(dir_, ec);
            if (SDL_SaveBMP(rgb, (std::filesystem::path(dir_) / name).string().c_str()))
                Report("launcher: dumped %s (frame %d)", name, frames_);
            else
                Report("launcher: could not save %s: %s", name, SDL_GetError());
        }
        last_ = std::move(bytes);
        SDL_DestroySurface(rgb);
    }
    void NextFrame() { ++frames_; }
    // From now on every differing frame (the Starting screen's: there are few, and they are what the player sees).
    void EveryFrame() { every_ = 1; }

private:
    std::string dir_;
    int every_, limit_;
    int count_ = 0, seen_ = 0, frames_ = 0;
    std::vector<uint8_t> last_;
};

// The window left open on Play, for the Starting screen (DrawStartingWindow, CloseStartingWindow).
struct Kept {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* behind = nullptr;   // the last frame before Play, under the Starting box
    std::unique_ptr<LauncherUi> ui;
    std::unique_ptr<FrameDumper> dumper;
    uint64_t last = 0;
};
Kept* gKept = nullptr;

// The frame drawn last (not yet presented), as a texture.
SDL_Texture* TakePicture(SDL_Renderer* renderer) {
    SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr);
    if (!shot) return nullptr;
    SDL_Texture* picture = SDL_CreateTextureFromSurface(renderer, shot);
    SDL_DestroySurface(shot);
    return picture;
}

// The window an event is about (0: none, such as quit).
SDL_WindowID EventWindow(const SDL_Event& e) {
    if (e.type >= SDL_EVENT_WINDOW_FIRST && e.type <= SDL_EVENT_WINDOW_LAST) return e.window.windowID;
    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: return e.key.windowID;
        case SDL_EVENT_TEXT_INPUT: return e.text.windowID;
        case SDL_EVENT_MOUSE_MOTION: return e.motion.windowID;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: return e.button.windowID;
        case SDL_EVENT_MOUSE_WHEEL: return e.wheel.windowID;
        case SDL_EVENT_DROP_BEGIN:
        case SDL_EVENT_DROP_FILE:
        case SDL_EVENT_DROP_TEXT:
        case SDL_EVENT_DROP_COMPLETE:
        case SDL_EVENT_DROP_POSITION: return e.drop.windowID;
        default: return 0;
    }
}

// Takes every waiting event off the queue but the ones about another window (the game's, made but not yet shown):
// those go back, in their order. True when one taken was a quit or the launcher's window asked to close.
bool DrainLauncherEvents(SDL_WindowID launcher) {
    SDL_PumpEvents();
    std::vector<SDL_Event> events(64), others;
    bool closed = false;
    for (int got; (got = SDL_PeepEvents(events.data(), static_cast<int>(events.size()), SDL_GETEVENT, SDL_EVENT_FIRST,
                                        SDL_EVENT_LAST)) > 0;) {
        for (int i = 0; i < got; ++i) {
            const SDL_Event& e = events[static_cast<size_t>(i)];
            const SDL_WindowID to = EventWindow(e);
            if (to != 0 && to != launcher) others.push_back(e);
            else if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && to == launcher))
                closed = true;
        }
    }
    if (!others.empty()) SDL_PeepEvents(others.data(), static_cast<int>(others.size()), SDL_ADDEVENT, 0, 0);
    return closed;
}

// The scripted clicks (WindowPlacement::clicks) due before drawn frame `frame`, as the pointer's events.
void PushClicks(SDL_Window* window, const WindowPlacement& placement, int frame) {
    for (const WindowPlacement::Click& click : placement.clicks) {
        if (frame != click.frame && frame != click.frame + 1) continue;
        SDL_Event e{};
        if (frame == click.frame) {
            e.type = SDL_EVENT_MOUSE_MOTION;
            e.motion.windowID = SDL_GetWindowID(window);
            e.motion.x = click.x;
            e.motion.y = click.y;
            SDL_PushEvent(&e);
            e = SDL_Event{};
        }
        e.type = frame == click.frame ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.windowID = SDL_GetWindowID(window);
        e.button.button = SDL_BUTTON_LEFT;
        e.button.down = frame == click.frame;
        e.button.clicks = 1;
        e.button.x = click.x;
        e.button.y = click.y;
        SDL_PushEvent(&e);
        Report("launcher: scripted click %s at %.0f,%.0f (frame %d)", e.button.down ? "down" : "up", click.x, click.y,
               frame);
    }
}

}  // namespace

WindowOutcome RunLauncherWindow(ImportBackend& backend, LaunchChoice& choice, std::string& err,
                                const WindowPlacement& placement) {
    gLog = placement.log;
    if (placement.background) {
        // Shown without becoming the key window (as the game's own window does with --background).
        SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
        SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, "0");
    }
    if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO)) {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            err = std::string("cannot start SDL video: ") + SDL_GetError();
            return WindowOutcome::Failed;
        }
        if (placement.videoStarted != nullptr) placement.videoStarted();
    }
    // Made hidden, placed, then shown, so it never appears on another screen first.
    SDL_Window* window = SDL_CreateWindow(kProductNameWithVersion, metric::kStartWidth, metric::kStartHeight,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    if (!window) {
        err = std::string("cannot open the launcher window: ") + SDL_GetError();
        return WindowOutcome::Failed;
    }
    SDL_SetWindowMinimumSize(window, metric::kMinWidth, metric::kMinHeight);
    if (placement.display != 0)
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(placement.display),
                              SDL_WINDOWPOS_CENTERED_DISPLAY(placement.display));
    SDL_ShowWindow(window);
    if (placement.windowShown != nullptr) placement.windowShown();
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        err = std::string("cannot draw in the launcher window: ") + SDL_GetError();
        SDL_DestroyWindow(window);
        return WindowOutcome::Failed;
    }
    SDL_SetRenderVSync(renderer, 1);

    WindowOutcome outcome = WindowOutcome::Quit;
    std::unique_ptr<LauncherUi> kept;
    std::unique_ptr<FrameDumper> keptDumper;
    SDL_Texture* behind = nullptr;
    uint64_t last = SDL_GetTicksNS();
    {
        auto owned = std::make_unique<LauncherUi>(renderer, backend);
        LauncherUi& ui = *owned;
        ui.SetWindow(window);
        ui.SetLogsFolder(placement.logsFolder);
        if (!ui.Ok()) {
            err = ui.Error();
            outcome = WindowOutcome::Failed;
        }
        bool running = ui.Ok();
        auto dumperOwned = std::make_unique<FrameDumper>(placement);
        FrameDumper& dumper = *dumperOwned;
        int drawn = 0;
        while (running) {
            PushClicks(window, placement, drawn + 1);
            // Rest while nothing moves; wake for events (file dialogs push one when they close).
            SDL_Event e;
            // (a counted test run does not wait long: its frames are what it is after)
            bool got = ui.Animating() ? SDL_PollEvent(&e) : SDL_WaitEventTimeout(&e, placement.maxFrames > 0 ? 16 : 250);
            while (got) {
                if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
                ui.HandleEvent(e);
                got = SDL_PollEvent(&e);
            }
            if (!running) break;
            uint64_t now = SDL_GetTicksNS();
            float dt = static_cast<float>(now - last) / 1e9f;
            last = now;
            int w = 0, h = 0;
            SDL_GetWindowSize(window, &w, &h);
            float density = SDL_GetWindowPixelDensity(window);
            ui.Frame(std::min(dt, 0.1f), static_cast<float>(w), static_cast<float>(h), density > 0 ? density : 1);
            dumper.NextFrame();
            dumper.Dump(renderer);
            const bool play = ui.TakePlay(choice);
            // Play: this frame, before it is presented, is what the Starting screen dims (its backend is gone by then).
            if (play) behind = TakePicture(renderer);
            SDL_RenderPresent(renderer);
            ++drawn;
            if (play) {
                outcome = WindowOutcome::Play;
                running = false;
            } else if (placement.maxFrames > 0 && drawn >= placement.maxFrames) {
                running = false;   // --frames N: the window run ends by itself, exit 0 (Quit)
            }
        }
        // An import still running is cancelled and joined by the importer when its owner goes.
        if (backend.Running()) {
            backend.Cancel();
            backend.Wait();
        }
        if (outcome == WindowOutcome::Play) {
            kept = std::move(owned);
            keptDumper = std::move(dumperOwned);
        }
    }
    if (outcome == WindowOutcome::Play) {
        // The window stays, on the Starting screen, until the game's window is shown (CloseStartingWindow). It is drawn
        // between the game's file reads from now on, so it never waits for the display.
        kept->BeginStarting(choice.edition, behind);
        keptDumper->EveryFrame();
        SDL_SetRenderVSync(renderer, 0);
        delete gKept;
        gKept = new Kept{window, renderer, behind, std::move(kept), std::move(keptDumper), last};
        Report("launcher: Play; the window stays open on the Starting screen (renderer %s)", SDL_GetRendererName(renderer));
        return outcome;
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    return outcome;
}

bool DrawStartingWindow(float fraction) {
    if (!gKept) return true;
    // The game's OpenGL context (made with its window) is current on this thread: it stays so.
    SDL_Window* glWindow = SDL_GL_GetCurrentWindow();
    SDL_GLContext glContext = SDL_GL_GetCurrentContext();
    const bool closed = DrainLauncherEvents(SDL_GetWindowID(gKept->window));
    if (!closed) {
        const uint64_t now = SDL_GetTicksNS();
        const float dt = static_cast<float>(now - gKept->last) / 1e9f;
        gKept->last = now;
        int w = 0, h = 0;
        SDL_GetWindowSize(gKept->window, &w, &h);
        const float density = SDL_GetWindowPixelDensity(gKept->window);
        gKept->ui->SetStartingProgress(fraction);
        gKept->ui->FrameStarting(std::min(dt, 0.1f), static_cast<float>(w), static_cast<float>(h),
                                 density > 0 ? density : 1);
        gKept->dumper->NextFrame();
        gKept->dumper->Dump(gKept->renderer);
        SDL_RenderPresent(gKept->renderer);
    }
    if (glContext != nullptr) SDL_GL_MakeCurrent(glWindow, glContext);
    return !closed;
}

void CloseStartingWindow() {
    if (!gKept) return;
    SDL_Window* glWindow = SDL_GL_GetCurrentWindow();
    SDL_GLContext glContext = SDL_GL_GetCurrentContext();
    const SDL_WindowID id = SDL_GetWindowID(gKept->window);
    DrainLauncherEvents(id);
    gKept->ui.reset();
    if (gKept->behind) SDL_DestroyTexture(gKept->behind);
    SDL_DestroyRenderer(gKept->renderer);
    SDL_DestroyWindow(gKept->window);
    delete gKept;
    gKept = nullptr;
    // What the window said as it went (its focus going, say) is not the game's to hear.
    DrainLauncherEvents(id);
    if (glContext != nullptr) SDL_GL_MakeCurrent(glWindow, glContext);
}

}  // namespace tpw::ui
