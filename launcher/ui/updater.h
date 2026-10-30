// updater.h: the update check and the Update button (port_README.md, "Updates"; notes/updater-and-offline.md in the
// monorepo for the why).
//
// The check: at every launcher start, in the background, never delaying the window. It compares the
// newest release (net::LatestRelease) with this launcher's version, on every install: releases only (the maintainer's decision,
// 2026-10-25). A dev build (0.0.4-dev) newer than the newest release hears of nothing; one older than it, or 0.0.4-dev
// once 0.0.4 is out, is offered the release. Dev builds come only from running install.sh --dev. No answer (offline,
// GitHub down, a rate limit, junk) says nothing. What it found is kept in launcher.ini, so the banner shows at once on
// later launches, offline too, until an answer says this launcher is current. (Once a day before 0.2.1: a release went
// unseen for up to a day.)
//
// The Update button: an install made by install.sh (install.ps1 on Windows) is a folder DIR with source/ (the clone),
// build/ (this launcher), build-next/ and channel.txt. Update runs `install.sh --update --stage --dir DIR` (never
// --dev, whatever channel.txt says, so the script goes to the newest release), which builds it into build-next and
// changes nothing else, and shows its STEP lines; its last line is RESULT ok VERSION or RESULT fail REASON. Then
// "Restart to update" starts `install.sh --swap-and-launch --wait-pid PID --dir DIR` on its own and quits: the script
// waits for this process to end, switches build-next in and starts the new launcher. Anything else (a prebuilt
// download, a build of one's own) gets the release page in the browser instead.
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "backend.h"
#include "net.h"
#include "process.h"

namespace tpw::ui {

// launcher.ini's key for the Settings box's "Check for updates": absent or "1" on, "0" off. ON BY DEFAULT, unlike every
// other setting of the launcher (port_CONTRIBUTING.md: settings are off by default): The maintainer's explicit exception, so that
// players hear of fixes. It asks GitHub one question per launch and sends nothing about the player.
inline constexpr const char* kCheckUpdatesKey = "check_updates";
// When the last check got an answer, in seconds since 1970 (a check that got none leaves it). Kept for the log and
// for launchers before 0.2.1, which checked once a day by it.
inline constexpr const char* kUpdateLastCheckKey = "update_last_check";
// What the last answer found: a newer version ("0.0.4"), or "none".
inline constexpr const char* kUpdateFoundKey = "update_found";

namespace update {

// "1.2.3" or "1.2.3-dev" (launcher/VERSION.txt's forms).
struct Version {
    long major = 0, minor = 0, patch = 0;
    bool dev = false;
};
bool ParseVersion(const std::string& text, Version& out);
// True when `candidate` is a later version than `current`; 0.0.4 is later than 0.0.4-dev (main before the release).
bool Newer(const std::string& candidate, const std::string& current);

// An install made by install.sh: `exeDir` is DIR/build, the folder of the running launcher.
struct Install {
    std::string dir;   // empty: not such an install
    bool Found() const { return !dir.empty(); }
};
Install FindInstall(const std::string& exeDir);
// The update script's two runs (process.h argv). Both run in DIR, never DIR/build: on Windows a process whose current
// folder is in build\ keeps the swap from renaming it.
std::vector<std::string> StageCommand(const Install& install);
std::vector<std::string> SwapCommand(const Install& install, unsigned long pid);
// "STEP 2/4 Downloading the source": true with the numbers and the words.
bool ParseStep(const std::string& line, int& step, int& steps, std::string& words);
// "RESULT ok 0.0.4" or "RESULT fail Couldn't reach GitHub": true with ok and the rest.
bool ParseResult(const std::string& line, bool& ok, std::string& rest);

// What this launcher is, for the check.
struct Current {
    std::string version;   // kLauncherVersion
    Install install;
};
Current DescribeCurrent(const std::string& version, const std::string& exeDir);

// The questions the check asks (net.h), replaceable for the tests.
struct Questions {
    std::function<net::Release(const net::Limits&)> latestRelease = [](const net::Limits& l) { return net::LatestRelease(l); };
};

// One check, blocking (the Updater runs it on its thread; --check-updates in the open). `found`: the newer version,
// empty when up to date. `why`: when not answered, or when there was nothing to ask.
struct Answer {
    bool answered = false;
    std::string found;
    std::string why;
};
Answer Check(const Current& current, const Questions& questions, const net::Limits& limits = {});

}  // namespace update

// What the Updater asks and runs, replaceable for the tests (outside the class: a default argument needs it complete).
struct UpdaterHooks {
    update::Questions questions;
    std::function<proc::Result(const proc::Options&)> run = [](const proc::Options& o) { return proc::Run(o); };
    // argv, the working folder, the error
    std::function<bool(const std::vector<std::string>&, const std::string&, std::string&)> spawn =
        [](const std::vector<std::string>& argv, const std::string& dir, std::string& err) {
            return proc::Spawn(argv, err, dir);
        };
    std::function<bool(const std::string&)> exists;   // a file is there (default: std::filesystem)
};

class Updater {
public:
    enum class Phase {
        Idle,       // nothing running (a check may be under way unseen: Checking)
        Checking,   // the background check
        Probing,    // Update pressed: asking again first, so that no internet says so at once
        Staging,    // the script builds the new version (StepText)
        Staged,     // built: "Restart to update"
        Failed,     // the script failed (FailReason); nothing was changed
    };
    using Hooks = UpdaterHooks;

