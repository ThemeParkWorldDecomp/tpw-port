// game_app_win.cpp: the Windows part of game_app.h: the game's program beside tpw_launcher.exe (a copy of the launcher's
// with the game's version resource and icon, KeepGameExe), started as a process of its own; the game process's
// AppUserModelID and its window's property store; tpw_launcher.exe started again from the game at its exit, and from the
// game's program started without --play.
#include "game_app.h"

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <propsys.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "app_icon.h"
#include "host_win.h"

// platform.h is not included (the game's headers define Win32 names themselves, as host_win.h says): only the log line.
namespace tpw {
void Log(const char* format, ...) __attribute__((format(printf, 1, 2)));
}

namespace tpw::launcher {

namespace {

namespace fs = std::filesystem;

constexpr const wchar_t* kLauncherExe = L"tpw_launcher.exe";
constexpr const char* kNoteVariable = "TPW_APP_BUNDLE_NOTE";   // app_bundle.cpp TakeBundleNote reads it
// The resource types, wide whether or not UNICODE is defined (RT_VERSION and the others follow it).
const LPCWSTR kVersionType = MAKEINTRESOURCEW(16);
const LPCWSTR kGroupIconType = MAKEINTRESOURCEW(14);
const LPCWSTR kIconType = MAKEINTRESOURCEW(3);

std::wstring Wide(const std::string& text, UINT codePage = CP_UTF8) {
    const int length = MultiByteToWideChar(codePage, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(length > 0 ? length : 0), L'\0');
    if (length > 0) MultiByteToWideChar(codePage, 0, text.c_str(), static_cast<int>(text.size()), out.data(), length);
    return out;
}

std::string Utf8(const std::wstring& text) {
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(length > 0 ? length : 0), '\0');
    if (length > 0) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), length, nullptr, nullptr);
    return out;
}

std::string SystemMessage(DWORD code) {
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                   reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::string message = text != nullptr ? Utf8(text) : "";
    if (text != nullptr) LocalFree(text);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r' || message.back() == '.')) message.pop_back();
    char code8[24];
    std::snprintf(code8, sizeof code8, "error %lu", static_cast<unsigned long>(code));
    return message.empty() ? code8 : message + " (" + code8 + ")";
}

std::wstring OwnExe() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) return L"";
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

// Starts `exe` with `args` (in the C runtime's code page, as main's argv are) in this process's folder, as a process of
// its own. Its standard streams are this process's when it has a console or a redirection of its own; none when this
// process's go into the log file through CaptureOutputToLog's pipe, which ends with this process (the game then captures
// its own into the same log file, main.cpp LauncherMain). `front` lets it take the foreground from this process.
bool StartProcess(const std::wstring& exe, const std::vector<std::string>& args, bool front, PROCESS_INFORMATION& info,
                  std::string& why) {
    std::wstring line = L"\"" + exe + L"\"";
    if (!args.empty()) line += L" " + Wide(WindowsCommandLine(args), CP_ACP);
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    HANDLE handles[3] = {};
    BOOL inherit = FALSE;
    if (!tpw::win32::LoggingToFile() && tpw::win32::HasStandardError()) {
        const DWORD which[3] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
        for (int i = 0; i < 3; ++i) {
            const HANDLE own = GetStdHandle(which[i]);
            if (own != nullptr && own != INVALID_HANDLE_VALUE &&
                DuplicateHandle(GetCurrentProcess(), own, GetCurrentProcess(), &handles[i], 0, TRUE, DUPLICATE_SAME_ACCESS))
                inherit = TRUE;
        }
    }
    startup.hStdInput = handles[0];
    startup.hStdOutput = handles[1];
    startup.hStdError = handles[2];
    info = PROCESS_INFORMATION{};
    const BOOL started = CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, inherit, 0, nullptr, nullptr, &startup, &info);
    const DWORD error = GetLastError();
    for (HANDLE handle : handles)
        if (handle != nullptr) CloseHandle(handle);
    if (!started) {
        why = "cannot start " + Utf8(exe) + ": " + SystemMessage(error);
        return false;
    }
    CloseHandle(info.hThread);
    info.hThread = nullptr;
    if (front) AllowSetForegroundWindow(info.dwProcessId);
    return true;
}

