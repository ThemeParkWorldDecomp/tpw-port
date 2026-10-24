// updates.cpp: see updates.h.
#include "updates.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>

#include "texts.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace tpw::ui {
namespace {

namespace fs = std::filesystem;

#ifdef _WIN32
constexpr const char* kUpdateScript = "update.cmd";
#else
constexpr const char* kUpdateScript = "update.command";
#endif

// How long a git step may take before it is stopped; the fetch goes over the network.
constexpr int kStepSeconds = 30;
constexpr int kFetchSeconds = 90;

// One git step: whether it started (git is on the PATH) and finished (in time, not cancelled), its exit code and its
// output (stdout and stderr together).
struct Run {
    bool started = false;
    bool finished = false;
    int exitCode = -1;
    std::string output;
};

// The first line of `text` with something on it, without its line ending.
std::string FirstLine(const std::string& text) {
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find_first_of("\r\n", start);
        if (end == std::string::npos) end = text.size();
        if (end > start) return text.substr(start, end - start);
        start = end + 1;
    }
    return {};
}

std::string Trimmed(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

#ifdef _WIN32
std::wstring Wide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), n);
    return wide;
}

// One argument as CommandLineToArgvW reads it back: quoted when it holds a space, a tab or a quote, with the backslashes
// before a quote (and before the closing quote) doubled.
std::wstring Quote(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L"\"";
}

// The launcher's environment with GIT_TERMINAL_PROMPT=0 (no prompt for a user name or password: nobody can answer it),
// for git alone: set on the launcher itself, it would reach the game and the update script's window too.
std::vector<wchar_t> GitEnvironment() {
    // A variable's name ends at its first "=" after the first character ("=C:=C:\..." is one of cmd's).
    auto name = [](const std::wstring& var) { return var.substr(0, var.find(L'=', 1)); };
    auto compare = [](const std::wstring& a, const std::wstring& b) {
        return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE);
    };
    std::vector<std::wstring> vars;
    if (wchar_t* block = GetEnvironmentStringsW()) {
        for (const wchar_t* var = block; *var != L'\0'; var += wcslen(var) + 1) vars.emplace_back(var);
        FreeEnvironmentStringsW(block);
    }
    vars.erase(std::remove_if(vars.begin(), vars.end(),
                              [&](const std::wstring& var) { return compare(name(var), L"GIT_TERMINAL_PROMPT") == CSTR_EQUAL; }),
               vars.end());
    vars.emplace_back(L"GIT_TERMINAL_PROMPT=0");
    // An environment block is sorted by name, ignoring case.
    std::stable_sort(vars.begin(), vars.end(), [&](const std::wstring& a, const std::wstring& b) {
        return compare(name(a), name(b)) == CSTR_LESS_THAN;
    });
    std::vector<wchar_t> block;
    for (const std::wstring& var : vars) block.insert(block.end(), var.c_str(), var.c_str() + var.size() + 1);
    block.push_back(L'\0');
    return block;
}

