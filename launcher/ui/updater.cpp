// updater.cpp: the update check and the Update button (updater.h).
#include "updater.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

namespace tpw::ui {

namespace update {

namespace {
bool Number(const std::string& s, long& out) {
    if (s.empty() || s.size() > 6) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    out = std::atol(s.c_str());
    return true;
}

std::string FirstLine(const std::filesystem::path& file) {
    std::ifstream in(file);
    std::string line;
    if (!std::getline(in, line)) return std::string();
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    return line;
}

std::string PathText(const std::filesystem::path& path) {
    return path.u8string();
}
}  // namespace

bool ParseVersion(const std::string& text, Version& out) {
    std::string s = text;
    Version v;
    const std::string dev = "-dev";
    if (s.size() > dev.size() && s.compare(s.size() - dev.size(), dev.size(), dev) == 0) {
        v.dev = true;
        s.resize(s.size() - dev.size());
    }
    const size_t a = s.find('.');
    const size_t b = a == std::string::npos ? a : s.find('.', a + 1);
    if (b == std::string::npos) return false;
    if (!Number(s.substr(0, a), v.major) || !Number(s.substr(a + 1, b - a - 1), v.minor) ||
        !Number(s.substr(b + 1), v.patch))
        return false;
    out = v;
    return true;
}

bool Newer(const std::string& candidate, const std::string& current) {
    Version c, now;
    if (!ParseVersion(candidate, c) || !ParseVersion(current, now)) return false;
    if (c.major != now.major) return c.major > now.major;
    if (c.minor != now.minor) return c.minor > now.minor;
    if (c.patch != now.patch) return c.patch > now.patch;
    return !c.dev && now.dev;
}

Install FindInstall(const std::string& exeDir) {
    namespace fs = std::filesystem;
    Install install;
    if (exeDir.empty()) return install;
    fs::path build = fs::u8path(exeDir);
    if (!build.has_filename()) build = build.parent_path();   // SDL_GetBasePath ends with a separator
    if (build.filename() != "build") return install;
    const fs::path dir = build.parent_path();
    const std::string channel = FirstLine(dir / "channel.txt");
    if (channel != "stable" && channel != "dev") return install;
#ifdef _WIN32
    const char* script = "install.ps1";
#else
    const char* script = "install.sh";
#endif
    std::error_code ec;
    if (!fs::is_regular_file(dir / "source" / script, ec)) return install;
    install.dir = PathText(dir);
    return install;
}

std::vector<std::string> StageCommand(const Install& install) {
    const std::filesystem::path dir = std::filesystem::u8path(install.dir);
#ifdef _WIN32
    std::vector<std::string> argv = {"powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                                     PathText(dir / "source" / "install.ps1"), "-Update", "-Stage", "-Dir",
                                     install.dir};
#else
    std::vector<std::string> argv = {"/bin/sh", PathText(dir / "source" / "install.sh"), "--update", "--stage", "--dir",
                                     install.dir};
#endif
    return argv;
}

std::vector<std::string> SwapCommand(const Install& install, unsigned long pid) {
    const std::filesystem::path dir = std::filesystem::u8path(install.dir);
#ifdef _WIN32
    return {"powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-File",
            PathText(dir / "source" / "install.ps1"), "-SwapAndLaunch", "-WaitPid", std::to_string(pid), "-Dir",
            install.dir};
#else
    return {"/bin/sh", PathText(dir / "source" / "install.sh"), "--swap-and-launch", "--wait-pid", std::to_string(pid),
            "--dir", install.dir};
#endif
}

bool ParseStep(const std::string& line, int& step, int& steps, std::string& words) {
    if (line.rfind("STEP ", 0) != 0) return false;
    const size_t slash = line.find('/', 5), space = line.find(' ', 5);
    if (slash == std::string::npos || space == std::string::npos || slash > space) return false;
    long n = 0, total = 0;
    if (!Number(line.substr(5, slash - 5), n) || !Number(line.substr(slash + 1, space - slash - 1), total)) return false;
    if (n < 1 || total < n) return false;
    step = static_cast<int>(n);
    steps = static_cast<int>(total);
    words = line.substr(space + 1);
    return true;
}

bool ParseResult(const std::string& line, bool& ok, std::string& rest) {
    if (line.rfind("RESULT ok", 0) == 0) {
        ok = true;
    } else if (line.rfind("RESULT fail", 0) == 0) {
        ok = false;
    } else {
        return false;
    }
    const size_t space = line.find(' ', 7);
    rest = space == std::string::npos ? std::string() : line.substr(space + 1);
    return true;
}

Current DescribeCurrent(const std::string& version, const std::string& exeDir) {
    Current current;
    current.version = version;
    current.install = FindInstall(exeDir);
    return current;
}

Answer Check(const Current& current, const Questions& questions, const net::Limits& limits) {
    Answer answer;
    const net::Release release = questions.latestRelease(limits);
    answer.answered = release.answered;
    answer.why = release.why;
    if (release.answered && Newer(release.version, current.version)) answer.found = release.version;
    return answer;
}

}  // namespace update

Updater::Updater(ImportBackend& settings, update::Current current, void (*log)(const char*), Hooks hooks)
    : settings_(settings), current_(std::move(current)), log_(log), hooks_(std::move(hooks)) {
    if (!hooks_.exists)
        hooks_.exists = [](const std::string& path) {
            std::error_code ec;
            return std::filesystem::exists(std::filesystem::u8path(path), ec);
        };
    enabled_ = settings_.Setting(kCheckUpdatesKey) != "0";
    const std::string found = settings_.Setting(kUpdateFoundKey);
    if (Usable(found)) found_ = found;
}

Updater::~Updater() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
}