bool ResourceBytes(HMODULE module, LPCWSTR type, LPCWSTR name, std::vector<uint8_t>& out) {
    const HRSRC found = FindResourceW(module, name, type);
    const HGLOBAL loaded = found != nullptr ? LoadResource(module, found) : nullptr;
    const DWORD size = found != nullptr ? SizeofResource(module, found) : 0;
    const void* bytes = loaded != nullptr ? LockResource(loaded) : nullptr;
    if (bytes == nullptr || size == 0) return false;
    out.assign(static_cast<const uint8_t*>(bytes), static_cast<const uint8_t*>(bytes) + size);
    return true;
}

// The version resource of the program at `path`, read without running it; whether it has the game's icon group too.
bool ReadProgram(const std::wstring& path, VersionResource& version, bool& icon) {
    const HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (module == nullptr) return false;
    std::vector<uint8_t> bytes;
    const bool ok = ResourceBytes(module, kVersionType, MAKEINTRESOURCEW(VS_VERSION_INFO), bytes) && ParseVersionResource(bytes, version);
    icon = FindResourceW(module, MAKEINTRESOURCEW(1), kGroupIconType) != nullptr;
    FreeLibrary(module);
    return ok;
}

// What a game program made from the launcher's program at `launcher` and the icon `ico` carries (kGameExeStampKey): the
// program's size and time, and the icon's FNV-1a hash. Another launcher (an update) or another icon (an import) makes
// the program again.
std::string Stamp(const std::wstring& launcher, const std::vector<uint8_t>& ico) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    GetFileAttributesExW(launcher.c_str(), GetFileExInfoStandard, &data);
    const unsigned long long size = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    const unsigned long long time =
        (static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
    unsigned long long hash = 14695981039346656037ull;
    for (const uint8_t byte : ico) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    char text[128];
    std::snprintf(text, sizeof text, "tpw_launcher.exe of %llu bytes written at %llu, icon %016llx", size, time, hash);
    return text;
}

// A resource of the launcher's program, to be taken out of the copy: its type, name (a number, or text) and language.
struct ResourceName {
    LPCWSTR type;
    std::wstring text;
    WORD id = 0;
    WORD language = 0;
    LPCWSTR Name() const { return text.empty() ? MAKEINTRESOURCEW(id) : text.c_str(); }
};

BOOL CALLBACK AddLanguage(HMODULE, LPCWSTR type, LPCWSTR name, WORD language, LONG_PTR list) {
    ResourceName entry;
    entry.type = type;
    if (IS_INTRESOURCE(name)) entry.id = static_cast<WORD>(reinterpret_cast<uintptr_t>(name));
    else entry.text = name;
    entry.language = language;
    reinterpret_cast<std::vector<ResourceName>*>(list)->push_back(entry);
    return TRUE;
}

BOOL CALLBACK AddName(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR list) {
    EnumResourceLanguagesW(module, type, name, AddLanguage, list);
    return TRUE;
}