// git through CreateProcessW: SDL_CreateProcess hides a console program's window only when it runs in the background,
// and SDL then reports its exit code as 0, so from the launcher (a program without a console) each git step would
// flash a console window. CREATE_NO_WINDOW keeps it hidden, and a pipe takes its output.
Run RunGit(const std::vector<std::string>& args, int seconds, const std::atomic<bool>& cancel) {
    Run run;
    std::wstring cmdline = L"git";
    for (const std::string& arg : args) cmdline += L" " + Quote(Wide(arg));
    SECURITY_ATTRIBUTES inherit{sizeof inherit, nullptr, TRUE};
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &inherit, 0)) return run;
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nul;
    startup.hStdOutput = writeEnd;
    startup.hStdError = writeEnd;
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> line(cmdline.begin(), cmdline.end());
    line.push_back(L'\0');
    std::vector<wchar_t> env = GitEnvironment();
    const BOOL started = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, env.data(), nullptr, &startup,
                                        &process);
    CloseHandle(writeEnd);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!started) {
        CloseHandle(readEnd);
        return run;
    }
    run.started = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    char chunk[4096];
    for (;;) {
        DWORD waiting = 0, got = 0;
        while (PeekNamedPipe(readEnd, nullptr, 0, nullptr, &waiting, nullptr) && waiting > 0 &&
               ReadFile(readEnd, chunk, std::min<DWORD>(waiting, sizeof chunk), &got, nullptr) && got > 0) {
            run.output.append(chunk, got);
        }
        if (WaitForSingleObject(process.hProcess, 50) == WAIT_OBJECT_0) {
            while (ReadFile(readEnd, chunk, sizeof chunk, &got, nullptr) && got > 0) run.output.append(chunk, got);
            DWORD code = 0;
            GetExitCodeProcess(process.hProcess, &code);
            run.exitCode = static_cast<int>(code);
            run.finished = true;
            break;
        }
        if (cancel || std::chrono::steady_clock::now() > deadline) {
            TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, 2000);
            break;
        }
    }
    CloseHandle(readEnd);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return run;
}
#else
Run RunGit(const std::vector<std::string>& args, int seconds, const std::atomic<bool>& cancel) {
    Run run;
    std::vector<const char*> argv{"git"};
    for (const std::string& arg : args) argv.push_back(arg.c_str());
    argv.push_back(nullptr);
    SDL_Environment* env = SDL_CreateEnvironment(true);
    if (env != nullptr) SDL_SetEnvironmentVariable(env, "GIT_TERMINAL_PROMPT", "0", true);   // nobody can answer a prompt
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    if (env != nullptr) SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    SDL_Process* process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (env != nullptr) SDL_DestroyEnvironment(env);
    if (process == nullptr) return run;
    run.started = true;
    SDL_IOStream* out = SDL_GetProcessOutput(process);   // non-blocking
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    char chunk[4096];
    for (;;) {
        size_t got;
        while (out != nullptr && (got = SDL_ReadIO(out, chunk, sizeof chunk)) > 0) run.output.append(chunk, got);
        int code = 0;
        if (SDL_WaitProcess(process, false, &code)) {
            while (out != nullptr && (got = SDL_ReadIO(out, chunk, sizeof chunk)) > 0) run.output.append(chunk, got);
            run.exitCode = code;
            run.finished = true;
            break;
        }
        if (cancel || std::chrono::steady_clock::now() > deadline) {
            SDL_KillProcess(process, true);
            SDL_WaitProcess(process, true, nullptr);
            break;
        }
        SDL_Delay(50);
    }
    SDL_DestroyProcess(process);
    return run;
}
#endif

}  // namespace