void Updater::Log(const std::string& line) {
    if (log_ != nullptr) log_(line.c_str());
}

// A newer version than this launcher's (launcher.ini may hold a commit from before checks were releases only: not one).
bool Updater::Usable(const std::string& found) const {
    if (found.empty() || found == "none") return false;
    return update::Newer(found, current_.version);
}

void Updater::Remember(const std::string& found) {
    found_ = Usable(found) ? found : std::string();
    std::string err;
    if (!settings_.SaveSetting(kUpdateFoundKey, found_.empty() ? "none" : found_, err))
        Log("update: cannot keep what the check found in launcher.ini: " + err);
}

void Updater::Begin(Phase phase, std::function<void()> job) {
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        done_ = false;
        answer_ = update::Answer();
        staged_ = alreadyCurrent_ = false;
        stageVersion_.clear();
        stageFailure_.clear();
        step_ = steps_ = 0;
        stepWords_.clear();
    }
    phase_ = phase;
    worker_ = std::thread(std::move(job));
}

void Updater::StartupCheck(long long now) {
    if (!enabled_) {
        Log("update check: off (Settings)");
        return;
    }
    Log("update check: asking GitHub for the newest release (this launcher " + current_.version + ")" +
        (found_.empty() ? "" : "; found earlier: " + found_));
    checkTime_ = now;
    Begin(Phase::Checking, [this] {
        net::Limits limits;
        limits.cancel = &cancel_;
        update::Answer answer = update::Check(current_, hooks_.questions, limits);
        std::lock_guard<std::mutex> lock(mutex_);
        answer_ = std::move(answer);
        done_ = true;
    });
}

void Updater::Update() {
    if (!ScriptInstall() || Moving() || phase_ == Phase::Checking) return;
    Log("update: Update pressed: asking GitHub again, then " + update::StageCommand(current_.install)[1]);
    Begin(Phase::Probing, [this] {
        net::Limits limits;
        limits.cancel = &cancel_;
        update::Answer answer = update::Check(current_, hooks_.questions, limits);
        const bool stage = answer.answered && !answer.found.empty() && !cancel_;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            answer_ = answer;
            if (!stage) {
                done_ = true;
                return;
            }
        }
        proc::Options options;
        options.argv = update::StageCommand(current_.install);
        options.workingDir = current_.install.dir;
        options.timeoutSeconds = 60 * 60;   // a slow computer builds in about ten minutes
        options.mergeStderr = true;
        options.cancel = &cancel_;
        bool sawResult = false, ok = false;
        std::string rest;
        options.onLine = [&](const std::string& line) {
            std::lock_guard<std::mutex> lock(mutex_);
            lines_.push_back("update: " + line);
            int step = 0, steps = 0;
            std::string words;
            if (update::ParseStep(line, step, steps, words)) {
                step_ = step;
                steps_ = steps;
                stepWords_ = words;
            }
            // The last RESULT line is the one (the script prints one, last).
            if (update::ParseResult(line, ok, rest)) sawResult = true;
        };
        // (the first line: the Staging phase starts, seen by Poll from step_)
        const proc::Result run = hooks_.run(options);
        std::lock_guard<std::mutex> lock(mutex_);
        if (run.Ok() && sawResult && ok) {
            const std::filesystem::path next = std::filesystem::u8path(current_.install.dir) / "build-next";
#ifdef _WIN32
            const std::string launcher = (next / "tpw_launcher.exe").u8string();
#else
            const std::string launcher = (next / "tpw_launcher").u8string();
#endif
            if (hooks_.exists(launcher)) {
                staged_ = true;
                stageVersion_ = rest;
            } else {
                alreadyCurrent_ = true;   // the script found this launcher current and built nothing
            }
        } else if (!run.started) {
            stageFailure_ = "The update script could not be started";
        } else if (run.cancelled) {
            stageFailure_ = "The update was stopped";
        } else if (run.timedOut) {
            stageFailure_ = "The update took more than an hour and was stopped";
        } else if (sawResult && !ok && !rest.empty()) {
            stageFailure_ = rest;
        } else {
            stageFailure_ = "The update script stopped without saying why (exit code " + std::to_string(run.exitCode) + ")";
        }
        done_ = true;
    });
}

