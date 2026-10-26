// process.h: starting another program from the launcher: run it and read what it prints (with a time limit, a size cap
// and a cancel flag), or start it and let it go. On SDL's process functions (SDL_CreateProcessWithProperties), as
// main.cpp's ReopenLauncher. The update check's curl (launcher/net/net.cpp) and the update script (updater.cpp) both run
// through here. Nothing in this file touches the network.
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace tpw::proc {

struct Options {
    std::vector<std::string> argv;   // argv[0] is the program: a path, or a name looked up on PATH
    int timeoutSeconds = 0;          // 0: no limit; after it the program is stopped (Stop below)
    size_t maxOutputBytes = 0;       // 0: no cap; past it the program is killed and Result::tooMuch is set
    bool mergeStderr = false;        // stderr joins the output (otherwise it is thrown away)
    const std::atomic<bool>* cancel = nullptr;   // set to true by another thread: the program is stopped
    std::function<void(const std::string& line)> onLine;   // each output line, as it arrives (without its newline)
    std::string workingDir;          // the program's current folder; empty: this process's (see Spawn)
};

struct Result {
    bool started = false;    // false: the program could not be started (not there, not allowed)
    int exitCode = -1;       // the program's own, when it ended by itself
    bool timedOut = false, cancelled = false, tooMuch = false;
    std::string output;      // everything it printed (up to the cap)
    bool Ok() const { return started && exitCode == 0 && !timedOut && !cancelled && !tooMuch; }
};

// Runs the program until it ends, and waits for it. Never throws. A program is stopped politely first (SIGTERM, so a
// script's trap can say why and stop its own children; Windows has only TerminateProcess) and killed 3 seconds later.
// On macOS and Linux only the program itself is stopped (the update script's trap stops its own build); on Windows all
// it started is ended with it, and whatever is left when Run returns or this process ends.
Result Run(const Options& options);

// Starts the program with no window, with its input and output on the null device (open, not closed: a file it opens
// then cannot land on 1 or 2), detached so that it outlives this process, and does not wait for it.
// False (with `err`) when it could not be started.
//
// `workingDir` (and Options::workingDir): SDL's process functions have no setting for it, so this process switches its
// own current folder to it for the moment the program is created (which inherits it) and back, under a lock. The
// launcher names its files by full paths, so nothing else notices.
bool Spawn(const std::vector<std::string>& argv, std::string& err, const std::string& workingDir = {});

// This process's id (the update script waits for it to end).
unsigned long CurrentId();

}  // namespace tpw::proc