// Writes `version` and `icon` into the program at `path` in place of its version resource and every icon it has.
bool PutResources(const std::wstring& path, const std::vector<uint8_t>& version, const IconResources& icon, std::string& why) {
    std::vector<ResourceName> old;
    const HMODULE own = GetModuleHandleW(nullptr);   // the launcher's program, which `path` is a copy of
    for (LPCWSTR type : {kVersionType, kGroupIconType, kIconType})
        EnumResourceNamesW(own, type, AddName, reinterpret_cast<LONG_PTR>(&old));
    WORD language = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
    for (const ResourceName& entry : old)
        if (entry.type == kVersionType) language = entry.language;
    const HANDLE update = BeginUpdateResourceW(path.c_str(), FALSE);
    if (update == nullptr) {
        why = "cannot open its resources: " + SystemMessage(GetLastError());
        return false;
    }
    bool ok = true;
    for (const ResourceName& entry : old) ok = ok && UpdateResourceW(update, entry.type, entry.Name(), entry.language, nullptr, 0);
    ok = ok && UpdateResourceW(update, kVersionType, MAKEINTRESOURCEW(VS_VERSION_INFO), language, const_cast<uint8_t*>(version.data()),
                               static_cast<DWORD>(version.size()));
    for (size_t i = 0; ok && i < icon.images.size(); ++i)
        ok = UpdateResourceW(update, kIconType, MAKEINTRESOURCEW(1 + i), language, const_cast<uint8_t*>(icon.images[i].data()),
                             static_cast<DWORD>(icon.images[i].size()));
    ok = ok && UpdateResourceW(update, kGroupIconType, MAKEINTRESOURCEW(1), language, const_cast<uint8_t*>(icon.group.data()),
                               static_cast<DWORD>(icon.group.size()));
    if (!ok) {
        why = "cannot change its resources: " + SystemMessage(GetLastError());
        EndUpdateResourceW(update, TRUE);
        return false;
    }
    if (!EndUpdateResourceW(update, FALSE)) {
        why = "cannot write its resources: " + SystemMessage(GetLastError());
        return false;
    }
    return true;
}

// Makes `exe` the launcher's program under the game's name, or keeps it when it was made from this launcher and this
// icon already: a copy of tpw_launcher.exe written beside it as .part, its version resource the game's
// (GameVersionResource) and its icon the game's (icon.png, or the stand-in), read back, then put in place. When its
// resources cannot be changed it is a plain copy, which the log says (the Task Manager's Processes tab then names the
// launcher; the taskbar still has the game's window apart, with the game's icon). `made` says what was done.
bool KeepGameExe(const std::wstring& launcher, const std::wstring& exe, bool us, const std::string& dataFolder, std::string& made,
                 std::string& why) {
    bool ownIcon = false;
    std::string source;
    tpw::assets::IconImage picture = LoadAppIcon(dataFolder, ownIcon, source);
    if (picture.width > 256 || picture.height > 256) picture = ResampleIcon(picture, 256);
    const std::vector<uint8_t> ico = tpw::assets::EncodeIco(picture);
    IconResources icon;
    const bool haveIcon = IconResourcesFromIco(ico, 1, icon);
    const std::string stamp = Stamp(launcher, ico);
    const char* iconWord = !haveIcon ? "no icon" : ownIcon ? "the game's icon" : "the stand-in icon";
    std::error_code ec;
    const bool there = fs::exists(fs::path(exe), ec);
    VersionResource current;
    bool currentIcon = false;
    if (there && ReadProgram(exe, current, currentIcon) && VersionString(current, kGameExeStampKey) == stamp) {
        made = std::string("kept (made from this launcher with ") + iconWord + ")";
        return true;
    }
    const std::wstring part = exe + L".part";
    if (!CopyFileW(launcher.c_str(), part.c_str(), FALSE)) {
        why = "cannot copy tpw_launcher.exe to " + Utf8(part) + ": " + SystemMessage(GetLastError());
        return false;
    }
    std::vector<uint8_t> launcherVersion;
    VersionResource base;
    if (ResourceBytes(GetModuleHandleW(nullptr), kVersionType, MAKEINTRESOURCEW(VS_VERSION_INFO), launcherVersion))
        ParseVersionResource(launcherVersion, base);
    std::string plain;   // why the copy kept the launcher's resources, when it did
    if (!haveIcon) {
        plain = "no icon to put in it";
    } else if (!PutResources(part, EncodeVersionResource(GameVersionResource(us, base, stamp)), icon, plain)) {
        if (!CopyFileW(launcher.c_str(), part.c_str(), FALSE)) {   // the update may have left it half written
            why = "cannot copy tpw_launcher.exe to " + Utf8(part) + ": " + SystemMessage(GetLastError());
            DeleteFileW(part.c_str());
            return false;
        }
    } else {
        VersionResource written;
        bool writtenIcon = false;
        if (!ReadProgram(part, written, writtenIcon) || VersionString(written, kGameExeStampKey) != stamp ||
            VersionString(written, "FileDescription") != GameAppName(us) || !writtenIcon)
            plain = "its resources did not read back as written";
    }
    if (!MoveFileExW(part.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD error = GetLastError();
        DeleteFileW(part.c_str());
        if (!there) {
            why = "cannot rename " + Utf8(part) + ": " + SystemMessage(error);
            return false;
        }
        made = "left as it was (cannot replace it: " + SystemMessage(error) + ")";   // a game still running from it
        return true;
    }
    made = std::string(there ? "brought up to date" : "made") + " with the game's name and " + iconWord;
    if (!plain.empty()) made = std::string(there ? "brought up to date" : "made") + " as a plain copy of tpw_launcher.exe (" + plain +
                               "; the Task Manager's Processes tab names the launcher)";
    return true;
}

std::string WorkingFolder() {
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    return ec ? "" : cwd.string();   // in the C runtime's code page, as argv's are (StartProcess)
}

struct WindowSearch {
    DWORD pid = 0;
    bool found = false;
};

BOOL CALLBACK FindShownWindow(HWND window, LPARAM data) {
    WindowSearch* search = reinterpret_cast<WindowSearch*>(data);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != search->pid || !IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr) return TRUE;
    search->found = true;
    return FALSE;
}

