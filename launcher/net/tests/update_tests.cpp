// update_tests.cpp: the network module (launcher/net/net.h) and the update check (launcher/ui/updater.h), with no
// internet. The curl it runs is this program itself (TPW_NET_CURL, with TPW_NET_FAKE saying what to answer): GitHub's
// answers, a captive portal's, junk, silence and a flood. Then the system's curl against a port nothing listens on: no
// answer, quickly. Then the Updater with a scripted update script: staged, failed, already current, offline.
#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "net.h"
#include "updater.h"

namespace fs = std::filesystem;
using namespace tpw;

namespace {

int gFailures = 0;
int gChecks = 0;

void Check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::fprintf(stderr, "FAILED: %s\n", what.c_str());
    }
}

// This program as curl: TPW_NET_FAKE names the answer. Also as any program process.h starts (TestProcessFolder).
int FakeCurl(const std::string& mode, int argc, char** argv) {
    if (mode == "release") std::printf("302 https://github.com/ThemeParkWorldDecomp/tpw-port/releases/tag/v9.8.7");
    else if (mode == "older") std::printf("302 https://github.com/ThemeParkWorldDecomp/tpw-port/releases/tag/v0.0.1");
    else if (mode == "none") std::printf("302 https://github.com/ThemeParkWorldDecomp/tpw-port/releases");
    else if (mode == "portal") std::printf("200 ");
    else if (mode == "elsewhere") std::printf("302 https://portal.example/login?next=github.com");
    else if (mode == "beta") std::printf("302 https://github.com/ThemeParkWorldDecomp/tpw-port/releases/tag/v1.2.3-beta");
    else if (mode == "html") std::printf("<html><body>Sign in to the hotel's wifi</body></html>");
    else if (mode == "fail") return 22;
    else if (mode == "sleep") SDL_Delay(30000);
    else if (mode == "flood") {
        std::string chunk(4096, 'x');
        for (int i = 0; i < 64; ++i) std::fwrite(chunk.data(), 1, chunk.size(), stdout);
    } else if (mode == "args") {
        for (int i = 1; i < argc; ++i) std::printf("%s\n", argv[i]);
    } else if (mode == "pwd") {
        std::error_code ec;
        std::printf("%s\n", fs::current_path(ec).u8string().c_str());
    } else if (mode == "spawned") {   // where it runs, and whether its output and error can be written
        std::error_code ec;
        const bool out = std::fputs("x\n", stdout) >= 0 && std::fflush(stdout) == 0;
        const bool err = std::fputs("x\n", stderr) >= 0 && std::fflush(stderr) == 0;
        std::ofstream("spawned.tmp") << fs::current_path(ec).u8string() << "\n" << (out ? "out" : "") << "\n"
                                     << (err ? "err" : "") << "\n";
        fs::rename("spawned.tmp", "spawned.txt", ec);
    } else if (mode == "console") {
#ifdef _WIN32
        std::printf("%s\n", GetConsoleWindow() == nullptr ? "no window" : "a console window");
#else
        std::printf("no window\n");
#endif
    }
    std::fflush(stdout);
    return 0;
}

void Fake(const char* mode) {
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "TPW_NET_FAKE", mode, true);
}

Uint64 Since(Uint64 start) { return SDL_GetTicks() - start; }

bool Has(const std::vector<std::string>& argv, const std::string& arg) {
    return std::find(argv.begin(), argv.end(), arg) != argv.end();
}

// The argument after `flag` (empty when there is none).
std::string After(const std::vector<std::string>& argv, const std::string& flag) {
    const auto at = std::find(argv.begin(), argv.end(), flag);
    return at == argv.end() || at + 1 == argv.end() ? std::string() : *(at + 1);
}

