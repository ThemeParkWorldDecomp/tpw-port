// handoff.cpp: the hand-off from the launcher's window to the game's (handoff.h).
#include "handoff.h"

#include <pthread.h>
#include <pthread/qos.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>

#include <SDL3/SDL.h>

#include "platform.h"

namespace fs = std::filesystem;

namespace tpw {

namespace {

using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point from) {
    return std::chrono::duration<double, std::milli>(Clock::now() - from).count();
}

// ---- The preload's state. Never destroyed (the thread is joined before the launcher returns; a static destructor
// running at exit must not race it).
struct Preload {
    std::mutex mutex;
    std::condition_variable doneSignal;
    std::thread thread;
    bool started = false;
    bool done = false;                     // under mutex
    std::atomic<bool> cancel{false};
    std::atomic<bool> hurry{false};        // Play is waiting: the normal priority from the next piece on
    std::atomic<uint64_t> bytes{0};
    std::atomic<uint64_t> total{0};
    PreloadPlan plan;
    // The tables, read on the thread (valid once done).
    bool tablesTried = false;
    bool tablesOk = false;
    std::string tablesWhy;
    double tablesMs = 0, filesMs = 0;
    int filesRead = 0;
    bool cut = false;                      // stopped by CancelPreload before the end
};
Preload& ThePreload() {
    static auto* preload = new Preload;
    return *preload;
}

// ---- The start's state (main thread, except the atomics).
struct Startup {
    bool begun = false;
    bool shown = false;
    std::thread::id mainThread;
    Clock::time_point clock;
    std::atomic<uint64_t> gameBytes{0};
    Clock::time_point lastAnswer;            // KeepAnswering
    int answers = 0;
    uint64_t expectedGameBytes = 0;
    StartupPump pump = nullptr;
    GameWindowShownHook shownHook = nullptr;
    Clock::time_point lastPump, lastCall;
    double longestGapMs = 0;
    double longestGapAtMs = 0;   // where (ms since Play) the longest stretch ended
    int pumps = 0;
};
Startup& TheStartup() {
    static auto* startup = new Startup;
    return *startup;
}

uint64_t SizeOf(const fs::path& path) {
    std::error_code ec;
    const uintmax_t size = fs::file_size(path, ec);
    return ec ? 0 : static_cast<uint64_t>(size);
}

void SetQos(qos_class_t qos) { pthread_set_qos_class_self_np(qos, 0); }

void RunPreload(Preload* p) {
    SetQos(QOS_CLASS_BACKGROUND);
    bool promoted = false;
    auto keepUp = [&] {
        if (!promoted && p->hurry.load()) {
            SetQos(QOS_CLASS_USER_INITIATED);
            promoted = true;
        }
    };
    if (!p->plan.program.empty() && p->plan.loadTables != nullptr) {
        const Clock::time_point from = Clock::now();
        keepUp();
        // Only the tables' own arrays are written (game_tables.h), and nothing reads them until the game starts, which
        // waits for this thread first (WaitPreload).
        p->tablesOk = p->plan.loadTables(p->plan.program.string().c_str(), &p->tablesWhy);
        p->tablesTried = true;
        p->tablesMs = MsSince(from);
        p->bytes += SizeOf(p->plan.program);
    }
    const Clock::time_point from = Clock::now();
    std::vector<char> piece(size_t(1) << 20);
    for (const fs::path& path : p->plan.files) {
        if (p->cancel.load()) break;
        std::FILE* file = std::fopen(path.string().c_str(), "rb");
        if (file == nullptr) continue;
        std::setvbuf(file, nullptr, _IONBF, 0);
        size_t got;
        while (!p->cancel.load() && (got = std::fread(piece.data(), 1, piece.size(), file)) > 0) {
            p->bytes += got;
            keepUp();
        }
        std::fclose(file);
        ++p->filesRead;
    }
    p->filesMs = MsSince(from);
    // A cancelled preload ends where it stopped: the bar's total shrinks to what was read.
    if (p->cancel.load() && p->bytes.load() < p->total.load()) {
        p->cut = true;
        p->total = p->bytes.load();
    }
    std::lock_guard<std::mutex> lock(p->mutex);
    p->done = true;
    p->doneSignal.notify_all();
}

}  // namespace

// ---- The bar's amount

float StartupFraction(uint64_t done, uint64_t total, bool shown) {
    if (shown) return 1.0f;
    if (total == 0) return -1.0f;
    const double f = static_cast<double>(done) / static_cast<double>(total);
    return static_cast<float>(std::clamp(f, 0.0, 0.99));
}

// ---- The start, counted

void StartupBegin(uint64_t expectedGameBytes) {
    Startup& s = TheStartup();
    s.begun = true;
    s.mainThread = std::this_thread::get_id();
    s.clock = Clock::now();
    s.lastCall = s.clock;
    s.lastPump = s.clock - std::chrono::seconds(1);   // the first call draws at once
    s.gameBytes = 0;
    s.expectedGameBytes = expectedGameBytes;
    s.longestGapMs = 0;
}

uint64_t StartupGameBytes() { return TheStartup().gameBytes.load(); }

bool StartupBegun() { return TheStartup().begun; }