// The window's property store keys (propkey.h's, written out: no GUID library is linked for them).
constexpr GUID kAppUserModel = {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}};
constexpr PROPERTYKEY kRelaunchCommand = {kAppUserModel, 2};
constexpr PROPERTYKEY kRelaunchIconResource = {kAppUserModel, 3};
constexpr PROPERTYKEY kRelaunchDisplayName = {kAppUserModel, 4};
constexpr PROPERTYKEY kAppId = {kAppUserModel, 5};
constexpr GUID kIPropertyStore = {0x886D8EEB, 0x8CF2, 0x4446, {0x8D, 0x02, 0xCD, 0xBA, 0x1D, 0xBD, 0xCF, 0x99}};

HRESULT SetText(IPropertyStore* store, const PROPERTYKEY& key, const std::wstring& text) {
    PROPVARIANT value;
    PropVariantInit(&value);
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    value.pwszVal = static_cast<wchar_t*>(CoTaskMemAlloc(bytes));
    if (value.pwszVal == nullptr) return E_OUTOFMEMORY;
    std::memcpy(value.pwszVal, text.c_str(), bytes);
    value.vt = VT_LPWSTR;
    const HRESULT result = store->SetValue(key, value);
    PropVariantClear(&value);
    return result;
}

}  // namespace

bool StartGameApp(int argc, char** argv, bool us, const std::string& dataFolder, bool activate, GameApp& app, std::string& why) {
    if (!CurrentGameBundle().empty()) {
        why = "this process runs from the game's program, not tpw_launcher.exe";
        return false;
    }
    const std::wstring launcher = OwnExe();
    if (launcher.empty()) {
        why = "the program's own path is unknown";
        return false;
    }
    const std::wstring exe = (fs::path(launcher).parent_path() / Wide(GameExeName(us))).wstring();
    if (!KeepGameExe(launcher, exe, us, dataFolder, app.made, why)) return false;
    app.bundle = Utf8(exe);
    PROCESS_INFORMATION info{};
    if (!StartProcess(exe, GameArguments(argc, argv, us, WorkingFolder()), activate, info, why)) return false;
    app.pid = static_cast<int>(info.dwProcessId);
    app.running = info.hProcess;
    return true;
}