// launcher.ini's keys, in memory.
class Settings : public ui::ImportBackend {
public:
    std::map<std::string, std::string> keys;
    bool Start(const std::vector<std::string>&, std::string&) override { return false; }
    bool Running() const override { return false; }
    void Cancel() override {}
    std::vector<assets::ImportProgress> Drain() override { return {}; }
    assets::ImportProgress Latest() const override { return {}; }
    assets::ImportReport Wait() override { return {}; }
    assets::EditionStatus Status(assets::Edition) const override { return {}; }
    std::string DataFolder() const override { return ""; }
    bool ChangeDataFolder(const std::string&, std::string&) override { return false; }
    long VerifyEdition(assets::Edition, std::string&) override { return 0; }
    bool RemoveEdition(assets::Edition, std::string&) override { return false; }
    std::string LastChoice() const override { return ""; }
    bool SetLastChoice(const std::string&, std::string&) override { return true; }
    std::string Setting(const std::string& key) const override {
        auto it = keys.find(key);
        return it == keys.end() ? std::string() : it->second;
    }
    bool SaveSetting(const std::string& key, const std::string& value, std::string&) override {
        keys[key] = value;
        return true;
    }
};

// Polls until the updater's thread has finished (or 5 seconds).
void Settle(ui::Updater& updater) {
    const Uint64 start = SDL_GetTicks();
    while (Since(start) < 5000) {
        updater.Poll();
        const ui::Updater::Phase phase = updater.CurrentPhase();
        if (phase != ui::Updater::Phase::Checking && phase != ui::Updater::Phase::Probing &&
            phase != ui::Updater::Phase::Staging)
            return;
        SDL_Delay(5);
    }
}

void TestParsers() {
    using net::ParseLatestRelease;
    net::Release r = ParseLatestRelease("302 https://github.com/ThemeParkWorldDecomp/tpw-port/releases/tag/v0.0.3");
    Check(r.answered && r.version == "0.0.3", "a release tag is an answer");
    r = ParseLatestRelease("302 https://github.com/ThemeParkWorldDecomp/tpw-port/releases\n");
    Check(r.answered && r.version.empty(), "no release yet is an answer, with no version");
    for (const char* junk : {"", "200 ", "200 https://github.com/ThemeParkWorldDecomp/tpw-port/releases/tag/v0.0.3",
                             "301 https://github.com/ThemeParkWorldDecomp/tpw-port/releases/tag/v0.0.3",
                             "302 http://github.com/ThemeParkWorldDecomp/tpw-port/releases/tag/v0.0.3",
                             "302 https://github.com/Someone/tpw-port/releases/tag/v0.0.3",
                             "302 https://github.com/ThemeParkWorldDecomp/tpw-port/releases/tag/v0.0",
                             "302 https://github.com/ThemeParkWorldDecomp/tpw-port/releases/tag/v0.0.3x",
                             "302 https://github.com/ThemeParkWorldDecomp/tpw-port/releases/tag/0.0.3",
                             "302 https://github.com/ThemeParkWorldDecomp/tpw-port/releases/tag/v0.0.3/",
                             "302 https://github.com/ThemeParkWorldDecomp/tpw-port/releases/tag/v1234567.0.0",
                             "302 https://github.com/login", "<html>"}) {
        r = ParseLatestRelease(junk);
        Check(!r.answered && !r.why.empty(), std::string("junk is no answer: \"") + junk + "\"");
    }
    Check(net::ReleasePageUrl("0.0.4") == "https://github.com/ThemeParkWorldDecomp/tpw-port/releases/tag/v0.0.4",
          "the release page");
}

