// window.h: the launcher's own window: about 800 x 560 points, resizable, sharp on high-density screens, running the
// screens of launcher_ui.h until the player presses Play or closes it. Closed, it is gone when this returns; after Play it
// stays on the Starting screen until the game's window is shown (CloseStartingWindow). launcher/main.cpp opens it when
// no --game is given.
#pragma once
#include <string>
#include <vector>

#include "backend.h"
#include "launcher_ui.h"

namespace tpw::ui {

enum class WindowOutcome {
    Play,    // `choice` says what to start
    Quit,    // the window was closed
    Failed,  // `err` says why
};

// Where the window opens: centred on an SDL display (0: SDL's choice), and in front or, for a test run on a screen
// someone is working on, behind the other windows without taking the focus (the launcher's --display and --background).
struct WindowPlacement {
    SDL_DisplayID display = 0;
    bool background = false;
    // A test run (the launcher's --frames, --dump-frames, --dump-every and --dump-limit, as the game's window has
    // them): quit with Quit after this many drawn frames (0: never), and save the drawn frames as frame-NNNN.bmp in
    // dumpDir (empty: none), each one that differs from the one before it, every dumpEvery'th of those, at most
    // dumpLimit pictures.
    int maxFrames = 0;
    std::string dumpDir;
    int dumpEvery = 1;
    int dumpLimit = 200;
    // Scripted left clicks in this window (the launcher's --window-click X,Y@F): at point (x, y) of the window, pressed
    // before drawn frame F (from 1) and released before frame F + 1.
    struct Click {
        int frame;
        float x, y;
    };
    std::vector<Click> clicks;
    // The launcher's log (launcher/main.cpp: stderr and the log file, platform/log_file.h) for the window's own report
    // lines (a scripted click, a dumped frame, Play); null: stderr only. The line comes without a newline.
    void (*log)(const char* line) = nullptr;
    // Called once when this window starts SDL's video itself (the log file's display line); null: nothing.
    void (*videoStarted)() = nullptr;
    // Called once the window is on screen (macOS: the application menu is named after the launcher from then on).
    void (*windowShown)() = nullptr;
    // The folder of the log files, which the About box's Show logs opens (empty: no such button).
    std::string logsFolder;
};

// Initialises SDL's video subsystem if it is not yet, and leaves it initialised. On Play the window is not closed: it
// stays on the Starting screen (the screen it was on, dimmed, with a progress bar) until CloseStartingWindow.
WindowOutcome RunLauncherWindow(ImportBackend& backend, LaunchChoice& choice, std::string& err,
                                const WindowPlacement& placement = {});

// After Play: draws the Starting screen once with the bar at `fraction` (-1: moving without a number), after taking every
// waiting event off SDL's queue (the game's window is not shown yet; only the launcher window's close and quit are acted
// on). False when the player closed the window or quit. True and nothing drawn when there is no such window (--play).
// The OpenGL context current on the thread stays current.
bool DrawStartingWindow(float fraction);
// Closes the window RunLauncherWindow left open on Play (nothing when there is none).
void CloseStartingWindow();

}  // namespace tpw::ui