bool GameAppFinishedLaunching(const GameApp& app) {
    if (app.running == nullptr || WaitForSingleObject(static_cast<HANDLE>(app.running), 0) == WAIT_OBJECT_0) return true;
    WindowSearch search;
    search.pid = static_cast<DWORD>(app.pid);
    EnumWindows(FindShownWindow, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

void ReleaseGameApp(GameApp& app) {
    if (app.running != nullptr) CloseHandle(static_cast<HANDLE>(app.running));
    app.running = nullptr;
}

std::string CurrentGameBundle() {
    const std::wstring exe = OwnExe();
    const std::wstring name = fs::path(exe).filename().wstring();
    for (const bool us : {true, false})
        if (!name.empty() && CompareStringOrdinal(name.c_str(), -1, Wide(GameExeName(us)).c_str(), -1, TRUE) == CSTR_EQUAL)
            return Utf8(exe);
    return "";
}

bool OpenLauncherApp(int argc, char** argv, const std::string& dataFolder, std::string& why) {
    (void)dataFolder;   // tpw_launcher.exe carries its icon itself (app_icon.h)
    const std::wstring launcher = (fs::path(OwnExe()).parent_path() / kLauncherExe).wstring();
    PROCESS_INFORMATION info{};
    if (!StartProcess(launcher, LauncherArguments(argc, argv), true, info, why)) return false;
    CloseHandle(info.hProcess);
    tpw::Log("launcher: opened again as %s (process %lu; Settings: back to the launcher after playing)", Utf8(launcher).c_str(),
             static_cast<unsigned long>(info.dwProcessId));
    return true;
}

void NameGameProcess(bool us) {
    const std::string id = GameAppId(us);
    const HRESULT result = SetCurrentProcessExplicitAppUserModelID(Wide(id).c_str());
    if (SUCCEEDED(result))
        tpw::Log("app id: %s (the game's own, so the taskbar keeps its window apart from the launcher's)", id.c_str());
    else
        tpw::Log("app id: not set (0x%08lx); the taskbar groups the game by its program, %s", static_cast<unsigned long>(result),
                 GameExeName(us).c_str());
}

void TagGameWindow(SDL_Window* window, bool us) {
    const HWND hwnd = window != nullptr ? static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                                                                    SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr))
                                        : nullptr;
    if (hwnd == nullptr) {
        tpw::Log("taskbar: the game's window not tagged (no window handle)");
        return;
    }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IPropertyStore* store = nullptr;
    HRESULT result = SHGetPropertyStoreForWindow(hwnd, kIPropertyStore, reinterpret_cast<void**>(&store));
    const std::wstring exe = OwnExe();
    // Pinned, the game's button starts the game's program, which without --play starts the launcher (HandOverToLauncher),
    // as the game's bundle kept in the Dock does on a Mac.
    if (SUCCEEDED(result)) result = SetText(store, kAppId, Wide(GameAppId(us)));
    if (SUCCEEDED(result)) result = SetText(store, kRelaunchCommand, L"\"" + exe + L"\"");
    if (SUCCEEDED(result)) result = SetText(store, kRelaunchDisplayName, Wide(GameAppName(us)));
    if (SUCCEEDED(result)) result = SetText(store, kRelaunchIconResource, exe + L",-1");
    if (SUCCEEDED(result)) result = store->Commit();
    if (store != nullptr) store->Release();
    if (SUCCEEDED(com)) CoUninitialize();
    if (SUCCEEDED(result))
        tpw::Log("taskbar: the game's window is %s's (ID %s; pinned, it starts %s)", GameAppName(us), GameAppId(us).c_str(),
                 Utf8(exe).c_str());
    else
        tpw::Log("taskbar: the game's window not tagged (0x%08lx)", static_cast<unsigned long>(result));
}

void HandOverToLauncher(int argc, char** argv, std::string& note) {
    const std::wstring exe = OwnExe();
    const std::wstring launcher = (fs::path(exe).parent_path() / kLauncherExe).wstring();
    const std::string left = "started from " + Utf8(fs::path(exe).filename().wstring()) +
                             " without --play: the launcher runs from tpw_launcher.exe, as the game's program is the game's";
    SetEnvironmentVariableW(Wide(kNoteVariable).c_str(), Wide(left).c_str());
    PROCESS_INFORMATION info{};
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (StartProcess(launcher, args, true, info, note)) {
        CloseHandle(info.hProcess);
        std::exit(0);
    }
    SetEnvironmentVariableW(Wide(kNoteVariable).c_str(), nullptr);
    note = "the game's program, without --play, runs the launcher itself: " + note;
}

}  // namespace tpw::launcher
#endif