void TestFakeCurl(const std::string& self) {
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "TPW_NET_CURL", self.c_str(), true);
    Fake("args");
    net::Fetched f = net::Curl({"https://example.invalid/x"});
    f.output.erase(std::remove(f.output.begin(), f.output.end(), '\r'), f.output.end());   // printf's \r\n on Windows
    Check(f.ok && f.output.rfind("-q\n", 0) == 0, "curl's first argument is -q (no .curlrc): " + f.output.substr(0, 40));
    Check(f.output.find("--proto\n=https\n") != std::string::npos &&
              f.output.find("--proto-redir\n=https\n") != std::string::npos,
          "https only, also after a redirect");
    Check(f.output.find("--max-time\n10\n") != std::string::npos, "a time limit");

    Fake("release");
    net::Release r = net::LatestRelease();
    Check(r.answered && r.version == "9.8.7", "LatestRelease through curl: " + r.why);
    for (const char* junk : {"portal", "elsewhere", "beta", "html", "fail"}) {
        Fake(junk);
        r = net::LatestRelease();
        Check(!r.answered, std::string("LatestRelease refuses ") + junk);
    }
    Fake("flood");
    f = net::Curl({"x"});
    Check(!f.ok && f.why.find("too long") != std::string::npos, "a flood is cut off: " + f.why);

    // Silence: stopped at the limit and 2 seconds more, however long curl would wait.
    Fake("sleep");
    net::Limits limits;
    limits.seconds = 1;
    Uint64 start = SDL_GetTicks();
    r = net::LatestRelease(limits);
    Uint64 took = Since(start);
    Check(!r.answered && took < 5000, "a curl that never answers is no answer in " + std::to_string(took) + " ms");

    // The launcher closing: the cancel flag stops curl at once.
    std::atomic<bool> cancel{false};
    limits.seconds = 10;
    limits.cancel = &cancel;
    std::thread closer([&] {
        SDL_Delay(200);
        cancel = true;
    });
    start = SDL_GetTicks();
    r = net::LatestRelease(limits);
    took = Since(start);
    closer.join();
    Check(!r.answered && took < 4000, "a cancelled check ends in " + std::to_string(took) + " ms");

    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "TPW_NET_CURL", "", true);
}

// The system's curl, to a port nothing listens on (and, in the CI's offline runs, with networking denied): no answer,
// quickly. A system without curl answers nothing too.
void TestUnreachable() {
    net::Limits limits;
    limits.seconds = 3;
    const Uint64 start = SDL_GetTicks();
    const net::Fetched f = net::Curl({"-o",
#ifdef _WIN32
                                      "NUL",
#else
                                      "/dev/null",
#endif
                                      "-w", "%{http_code}", "https://127.0.0.1:9/"},
                                     limits);
    const Uint64 took = Since(start);
    Check(!f.ok && took < 6000, "nothing listening: no answer (" + f.why + ") in " + std::to_string(took) + " ms");
}

// process.h's working folder (the update script runs in DIR), its exit code and no console window (Windows), and
// Spawn's null input and output (open, not closed). Each program is this one (FakeCurl).
void TestProcessFolder(const fs::path& root, const std::string& self) {
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "TPW_NET_CURL", self.c_str(), true);
    std::error_code ec;
    const fs::path dir = root / "work dir";
    fs::create_directories(dir, ec);
    const fs::path before = fs::current_path(ec);
    proc::Options options;
    options.argv = {self};
    options.workingDir = dir.u8string();
    Fake("pwd");
    proc::Result r = proc::Run(options);
    std::string printed = r.output;
    while (!printed.empty() && (printed.back() == '\n' || printed.back() == '\r')) printed.pop_back();
    Check(r.Ok() && fs::equivalent(fs::u8path(printed), dir, ec), "Run in its working folder: " + printed);
    Check(fs::current_path(ec) == before, "the launcher's own folder is back");
    options.workingDir = (root / "not there").u8string();
    Check(!proc::Run(options).started, "a working folder that is not there: not started");
    options.workingDir.clear();
    Fake("fail");
    r = proc::Run(options);
    Check(r.started && r.exitCode == 22, "the program's exit code: " + std::to_string(r.exitCode));
    Fake("console");
    r = proc::Run(options);
    Check(r.Ok() && r.output.rfind("no window", 0) == 0, "no console window: " + r.output);

    Fake("spawned");
    std::string err;
    const bool spawned = proc::Spawn({self}, err, dir.u8string());
    const Uint64 start = SDL_GetTicks();
    while (!fs::exists(dir / "spawned.txt", ec) && Since(start) < 5000) SDL_Delay(10);
    std::ifstream in(dir / "spawned.txt");
    std::string where, out, errLine;
    std::getline(in, where);
    std::getline(in, out);
    std::getline(in, errLine);
    Check(spawned && fs::equivalent(fs::u8path(where), dir, ec) && out == "out" && errLine == "err",
          "Spawn in its working folder, its output open on the null device: " + where + " " + out + " " + errLine + err);
    Fake("");
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "TPW_NET_CURL", "", true);
}

