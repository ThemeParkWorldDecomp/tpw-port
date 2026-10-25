// process.cpp: process.h on SDL's process functions.
#include "process.h"

#include <SDL3/SDL.h>

#include <filesystem>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace tpw::proc {

namespace {
std::vector<const char*> ArgPointers(const std::vector<std::string>& argv) {
    std::vector<const char*> args;
    for (const std::string& arg : argv) args.push_back(arg.c_str());
    args.push_back(nullptr);
    return args;
}

// Asks the program to stop, then kills it if it has not within 3 seconds. Returns its exit code (or -1).
int Stop(SDL_Process* process) {
    int code = -1;
    SDL_KillProcess(process, false);
    for (int waited = 0; waited < 3000; waited += 20) {
        if (SDL_WaitProcess(process, false, &code)) return code;
        SDL_Delay(20);
    }
    SDL_KillProcess(process, true);
    SDL_WaitProcess(process, true, &code);
    return code;
}

// SDL_CreateProcessWithProperties in `workingDir` (process.h, Spawn). Null with SDL's error set when the folder is not
// there or the program could not be started.
SDL_Process* Create(SDL_PropertiesID props, const std::string& workingDir) {
    if (workingDir.empty()) return SDL_CreateProcessWithProperties(props);
    static std::mutex lock;
    std::lock_guard<std::mutex> hold(lock);
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path was = fs::current_path(ec);
    fs::current_path(fs::u8path(workingDir), ec);
    if (ec) {
        SDL_SetError("cannot work in %s: %s", workingDir.c_str(), ec.message().c_str());
        return nullptr;
    }
    SDL_Process* process = SDL_CreateProcessWithProperties(props);
    if (!was.empty()) fs::current_path(was, ec);
    return process;
}

#ifdef _WIN32
// The program and everything it starts, on Windows: ending powershell.exe does not end the cmake and compilers the
// update script started, so Run puts the program in a job and ends the job. Closing the job (Run returning, or this
// process ending) ends what is left in it. Run also starts the program as SDL's "background", the only way to SDL's
// CREATE_NO_WINDOW (without it curl.exe and powershell.exe would each open a console window over the launcher), and
// SDL then reports every exit code as 0, so the code is read here.
struct Tree {
    HANDLE handle = nullptr, job = nullptr;
    explicit Tree(SDL_Process* process) {
        const DWORD pid = static_cast<DWORD>(
            SDL_GetNumberProperty(SDL_GetProcessProperties(process), SDL_PROP_PROCESS_PID_NUMBER, 0));
        // SDL holds the program's handle until SDL_DestroyProcess, so its id cannot be reused before then.
        handle = pid != 0 ? OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
                                        FALSE, pid)
                          : nullptr;
        if (handle == nullptr) return;
        job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (job != nullptr && (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits,
                                                        sizeof limits) ||
                               !AssignProcessToJobObject(job, handle))) {
            CloseHandle(job);
            job = nullptr;
        }
    }
    void End() {
        if (job != nullptr) TerminateJobObject(job, 1);
    }
    // The program's exit code once it has ended (-1 when it cannot be read).
    int ExitCode() const {
        DWORD code = 0;
        if (handle == nullptr || !GetExitCodeProcess(handle, &code) || code == STILL_ACTIVE) return -1;
        return static_cast<int>(code);
    }
    ~Tree() {
        if (job != nullptr) CloseHandle(job);
        if (handle != nullptr) CloseHandle(handle);
    }
};
#else
// On macOS and Linux a script's trap stops what it started (process.h, Run).
struct Tree {
    explicit Tree(SDL_Process*) {}
    void End() {}
};
#endif
}  // namespace

Result Run(const Options& options) {
    Result result;
    if (options.argv.empty()) return result;
    std::vector<const char*> args = ArgPointers(options.argv);
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args.data());
#ifdef _WIN32
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);   // no console window (Tree)
#endif
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    if (options.mergeStderr)
        SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    else
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_Process* process = Create(props, options.workingDir);
    SDL_DestroyProperties(props);
    if (process == nullptr) return result;
    result.started = true;
    Tree tree(process);

    // The pipe is non-blocking (SDL makes it so): read what is there, then look at the clock, the cancel flag and
    // whether the program has ended. A program that ended while something it started still holds the pipe open (a
    // background child) does not keep this waiting: its end is the end.
    SDL_IOStream* out = SDL_GetProcessOutput(process);
    const Uint64 start = SDL_GetTicks();
    std::string partial;
    auto take = [&](const char* data, size_t size) {
        if (options.maxOutputBytes != 0 && result.output.size() + size > options.maxOutputBytes) {
            result.tooMuch = true;
            return;
        }
        result.output.append(data, size);
        if (!options.onLine) return;
        partial.append(data, size);
        size_t at;
        while ((at = partial.find('\n')) != std::string::npos) {
            std::string line = partial.substr(0, at);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            options.onLine(line);
            partial.erase(0, at + 1);
        }
    };
    bool ended = false;
    int code = -1;
    while (true) {
        char buffer[4096];
        const size_t got = out != nullptr ? SDL_ReadIO(out, buffer, sizeof buffer) : 0;
        if (got > 0) {
            take(buffer, got);
        } else if (out == nullptr || SDL_GetIOStatus(out) != SDL_IO_STATUS_NOT_READY) {
            break;   // the end of the output (or an error reading it)
        } else if (ended) {
            break;   // ended, and nothing more to read now
        } else {
            ended = SDL_WaitProcess(process, false, &code);
            if (!ended) SDL_Delay(20);
        }
        if (result.tooMuch) break;
        if (options.cancel != nullptr && options.cancel->load()) {
            result.cancelled = true;
            break;
        }
        if (options.timeoutSeconds > 0 && SDL_GetTicks() - start > static_cast<Uint64>(options.timeoutSeconds) * 1000) {
            result.timedOut = true;
            break;
        }
    }
    if (!ended) ended = SDL_WaitProcess(process, false, &code);
    if (!ended) {
        if (result.tooMuch || result.cancelled || result.timedOut) {
            code = Stop(process);
            tree.End();
        } else {
            SDL_WaitProcess(process, true, &code);   // its output ended: it is ending
        }
    }
    if (options.onLine && !partial.empty() && !result.tooMuch) {
        if (partial.back() == '\r') partial.pop_back();
        options.onLine(partial);
    }
#ifdef _WIN32
    code = tree.ExitCode();   // it has ended by now
#endif
    result.exitCode = code;
    SDL_DestroyProcess(process);
    return result;
}

bool Spawn(const std::vector<std::string>& argv, std::string& err, const std::string& workingDir) {
    if (argv.empty()) {
        err = "nothing to start";
        return false;
    }
    std::vector<const char*> args = ArgPointers(argv);
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args.data());
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_Process* process = Create(props, workingDir);
    SDL_DestroyProperties(props);
    if (process == nullptr) {
        err = SDL_GetError();
        return false;
    }
    SDL_DestroyProcess(process);   // it runs on
    return true;
}

unsigned long CurrentId() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<unsigned long>(getpid());
#endif
}

}  // namespace tpw::proc
