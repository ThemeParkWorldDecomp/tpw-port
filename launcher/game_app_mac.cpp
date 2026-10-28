// game_app_mac.cpp: the macOS part of game_app.h: the game's bundle beside the launcher's, opened through NSWorkspace
// as an application of its own, and the launcher's bundle opened again from the game at its exit. Everything goes
// through the Objective-C runtime (objc_msgSend), as main.cpp NameAppMenu and app_icon.cpp do: the tree has no
// Objective-C file. The completion handler is a block (clang's, -fblocks; CMakeLists.txt sets it for this file).
#include "game_app.h"

#if defined(__APPLE__)
#include <dispatch/dispatch.h>
#include <objc/message.h>
#include <objc/objc.h>
#include <objc/runtime.h>
#include <unistd.h>

#include <cstring>
#include <filesystem>

#include "app_bundle.h"
#include "app_icon.h"

// platform.h is not included (its game headers clash with Apple's MacTypes.h, as app_icon.cpp says): only the log line.
namespace tpw {
void Log(const char* format, ...) __attribute__((format(printf, 1, 2)));
}

extern char** environ;
extern "C" void* objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void* pool);

namespace tpw::launcher {

namespace {

namespace fs = std::filesystem;
using Id = void*;
using SendId = Id (*)(Id, SEL);
using SendVoidId = void (*)(Id, SEL, Id);
using SendVoidIdId = void (*)(Id, SEL, Id, Id);
using SendVoidBool = void (*)(Id, SEL, BOOL);
using SendBool = BOOL (*)(Id, SEL);
using SendInt = int (*)(Id, SEL);
using SendText = Id (*)(Id, SEL, const char*);
using SendUtf8 = const char* (*)(Id, SEL);
using SendOpen = void (*)(Id, SEL, Id, Id, Id);

Id Class(const char* name) { return reinterpret_cast<Id>(objc_getClass(name)); }
Id Send(Id to, const char* selector) { return reinterpret_cast<SendId>(objc_msgSend)(to, sel_registerName(selector)); }
Id Str(const std::string& text) {
    return reinterpret_cast<SendText>(objc_msgSend)(Class("NSString"), sel_registerName("stringWithUTF8String:"), text.c_str());
}
const char* Utf8(Id string) { return string != nullptr ? reinterpret_cast<SendUtf8>(objc_msgSend)(string, sel_registerName("UTF8String")) : nullptr; }

// What the completion handler leaves: the application, or why there is none.
struct Opened {
    Id app = nullptr;
    std::string error;
    dispatch_semaphore_t done = nullptr;
};

// [[NSWorkspace sharedWorkspace] openApplicationAtURL:configuration:completionHandler:] with `args` and this process's
// environment, waited for (up to a minute): a new process of `app`, activated when `activate`. `running` is its
// NSRunningApplication, retained for the caller.
bool OpenApplication(const std::string& app, const std::vector<std::string>& args, bool activate, Id& running, int& pid, std::string& why) {
    void* pool = objc_autoreleasePoolPush();
    bool ok = false;
    Id url = reinterpret_cast<Id (*)(Id, SEL, Id)>(objc_msgSend)(Class("NSURL"), sel_registerName("fileURLWithPath:"), Str(app));
    Id list = Send(Class("NSMutableArray"), "array");
    for (const std::string& arg : args) reinterpret_cast<SendVoidId>(objc_msgSend)(list, sel_registerName("addObject:"), Str(arg));
    Id environment = Send(Class("NSMutableDictionary"), "dictionary");
    for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
        const char* equals = std::strchr(*entry, '=');
        if (equals == nullptr || equals == *entry) continue;
        reinterpret_cast<SendVoidIdId>(objc_msgSend)(environment, sel_registerName("setObject:forKey:"), Str(equals + 1),
                                                     Str(std::string(*entry, static_cast<size_t>(equals - *entry))));
    }
    Id configuration = Send(Class("NSWorkspaceOpenConfiguration"), "configuration");
    Id workspace = Send(Class("NSWorkspace"), "sharedWorkspace");
    if (url == nullptr || configuration == nullptr || workspace == nullptr) {
        why = "no NSWorkspace open configuration (macOS 10.15 or newer has one)";
        objc_autoreleasePoolPop(pool);
        return false;
    }
    reinterpret_cast<SendVoidId>(objc_msgSend)(configuration, sel_registerName("setArguments:"), list);
    reinterpret_cast<SendVoidId>(objc_msgSend)(configuration, sel_registerName("setEnvironment:"), environment);
    reinterpret_cast<SendVoidBool>(objc_msgSend)(configuration, sel_registerName("setActivates:"), activate ? YES : NO);
    reinterpret_cast<SendVoidBool>(objc_msgSend)(configuration, sel_registerName("setCreatesNewApplicationInstance:"), YES);
    Opened* opened = new Opened;
    opened->done = dispatch_semaphore_create(0);
    void (^handler)(Id, Id) = ^(Id launched, Id error) {
        if (launched != nullptr) {
            opened->app = Send(launched, "retain");
        } else {
            const char* text = error != nullptr ? Utf8(Send(error, "localizedDescription")) : nullptr;
            opened->error = text != nullptr ? text : "macOS gave no application and no reason";
        }
        dispatch_semaphore_signal(opened->done);
    };
    reinterpret_cast<SendOpen>(objc_msgSend)(workspace, sel_registerName("openApplicationAtURL:configuration:completionHandler:"), url,
                                             configuration, reinterpret_cast<Id>(handler));
    if (dispatch_semaphore_wait(opened->done, dispatch_time(DISPATCH_TIME_NOW, 60 * NSEC_PER_SEC)) != 0) {
        why = "macOS gave no answer within a minute";   // `opened` stays: the handler may still come
    } else {
        if (opened->app != nullptr) {
            running = opened->app;
            pid = reinterpret_cast<SendInt>(objc_msgSend)(running, sel_registerName("processIdentifier"));
            ok = true;
        } else {
            why = opened->error;
        }
        dispatch_release(opened->done);
        delete opened;
    }
    objc_autoreleasePoolPop(pool);
    return ok;
}

std::string WorkingFolder() {
    char cwd[PATH_MAX];
    return getcwd(cwd, sizeof cwd) != nullptr ? cwd : "";
}

}  // namespace