void TestVersions() {
    using namespace ui::update;
    Version v;
    Check(ParseVersion("0.0.3", v) && v.patch == 3 && !v.dev, "0.0.3");
    Check(ParseVersion("1.20.300-dev", v) && v.major == 1 && v.minor == 20 && v.patch == 300 && v.dev, "1.20.300-dev");
    for (const char* bad : {"", "0.0", "v0.0.3", "0.0.3-beta", "0..3", "a.b.c", "0.0.3 "})
        Check(!ParseVersion(bad, v), std::string("not a version: ") + bad);
    Check(Newer("0.0.4", "0.0.3"), "0.0.4 > 0.0.3");
    Check(Newer("0.0.4", "0.0.4-dev"), "0.0.4 > 0.0.4-dev");
    Check(!Newer("0.0.3", "0.0.4-dev"), "0.0.3 < 0.0.4-dev");
    Check(!Newer("0.0.4", "0.0.4"), "0.0.4 = 0.0.4");
    Check(Newer("0.10.0", "0.9.9"), "numbers, not text");
    Check(!Newer("junk", "0.0.1"), "junk is never newer");
    int step = 0, steps = 0;
    std::string words;
    Check(ParseStep("STEP 2/4 Downloading the source", step, steps, words) && step == 2 && steps == 4 &&
              words == "Downloading the source",
          "a STEP line");
    Check(!ParseStep("STEP 5/4 x", step, steps, words) && !ParseStep("step 1/2 x", step, steps, words) &&
              !ParseStep("STEP x/2 y", step, steps, words),
          "not STEP lines");
    bool ok = false;
    std::string rest;
    Check(ParseResult("RESULT ok 0.0.4", ok, rest) && ok && rest == "0.0.4", "RESULT ok");
    Check(ParseResult("RESULT fail Couldn't reach GitHub, try again later.", ok, rest) && !ok &&
              rest == "Couldn't reach GitHub, try again later.",
          "RESULT fail");
    Check(!ParseResult("RESULTS", ok, rest) && !ParseResult("a RESULT ok", ok, rest), "not RESULT lines");
}