std::string FindUpdateClone(const std::string& exeDir) {
    if (exeDir.empty()) return {};
    std::error_code ec;
    fs::path dir = fs::path(exeDir).lexically_normal();
    if (!dir.has_filename()) dir = dir.parent_path();   // SDL_GetBasePath ends with a separator
    for (int up = 0; up <= 3 && !dir.empty(); ++up) {
        if (fs::exists(dir / ".git", ec) && fs::is_directory(dir / "tpw-decomp", ec) &&
            fs::is_regular_file(dir / kUpdateScript, ec))
            return dir.string();
        const fs::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return {};
}

UpdateCheck CheckForUpdates(const std::string& clone, const std::atomic<bool>& cancel) {
    using Kind = UpdateCheck::Kind;
    UpdateCheck check;
    // Each step is `git -C <clone> ...`, noted in the log with how it ended.
    auto step = [&](const char* what, std::vector<std::string> args, int seconds) {
        args.insert(args.begin(), {"-C", clone});
        Run run = RunGit(args, seconds, cancel);
        std::string line = std::string("update check: git ") + what + ": ";
        if (!run.started) line += "git could not be started";
        else if (!run.finished) line += "stopped (it took too long, or the launcher closed)";
        else line += "exit " + std::to_string(run.exitCode);
        check.log.push_back(line);
        return run;
    };
    auto failed = [&](const Run& run, Kind kind) {
        check.kind = kind;
        check.detail = run.finished ? FirstLine(run.output) : "it took too long";
        return check;
    };

    const Run version = step("--version", {"--version"}, kStepSeconds);
    if (!version.started) {
        check.kind = Kind::NoGit;
        return check;
    }
    if (!version.finished || version.exitCode != 0) return failed(version, Kind::Failed);
    // Changes to the clone's own tracked files stop a fast-forward pull; the submodules are the update script's to set.
    const Run status = step("status", {"status", "--porcelain", "--untracked-files=no", "--ignore-submodules=all"},
                            kStepSeconds);
    if (!status.finished || status.exitCode != 0) return failed(status, Kind::Failed);
    if (!Trimmed(status.output).empty()) {
        check.kind = Kind::LocalChanges;
        return check;
    }
    const Run upstream = step("upstream", {"rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}"}, kStepSeconds);
    if (!upstream.finished) return failed(upstream, Kind::Failed);
    if (upstream.exitCode != 0) {
        check.kind = Kind::NoBranch;
        return check;
    }
    // A fetch that stalls below 1 KB/s for 20 seconds gives up, well before the step's own limit.
    const Run fetch = step("fetch", {"-c", "http.lowSpeedLimit=1000", "-c", "http.lowSpeedTime=20", "fetch", "--quiet",
                                     "--no-recurse-submodules"},
                           kFetchSeconds);
    if (!fetch.finished || fetch.exitCode != 0) return failed(fetch, Kind::FetchFailed);
    const Run count = step("rev-list", {"rev-list", "--count", "HEAD..@{u}"}, kStepSeconds);
    if (!count.finished || count.exitCode != 0) return failed(count, Kind::Failed);
    check.behind = std::atoi(Trimmed(count.output).c_str());
    check.kind = check.behind > 0 ? Kind::Behind : Kind::UpToDate;
    return check;
}

bool StartUpdate(const std::string& clone, std::string& err) {
    const fs::path script = fs::path(clone) / kUpdateScript;
#ifdef _WIN32
    // cmd /s /c ""<script>" <pid>": /s takes the outer quotes off and leaves the quoted path whole, spaces and all. The
    // script gets a console window of its own and outlives the launcher.
    std::wstring cmdline = L"cmd.exe /s /c \"\"" + script.wstring() + L"\" " + std::to_wstring(GetCurrentProcessId()) + L"\"";
    std::vector<wchar_t> line(cmdline.begin(), cmdline.end());
    line.push_back(L'\0');
    wchar_t title[] = L"Updating tpw-port";
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.lpTitle = title;
    PROCESS_INFORMATION process{};
    const std::wstring dir = fs::path(clone).wstring();
    if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, dir.c_str(), &startup,
                        &process)) {
        err = "Windows could not start update.cmd (error " + std::to_string(GetLastError()) + ").";
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#else
    // A .command file opens in Terminal.
    if (SDL_OpenURL(FileUrl(script.string()).c_str())) return true;
    err = std::string("Could not open update.command: ") + SDL_GetError();
    return false;
#endif
}

std::string UpdateProblem(const UpdateCheck& check) {
    using Kind = UpdateCheck::Kind;
    std::string detail = check.detail;
    while (!detail.empty() && (detail.back() == '.' || detail.back() == ' ')) detail.pop_back();
    switch (check.kind) {
        case Kind::UpToDate:
        case Kind::Behind: return {};
        case Kind::NoGit: return "Git was not found. Install it (see the README) to update from here.";
        case Kind::NoBranch: return "This folder doesn't follow a branch on GitHub.";
        case Kind::FetchFailed: return "Could not reach GitHub: " + (detail.empty() ? std::string("no answer") : detail) + ".";
        case Kind::LocalChanges:
            return "Files in this folder have been changed, so it can't update by itself. See the README's update steps.";
        case Kind::Failed: return "Git could not check for updates: " + (detail.empty() ? std::string("no answer") : detail) + ".";
    }
    return {};
}

}  // namespace tpw::ui
