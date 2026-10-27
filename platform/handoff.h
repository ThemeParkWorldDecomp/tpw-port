// handoff.h: the hand-off from the launcher's window to the game's. The launcher's window stays open after Play and
// shows a progress bar until the game's window appears, then it closes; the work it waits for is counted in bytes read.
//
// The bar's amount is bytes: the preload's (bytes of known files, an exact total) and the game's own start (the bytes the
// game reads through the file layer, platform/file_handles.cpp, from Play until its window is shown, against the total
// the previous start of that edition read, which the launcher keeps in launcher.ini). With no total from an earlier run
// the bar moves without a number. The game's code is not touched: the file layer counts its reads, and the window's
// ShowWindow (platform/window.cpp) says when the game's window is there.
//
// The preload starts when the launcher's window opens (not with --play or --no-preload), on a thread of its own at the
// lowest priority: the game's data tables from the program (LoadGameTables, which writes only the tables' arrays, which
// nothing reads before the game starts), then the first files of the edition last played, read in 1 MB pieces and thrown
// away so that the system's file cache holds them when the game asks. Play waits for it behind the same bar.
//
// After the game's window is shown the file layer's reads on the main thread still let the system's events in, at most
// every 100 ms, so that the game's window answers while it loads the lobby (a long stretch of reads that asks for no
// events): the events wait in SDL's queue for the game's next pass.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct SDL_Window;

namespace tpw {

// ---- The bar's amount

// done of total as a fraction for the bar: -1 (the bar moves without a number) when total is 0; else done / total, at
// most 0.99 until the game's window is shown (a start that reads more than last time waits at 99%), 1 once it is.
float StartupFraction(uint64_t done, uint64_t total, bool shown);

// ---- The start, counted (main thread unless said otherwise)

// Play: the start's clock and the game's byte count start from 0, and `expectedGameBytes` (0: unknown) is what the game
// read the last time. The bar's total is the preload's bytes and that.
void StartupBegin(uint64_t expectedGameBytes);
// One line "startup: <stage> +<ms>ms" in the platform log, with the bytes read so far and the bar's fraction then.
void StartupStamp(const char* stage);
// `bytes` were read through the file layer. Any thread; on the thread StartupBegin ran on it is also a moment to
// redraw the launcher's window (OnStartupProgress).
void CountStartupRead(uint64_t bytes);
// A moment to keep the launcher's window drawn: at most every 40 ms it calls the pump with the bar's fraction (also
// keeps the longest stretch between two calls, for the log). Does nothing before StartupBegin or after the game's
// window is shown.
void OnStartupProgress();
// The bar's fraction now (StartupFraction of the bytes so far).
float StartupProgress();
// The bytes the game has read since StartupBegin.
uint64_t StartupGameBytes();
// True once StartupBegin has run.
bool StartupBegun();

// What draws the launcher's window with the bar at a fraction (-1: moving without a number): the launcher's, set before
// StartupBegin; none with --play.
using StartupPump = void (*)(float fraction);
void SetStartupPump(StartupPump pump);
// What runs once when the game's window is first shown, with the bytes the game read until then: the launcher keeps
// the count and closes its window.
using GameWindowShownHook = void (*)(uint64_t gameBytes);
void SetGameWindowShownHook(GameWindowShownHook hook);
// The game's window was shown (platform/window.cpp ShowWindow). The first call logs the stamp and runs the hook; the
// later ones do nothing.
void OnGameWindowShown();
// What runs on the game's window once it is made, before it is shown: the launcher gives it the game's icon (icon.png)
// and, on Windows, its taskbar identity (launcher/game_app.h TagGameWindow). None: the window as SDL makes it.
using GameWindowMadeHook = void (*)(SDL_Window* window);
void SetGameWindowMadeHook(GameWindowMadeHook hook);
// The game's window was made (platform/window.cpp): runs the hook.
void OnGameWindowMade(SDL_Window* window);

// ---- The preload

struct PreloadPlan {
    std::filesystem::path program;              // the program the tables come from; empty: no tables
    bool (*loadTables)(const char* program, std::string* why) = nullptr;   // LoadGameTables (game_tables.h)
    std::vector<std::filesystem::path> files;   // read and thrown away, in this order (the ones not there are left out)
};
// The bytes a plan reads (the program's size, if it has one, and the files').
uint64_t PreloadPlanBytes(const PreloadPlan& plan);
// Starts the preload on its own thread. Once only: a second call does nothing.
void StartPreload(const PreloadPlan& plan);
// The files still to read are left alone (the tables, if they are being read, are finished).
void CancelPreload();
// Waits for the preload to end, calling OnStartupProgress about every 20 ms while it does, then joins it. Returns at
// once when there was none. (The thread runs at the user-initiated class throughout, so a busy computer does not starve
// it.)
void WaitPreload();
uint64_t PreloadBytesDone();    // any thread
uint64_t PreloadBytesTotal();   // 0 when there is no preload

// Whether the preload read the game's tables from `program` (after waiting for it, 1.5 s at most: past that the files
// are left and the tables are read by the thread only when it has begun them, logging why): then *ok is what
// LoadGameTables answered and *why its reason. False when it did not, and the tables are still to be read.
bool PreloadedTables(const std::filesystem::path& program, bool* ok, std::string* why);

}  // namespace tpw