float StartupProgress() {
    const Startup& s = TheStartup();
    // No total from an earlier start: the game's share is unknown, so the bar has no number at all.
    if (s.expectedGameBytes == 0) return s.shown ? 1.0f : -1.0f;
    return StartupFraction(PreloadBytesDone() + s.gameBytes.load(), PreloadBytesTotal() + s.expectedGameBytes, s.shown);
}

void StartupStamp(const char* stage) {
    const Startup& s = TheStartup();
    const double ms = s.begun ? MsSince(s.clock) : 0.0;
    const float f = StartupProgress();
    char fraction[32];
    if (f < 0) std::snprintf(fraction, sizeof fraction, "no total");
    else std::snprintf(fraction, sizeof fraction, "%.1f%%", f * 100.0f);
    Log("startup: %s +%.0fms (game read %llu bytes, preload %llu of %llu, bar %s)", stage, ms,
        static_cast<unsigned long long>(s.gameBytes.load()), static_cast<unsigned long long>(PreloadBytesDone()),
        static_cast<unsigned long long>(PreloadBytesTotal()), fraction);
}

// After the game's window is shown: the game loads the lobby (and later each park) in one long stretch of file reads
// without asking for events (about 5.5 s for the lobby, measured 2026-10-22), so the system would take it for hung and
// show the spinning cursor over its window. As SystemTask (platform/window.cpp) does in the engine's waits, the file
// layer lets the system's events in, at most every 100 ms; they wait in SDL's queue for the game's next pass.
static void KeepAnswering(Startup& s) {
    const Clock::time_point now = Clock::now();
    if (std::chrono::duration<double, std::milli>(now - s.lastAnswer).count() < 100.0) return;
    s.lastAnswer = now;
    if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO)) return;
    SDL_PumpEvents();
    ++s.answers;
}

void CountStartupRead(uint64_t bytes) {
    Startup& s = TheStartup();
    if (s.shown && std::this_thread::get_id() == s.mainThread) KeepAnswering(s);
    if (!s.begun || s.shown) return;
    s.gameBytes += bytes;
    if (std::this_thread::get_id() == s.mainThread) OnStartupProgress();
}

void OnStartupProgress() {
    Startup& s = TheStartup();
    if (!s.begun || s.shown || std::this_thread::get_id() != s.mainThread) return;
    const Clock::time_point now = Clock::now();
    const double gap = std::chrono::duration<double, std::milli>(now - s.lastCall).count();
    s.lastCall = now;
    if (gap > s.longestGapMs) {
        s.longestGapMs = gap;
        s.longestGapAtMs = MsSince(s.clock);
    }
    if (s.pump == nullptr || std::chrono::duration<double, std::milli>(now - s.lastPump).count() < 40.0) return;
    s.lastPump = now;
    ++s.pumps;
    s.pump(StartupProgress());
}

void SetStartupPump(StartupPump pump) { TheStartup().pump = pump; }

void SetGameWindowShownHook(GameWindowShownHook hook) { TheStartup().shownHook = hook; }

void OnGameWindowShown() {
    Startup& s = TheStartup();
    if (!s.begun || s.shown) return;
    OnStartupProgress();   // the stretch up to here counts
    s.shown = true;
    StartupStamp("ShowWindow");
    Log("startup: the launcher's window was drawn %d times; the longest stretch without a file read or a wait was %.0f ms "
        "(ending +%.0fms)", s.pumps, s.longestGapMs, s.longestGapAtMs);
    if (s.shownHook != nullptr) s.shownHook(s.gameBytes.load());
}

// ---- The preload

uint64_t PreloadPlanBytes(const PreloadPlan& plan) {
    uint64_t total = plan.program.empty() ? 0 : SizeOf(plan.program);
    for (const fs::path& path : plan.files) total += SizeOf(path);
    return total;
}

void StartPreload(const PreloadPlan& plan) {
    Preload& p = ThePreload();
    if (p.started) return;
    p.started = true;
    p.plan = plan;
    p.total = PreloadPlanBytes(plan);
    Log("preload: %zu files and %s, %llu bytes, on a thread of its own", plan.files.size(),
        plan.program.empty() ? "no tables" : "the game's tables", static_cast<unsigned long long>(p.total.load()));
    p.thread = std::thread(RunPreload, &p);
}

void CancelPreload() { ThePreload().cancel = true; }

void WaitPreload() {
    Preload& p = ThePreload();
    if (!p.started || !p.thread.joinable()) return;
    p.hurry = true;
    const Clock::time_point from = Clock::now();
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(p.mutex);
            if (p.doneSignal.wait_for(lock, std::chrono::milliseconds(20), [&] { return p.done; })) break;
        }
        OnStartupProgress();
    }
    p.thread.join();
    Log("preload: %s; tables %.0f ms, %d files %.0f ms, %llu bytes; waited %.0f ms for it",
        p.cut ? "cancelled" : "done", p.tablesMs, p.filesRead, p.filesMs,
        static_cast<unsigned long long>(p.bytes.load()), MsSince(from));
}

uint64_t PreloadBytesDone() { return ThePreload().bytes.load(); }

uint64_t PreloadBytesTotal() { return ThePreload().total.load(); }

bool PreloadedTables(const fs::path& program, bool* ok, std::string* why) {
    WaitPreload();
    const Preload& p = ThePreload();
    if (!p.tablesTried || p.plan.program != program) return false;
    *ok = p.tablesOk;
    if (why != nullptr) *why = p.tablesWhy;
    return true;
}

}  // namespace tpw
