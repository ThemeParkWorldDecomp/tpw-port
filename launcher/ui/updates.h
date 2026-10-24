// updates.h: the About box's Check for updates and Update. A launcher built in a clone of tpw-port (the README's build)
// finds that clone, asks git whether GitHub has newer commits, and on Update starts the clone's update script
// (update.cmd on Windows, update.command on a Mac) in a terminal window of its own, which pulls, builds and opens the
// launcher again. Nothing goes over the network until the player clicks Check for updates.
#pragma once
#include <atomic>
#include <string>
#include <vector>

namespace tpw::ui {

// What a check found, and its log lines (each git step and its exit code), which the UI writes to the launcher's log.
struct UpdateCheck {
    enum class Kind {
        UpToDate,       // GitHub has nothing newer
        Behind,         // `behind` new commits
        NoGit,          // git is not on the PATH
        NoBranch,       // a detached HEAD, or a branch without an upstream
        FetchFailed,    // `detail`: git's first error line
        LocalChanges,   // tracked files differ from the commit (a pull could not fast-forward cleanly)
        Failed,         // `detail`: another git step failed or ran out of time
    };
    Kind kind = Kind::UpToDate;
    int behind = 0;
    std::string detail;
    std::vector<std::string> log;
};

// The clone the launcher runs from: `exeDir` (the launcher's folder, SDL_GetBasePath) or one of the 3 folders above it,
// the first with a .git (a folder, or a file in a worktree), a tpw-decomp folder and this system's update script. Empty
// when there is none, as for a launcher copied out of its clone.
std::string FindUpdateClone(const std::string& exeDir);

// The git steps against `clone`: git found, no local changes, an upstream, a fetch, then the commits behind it. Slow (it
// fetches), so it runs on a worker; each step is noted in the answer's log. Answers early with Failed when `cancel` is
// set (the launcher is closing).
UpdateCheck CheckForUpdates(const std::string& clone, const std::atomic<bool>& cancel);

// Starts `clone`'s update script in a terminal window of its own, which outlives the launcher (on Windows it is given
// the launcher's process id and waits for it to close, as the running .exe cannot be replaced). False with `err` when
// it could not be started.
bool StartUpdate(const std::string& clone, std::string& err);

// The sentence the About box shows for a check that cannot lead to an update (empty for UpToDate and Behind).
std::string UpdateProblem(const UpdateCheck& check);

}  // namespace tpw::ui