void TestFindInstall(const fs::path& root) {
    using namespace ui::update;
    const fs::path dir = root / "tpw port";
    fs::create_directories(dir / "build");
    fs::create_directories(dir / "source");
    Check(!FindInstall((dir / "build").u8string()).Found(), "no channel.txt: not a script install");
    std::ofstream(dir / "channel.txt") << "dev\r\n";
#ifdef _WIN32
    std::ofstream(dir / "source" / "install.ps1") << "\n";
#else
    std::ofstream(dir / "source" / "install.sh") << "\n";
#endif
    Install install = FindInstall((dir / "build").u8string() + "/");
    Check(install.Found() && install.dir == dir.u8string(), "a script install: " + install.dir);
    Check(!FindInstall((dir / "source").u8string()).Found(), "only from DIR/build");
    std::ofstream(dir / "channel.txt") << "nightly\n";
    Check(!FindInstall((dir / "build").u8string()).Found(), "an unknown channel");
    std::ofstream(dir / "channel.txt") << "dev\n";
    install = FindInstall((dir / "build").u8string());
    // A dev install too: Update goes to the newest release (never --dev).
    const std::vector<std::string> stage = StageCommand(install);
    const std::vector<std::string> swap = SwapCommand(install, 4242);
#ifdef _WIN32
    Check(stage.size() == 10 && stage[5] == (dir / "source" / "install.ps1").u8string() && stage[8] == "-Dir" &&
              stage.back() == dir.u8string(),
          "the stage command");
    Check(swap[8] == "-SwapAndLaunch" && swap[10] == "4242", "the swap command");
#else
    const std::vector<std::string> expected = {"/bin/sh", (dir / "source" / "install.sh").u8string(), "--update",
                                               "--stage", "--dir", dir.u8string()};
    Check(stage == expected, "the stage command");
    Check(swap.size() == 7 && swap[2] == "--swap-and-launch" && swap[3] == "--wait-pid" && swap[4] == "4242" &&
              swap[6] == dir.u8string(),
          "the swap command");
#endif
}

ui::update::Current Stable(bool script) {
    ui::update::Current current;
    current.version = "0.0.3";
    if (script) {
        current.install.dir = "/nowhere/tpw-port";
    }
    return current;
}

ui::Updater::Hooks Answering(const std::string& version, bool answered = true) {
    ui::Updater::Hooks hooks;
    hooks.questions.latestRelease = [=](const net::Limits&) {
        net::Release r;
        r.answered = answered;
        r.version = version;
        r.why = answered ? "" : "curl failed (exit code 6)";
        return r;
    };
    return hooks;
}