    // `settings`: launcher.ini (only touched from the UI thread). `log`: the launcher's log, a line without its newline.
    Updater(ImportBackend& settings, update::Current current, void (*log)(const char*) = nullptr, Hooks hooks = {});
    // Stops a check or an update script still running and waits for it (a few hundred milliseconds at most).
    ~Updater();
    Updater(const Updater&) = delete;
    Updater& operator=(const Updater&) = delete;

    // On launcher start: the check when it is on and due. Returns at once.
    void StartupCheck(long long now);
    // Every frame, on the UI thread: takes what the background thread found and keeps it in launcher.ini.
    void Poll();

    bool Enabled() const { return enabled_; }
    // The Settings box's switch (which keeps kCheckUpdatesKey itself): off hides the banner; on checks from the next
    // launch.
    void SetEnabled(bool on);

    // The banner: shown while there is something to say.
    bool Visible() const;
    Phase CurrentPhase() const { return phase_; }
    const std::string& Found() const { return found_; }
    // True when this launch asked and got no answer: Update is greyed out ("No internet connection").
    bool Offline() const { return offline_; }
    // True for an install made by install.sh: Update updates in place; otherwise it opens the release page.
    bool ScriptInstall() const { return current_.install.Found(); }
    std::string WhatsNewUrl() const;
    // Staging: "Downloading the source (step 2 of 4)".
    std::string StepText() const;
    const std::string& FailReason() const { return failReason_; }
    const std::string& StagedVersion() const { return stagedVersion_; }
    // True while the banner moves (Probing, Staging).
    bool Moving() const { return phase_ == Phase::Probing || phase_ == Phase::Staging; }

    void Update();    // the Update button (a script install)
    void Restart();   // "Restart to update": starts the switch and asks the window to close (TakeQuit)
    void Dismiss();   // Failed: back to the banner
    bool TakeQuit();
    // A line for the window's notice ("Already up to date"), once.
    std::string TakeNotice();

private:
    void Log(const std::string& line);
    void Begin(Phase phase, std::function<void()> job);
    void Remember(const std::string& found);
    bool Usable(const std::string& found) const;

    ImportBackend& settings_;
    update::Current current_;
    void (*log_)(const char*);
    Hooks hooks_;

    bool enabled_ = true;
    std::string found_;
    bool offline_ = false;
    Phase phase_ = Phase::Idle;
    std::string failReason_, stagedVersion_, notice_;
    bool quit_ = false;
    long long checkTime_ = 0;

    // Shared with the background thread, under mutex_.
    mutable std::mutex mutex_;
    std::thread worker_;
    std::atomic<bool> cancel_{false};
    bool done_ = false;
    update::Answer answer_;
    bool staged_ = false, alreadyCurrent_ = false;
    std::string stageVersion_, stageFailure_;
    int step_ = 0, steps_ = 0;
    std::string stepWords_;
    std::vector<std::string> lines_;
};

}  // namespace tpw::ui