bool StartGameApp(int argc, char** argv, bool us, const std::string& dataFolder, bool activate, GameApp& app, std::string& why) {
    const std::string exe = ExecutablePath();
    const std::string own = BundleOf(exe);
    if (own.empty() && LauncherApp().empty()) {
        why = "the launcher does not run from its own bundle";
        return false;
    }
    const std::string plist = EmbeddedPlist();
    if (plist.empty()) {
        why = "the program has no Info.plist embedded";
        return false;
    }
    // beside the launcher's bundle in build/; run as the installer's application (app_bundle.h), the program is the
    // bare one in build/ itself, so beside it: never in Applications
    const fs::path bundle = (own.empty() ? fs::path(exe) : fs::path(own)).parent_path() / GameBundleName(us);
    bool ownIcon = false;
    std::string source;
    const std::vector<uint8_t> icns = EncodeIcns(LoadAppIcon(dataFolder, ownIcon, source));
    std::string program;
    if (!KeepBundle(exe, bundle.string(), GamePlist(us, plist), icns, program, app.made, why)) return false;
    app.made += icns.empty() ? ", no icon" : ownIcon ? ", the game's icon" : ", the stand-in icon";
    app.bundle = bundle.string();
    return OpenApplication(app.bundle, GameArguments(argc, argv, us, WorkingFolder()), activate, app.running, app.pid, why);
}

bool GameAppFinishedLaunching(const GameApp& app) {
    if (app.running == nullptr) return true;
    return reinterpret_cast<SendBool>(objc_msgSend)(app.running, sel_registerName("isFinishedLaunching")) ||
           reinterpret_cast<SendBool>(objc_msgSend)(app.running, sel_registerName("isTerminated"));
}

void ReleaseGameApp(GameApp& app) {
    if (app.running != nullptr) Send(app.running, "release");
    app.running = nullptr;
}

std::string CurrentGameBundle() {
    const std::string own = BundleOf(ExecutablePath());
    if (own.empty()) return "";
    const fs::path name = fs::path(own).filename();
    return name == GameBundleName(true) || name == GameBundleName(false) ? own : "";
}

bool OpenLauncherApp(int argc, char** argv, std::string& why) {
    const std::string exe = ExecutablePath();
    const std::string own = BundleOf(exe);
    if (own.empty()) {
        why = "this program does not run from a bundle";
        return false;
    }
    const std::string plist = EmbeddedPlist();
    if (plist.empty()) {
        why = "the program has no Info.plist embedded";
        return false;
    }
    // The installer's application when the launcher ran as it (TPW_LAUNCHER_APP, app_bundle.h) and its program is still
    // this one: the Dock's tile in Applications lights up again. It is the installer's, so it is opened as it is.
    fs::path launcher = LauncherApp();
    if (launcher.empty() || !AppRunsProgram(launcher.string(), exe)) {
        launcher = fs::path(own).parent_path() / kLauncherBundleName;
        std::string program, made;
        if (!KeepBundle(exe, launcher.string(), plist, {}, program, made, why)) return false;
    }
    Id running = nullptr;
    int pid = 0;
    if (!OpenApplication(launcher.string(), LauncherArguments(argc, argv), true, running, pid, why)) return false;
    Send(running, "release");
    tpw::Log("launcher: opened again as %s (process %d; Settings: back to the launcher after playing)", launcher.string().c_str(), pid);
    return true;
}

}  // namespace tpw::launcher
#endif