void TestUpdater() {
    using Phase = ui::Updater::Phase;
    const long long now = 1700000000;
    {   // the check finds a newer version and keeps it; the banner shows; the release page is What's new
        Settings settings;
        ui::Updater updater(settings, Stable(false), nullptr, Answering("0.0.4"));
        updater.StartupCheck(now);
        Settle(updater);
        Check(updater.Visible() && updater.Found() == "0.0.4" && !updater.Offline(), "found 0.0.4");
        Check(settings.keys["update_found"] == "0.0.4" && settings.keys["update_last_check"] == std::to_string(now),
              "kept in launcher.ini");
        Check(updater.WhatsNewUrl() == net::ReleasePageUrl("0.0.4"), "What's new is the release page");
        Check(!updater.ScriptInstall(), "a prebuilt launcher: Update opens the release page");
    }
    {   // the next launch, an hour later: the banner from launcher.ini at once, and the question asked again
        Settings settings;
        settings.keys = {{"update_found", "0.0.4"}, {"update_last_check", std::to_string(now)}};
        ui::Updater updater(settings, Stable(false), nullptr, Answering("0.0.5"));
        Check(updater.Visible() && updater.Found() == "0.0.4", "the kept banner shows before any answer");
        updater.StartupCheck(now + 3600);
        Settle(updater);
        Check(updater.Visible() && updater.Found() == "0.0.5" && settings.keys["update_found"] == "0.0.5" &&
                  settings.keys["update_last_check"] == std::to_string(now + 3600),
              "asked again within the day: a release out an hour later shows");
        settings.keys["update_found"] = "0.0.3";
        ui::Updater current(settings, Stable(false), nullptr, Answering("0.0.5"));
        Check(!current.Visible(), "a found version this launcher has is not shown");
    }
    {   // offline: nothing said, nothing kept, asked again next launch
        Settings settings;
        ui::Updater updater(settings, Stable(true), nullptr, Answering("", false));
        updater.StartupCheck(now);
        Settle(updater);
        Check(!updater.Visible() && updater.Offline() && settings.keys.count("update_last_check") == 0,
              "no answer: silent, and due again");
    }
    {   // turned off: no question, no banner
        Settings settings;
        settings.keys = {{"check_updates", "0"}, {"update_found", "0.0.4"}};
        ui::Updater updater(settings, Stable(false), nullptr, Answering("0.0.4"));
        updater.StartupCheck(now);
        Check(!updater.Visible() && updater.CurrentPhase() == Phase::Idle, "off");
    }
    {   // a script install: Update asks again, runs the script, shows its steps; built: Restart starts the switch
        Settings settings;
        ui::Updater::Hooks hooks = Answering("0.0.4");
        std::vector<std::string> ran, spawned;
        std::string ranIn, spawnedIn;
        std::atomic<bool> release{false};
        hooks.run = [&](const proc::Options& options) {
            ran = options.argv;
            ranIn = options.workingDir;
            options.onLine("STEP 1/4 Checking this computer");
            options.onLine("Some words for people");
            options.onLine("STEP 3/4 Building the launcher (a few minutes)");
            while (!release) SDL_Delay(2);
            options.onLine("RESULT ok 0.0.4");
            proc::Result r;
            r.started = true;
            r.exitCode = 0;
            return r;
        };
        hooks.exists = [](const std::string& path) { return path.find("build-next") != std::string::npos; };
        hooks.spawn = [&](const std::vector<std::string>& argv, const std::string& dir, std::string&) {
            spawned = argv;
            spawnedIn = dir;
            return true;
        };
        ui::Updater updater(settings, Stable(true), nullptr, hooks);
        updater.StartupCheck(now);
        Settle(updater);
        updater.Update();
        const Uint64 start = SDL_GetTicks();
        while (updater.CurrentPhase() != Phase::Staging && Since(start) < 3000) {
            SDL_Delay(2);
            updater.Poll();
        }
        SDL_Delay(20);
        Check(updater.CurrentPhase() == Phase::Staging && updater.Moving(), "staging");
        Check(updater.StepText() == "Building the launcher (a few minutes) (step 3 of 4)", "the step: " + updater.StepText());
        release = true;
        Settle(updater);
        Check(updater.CurrentPhase() == Phase::Staged && updater.StagedVersion() == "0.0.4", "staged");
#ifdef _WIN32
        const char *update = "-Update", *swap = "-SwapAndLaunch", *waitPid = "-WaitPid";
#else
        const char *update = "--update", *swap = "--swap-and-launch", *waitPid = "--wait-pid";
#endif
        Check(Has(ran, update) && ranIn == Stable(true).install.dir, "the stage command ran, in DIR: " + ranIn);
        updater.Restart();
        Check(updater.TakeQuit() && Has(spawned, swap) &&
                  After(spawned, waitPid) == std::to_string(proc::CurrentId()) && spawnedIn == Stable(true).install.dir,
              "Restart starts the switch with this process's id, in DIR, and quits");
    }
    {   // the script fails: its reason, nothing changed
        Settings settings;
        ui::Updater::Hooks hooks = Answering("0.0.4");
        hooks.run = [](const proc::Options& options) {
            options.onLine("STEP 2/4 Downloading the source and the build's tools");
            options.onLine("RESULT fail Couldn't reach GitHub, try again later.");
            proc::Result r;
            r.started = true;
            r.exitCode = 1;
            return r;
        };
        ui::Updater updater(settings, Stable(true), nullptr, hooks);
        updater.StartupCheck(now);
        Settle(updater);
        updater.Update();
        Settle(updater);
        Check(updater.CurrentPhase() == Phase::Failed && updater.FailReason() == "Couldn't reach GitHub, try again later.",
              "failed: " + updater.FailReason());
        updater.Dismiss();
        Check(updater.CurrentPhase() == Phase::Idle && updater.Visible(), "dismissed: the banner again");
    }
    {   // the script found nothing to build (this launcher is current): "Already up to date", the banner goes
        Settings settings;
        ui::Updater::Hooks hooks = Answering("0.0.4");
        hooks.run = [](const proc::Options& options) {
            options.onLine("RESULT ok 0.0.3");
            proc::Result r;
            r.started = true;
            r.exitCode = 0;
            return r;
        };
        hooks.exists = [](const std::string&) { return false; };
        ui::Updater updater(settings, Stable(true), nullptr, hooks);
        updater.StartupCheck(now);
        Settle(updater);
        updater.Update();
        Settle(updater);
        Check(updater.TakeNotice() == "Already up to date" && !updater.Visible() && settings.keys["update_found"] == "none",
              "already current");
    }
    {   // Update while offline: the question first, no script
        Settings settings;
        settings.keys = {{"update_found", "0.0.4"}, {"update_last_check", std::to_string(now)}};
        ui::Updater::Hooks hooks = Answering("", false);
        bool ran = false;
        hooks.run = [&](const proc::Options&) {
            ran = true;
            return proc::Result();
        };
        ui::Updater updater(settings, Stable(true), nullptr, hooks);
        updater.Update();
        Settle(updater);
        Check(!ran && updater.Offline() && updater.Visible(), "offline: Update greys out, the script never runs");
    }
    {   // a dev build hears only of releases: a newer one than itself, and 0.0.4 over 0.0.4-dev
        Settings settings;
        ui::update::Current current = Stable(true);
        current.version = "0.0.4-dev";
        ui::Updater ahead(settings, current, nullptr, Answering("0.0.3"));
        ahead.StartupCheck(now);
        Settle(ahead);
        Check(!ahead.Visible() && settings.keys["update_found"] == "none", "0.0.4-dev, 0.0.3 out: nothing");
        Settings later;
        ui::Updater released(later, current, nullptr, Answering("0.0.4"));
        released.StartupCheck(now);
        Settle(released);
        Check(released.Visible() && released.Found() == "0.0.4" &&
                  released.WhatsNewUrl() == net::ReleasePageUrl("0.0.4"),
              "0.0.4-dev, 0.0.4 out: offered");
        Settings old;
        old.keys = {{"update_found", "0123456789abcdef0123456789abcdef01234567"},
                    {"update_last_check", std::to_string(now)}};
        ui::Updater commit(old, current, nullptr, Answering("0.0.3"));
        commit.StartupCheck(now);
        Check(!commit.Visible(), "a commit kept from the old dev check: no banner");
    }
    {   // closing the launcher during a check does not wait for curl
        Settings settings;
        ui::Updater::Hooks hooks;
        hooks.questions.latestRelease = [](const net::Limits& limits) {
            while (!limits.cancel->load()) SDL_Delay(5);
            return net::Release();
        };
        const Uint64 start = SDL_GetTicks();
        {
            ui::Updater updater(settings, Stable(false), nullptr, hooks);
            updater.StartupCheck(now);
            SDL_Delay(50);
        }
        Check(Since(start) < 1000, "the updater goes at once with a check under way");
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (const char* mode = SDL_getenv("TPW_NET_FAKE"); mode != nullptr && *mode != '\0' &&
                                                     SDL_getenv("TPW_NET_CURL") != nullptr)
        return FakeCurl(mode, argc, argv);
    const std::string self = fs::absolute(fs::u8path(argv[0])).u8string();

    TestParsers();
    TestVersions();
    TestFakeCurl(self);
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "TPW_NET_FAKE", "", true);
    TestUnreachable();
    TestUpdater();

    std::error_code ec;
    const fs::path root = fs::temp_directory_path(ec) /
                          ("tpw-update-tests-" + std::to_string(static_cast<long long>(std::time(nullptr))) + "-" +
                           std::to_string(proc::CurrentId()));
    fs::create_directories(root, ec);
    TestFindInstall(root);
    TestProcessFolder(root, self);
    fs::remove_all(root, ec);

    std::printf("%d of %d update checks passed\n", gChecks - gFailures, gChecks);
    return gFailures == 0 ? 0 : 1;
}