void Updater::Poll() {
    std::vector<std::string> lines;
    bool done = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lines.swap(lines_);
        done = done_;
        if (phase_ == Phase::Probing && steps_ > 0) phase_ = Phase::Staging;
    }
    for (const std::string& line : lines) Log(line);
    if (!done) return;
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    done_ = false;
    const Phase was = phase_;
    phase_ = Phase::Idle;
    const update::Answer answer = answer_;
    if (!answer.answered) {
        // No internet, GitHub down, a rate limit or junk: nothing is said, and the next launch asks again.
        offline_ = true;
        Log("update check: no answer (" + answer.why + ")" + (was == Phase::Checking ? "; asked again next launch" : ""));
        return;
    }
    offline_ = false;
    if (was == Phase::Checking) {
        std::string err;
        if (!settings_.SaveSetting(kUpdateLastCheckKey, std::to_string(checkTime_), err))
            Log("update check: cannot keep its time in launcher.ini: " + err);
    }
    if (answer.found.empty()) {
        Log("update check: up to date (this launcher is " + current_.version + ")");
        if (was != Phase::Checking) notice_ = "Already up to date";
        Remember("");
        return;
    }
    Log("update check: found version " + answer.found);
    Remember(answer.found);
    if (was == Phase::Checking) return;
    if (staged_) {
        phase_ = Phase::Staged;
        stagedVersion_ = stageVersion_;
        Log("update: version " + stageVersion_ + " is built and waits in build-next");
    } else if (alreadyCurrent_) {
        notice_ = "Already up to date";
        Log("update: the script found this launcher up to date");
        Remember("");
    } else if (!stageFailure_.empty()) {
        phase_ = Phase::Failed;
        failReason_ = stageFailure_;
        Log("update: did not finish: " + failReason_ + " (nothing was changed)");
    }
}

void Updater::SetEnabled(bool on) {
    enabled_ = on;
    Log(std::string("update check: turned ") + (on ? "on" : "off") + " in Settings");
}

bool Updater::Visible() const {
    if (phase_ == Phase::Probing || phase_ == Phase::Staging || phase_ == Phase::Staged || phase_ == Phase::Failed)
        return true;
    return enabled_ && !found_.empty();
}

std::string Updater::WhatsNewUrl() const {
    return net::ReleasePageUrl(found_);
}

std::string Updater::StepText() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (steps_ == 0) return "Starting";
    return stepWords_ + " (step " + std::to_string(step_) + " of " + std::to_string(steps_) + ")";
}

void Updater::Restart() {
    if (phase_ != Phase::Staged) return;
    std::string err;
    const std::vector<std::string> argv = update::SwapCommand(current_.install, proc::CurrentId());
    if (!hooks_.spawn(argv, current_.install.dir, err)) {
        phase_ = Phase::Failed;
        failReason_ = "Could not start the switch to the new version: " + err;
        Log("update: " + failReason_);
        return;
    }
    Log("update: the switch to version " + stagedVersion_ + " starts when this launcher has quit (" + argv[1] + ")");
    quit_ = true;
}

void Updater::Dismiss() {
    if (phase_ == Phase::Failed) phase_ = Phase::Idle;
}

bool Updater::TakeQuit() {
    const bool quit = quit_;
    quit_ = false;
    return quit;
}

std::string Updater::TakeNotice() {
    std::string notice;
    notice.swap(notice_);
    return notice;
}

}  // namespace tpw::ui
