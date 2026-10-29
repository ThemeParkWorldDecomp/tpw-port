// Windows port contributed by samueleagle91 (tpw-port PR #1)
// posix_win.cpp: the POSIX calls the platform layer and the game make that Windows lacks, declared by the headers in
// platform/win/ (dlfcn.h, sys/statvfs.h, pthread/qos.h, posix_win.h).
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>

#include <cerrno>
#include <cstdlib>
#include <string>
#include <vector>

#include <dlfcn.h>
#include <pthread/qos.h>
#include <sys/statvfs.h>

namespace {

// The module an address lies in (the executable or a DLL), or null.
HMODULE ModuleOf(const void* address) {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              static_cast<LPCWSTR>(address), &module)
               ? module
               : nullptr;
}

// The process's modules in load order, the executable first.
std::vector<HMODULE> Modules() {
    std::vector<HMODULE> modules(256);
    DWORD bytes = 0;
    while (EnumProcessModules(GetCurrentProcess(), modules.data(), static_cast<DWORD>(modules.size() * sizeof(HMODULE)),
                              &bytes) &&
           bytes > modules.size() * sizeof(HMODULE)) {
        modules.resize(bytes / sizeof(HMODULE));
    }
    modules.resize(bytes / sizeof(HMODULE));
    return modules;
}

std::wstring Wide(const char* text) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    std::wstring wide(length > 0 ? static_cast<size_t>(length) : 1, L'\0');
    if (length > 0) MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), length);
    return wide;
}

}  // namespace

// RTLD_DEFAULT searches every module; RTLD_NEXT every one but the caller's, so that the launcher's own definition of a
// system function (platform/offscreen.cpp's glDrawBuffer) can reach the system's.
extern "C" void* dlsym(void* handle, const char* name) {
    if (handle != RTLD_DEFAULT && handle != RTLD_NEXT)
        return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name));
    const HMODULE caller = handle == RTLD_NEXT ? ModuleOf(__builtin_return_address(0)) : nullptr;
    for (HMODULE module : Modules()) {
        if (module == caller) continue;
        if (FARPROC function = GetProcAddress(module, name)) return reinterpret_cast<void*>(function);
    }
    return nullptr;
}

extern "C" int dladdr(const void* address, Dl_info* info) {
    const HMODULE module = ModuleOf(address);
    if (module == nullptr) return 0;
    static thread_local char path[MAX_PATH * 3];
    wchar_t wide[MAX_PATH];
    const DWORD length = GetModuleFileNameW(module, wide, MAX_PATH);
    if (length == 0 || length == MAX_PATH ||
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, path, sizeof path, nullptr, nullptr) == 0) {
        path[0] = '\0';
    }
    info->dli_fname = path;
    info->dli_fbase = module;
    info->dli_sname = nullptr;
    info->dli_saddr = nullptr;
    return 1;
}

int statvfs(const wchar_t* path, struct statvfs* info) {
    ULARGE_INTEGER available, total, free;
    if (!GetDiskFreeSpaceExW(path, &available, &total, &free)) {
        errno = ENOENT;
        return -1;
    }
    constexpr unsigned long kBlock = 4096;
    *info = {};
    info->f_bsize = info->f_frsize = kBlock;
    info->f_blocks = total.QuadPart / kBlock;
    info->f_bfree = free.QuadPart / kBlock;
    info->f_bavail = available.QuadPart / kBlock;
    return 0;
}

int statvfs(const char* path, struct statvfs* info) { return statvfs(Wide(path).c_str(), info); }

extern "C" int pthread_set_qos_class_self_np(qos_class_t qos, int) {
    // Entering background mode twice, or leaving it outside, is refused, and leaves the thread in the mode asked for.
    SetThreadPriority(GetCurrentThread(), qos == QOS_CLASS_BACKGROUND ? THREAD_MODE_BACKGROUND_BEGIN : THREAD_MODE_BACKGROUND_END);
    return 0;
}

// malloc's blocks are 16-byte aligned on x86-64, the most anything asks for (the OpenGL textures' buffer,
// src/engine/OpenGLTexture.cpp); a larger alignment would need _aligned_malloc, whose blocks free() cannot take back.
extern "C" int posix_memalign(void** block, size_t alignment, size_t size) {
    if (alignment > 16 || (alignment & (alignment - 1)) != 0 || alignment % sizeof(void*) != 0) return EINVAL;
    *block = std::malloc(size);
    return *block != nullptr || size == 0 ? 0 : ENOMEM;
}

bool tpw_win_decode(unsigned codePage, const std::string& bytes, std::u16string& text) {
    text.clear();
    if (bytes.empty()) return true;
    const int count = static_cast<int>(bytes.size());
    const int length = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, bytes.data(), count, nullptr, 0);
    if (length <= 0) return false;
    std::wstring wide(static_cast<size_t>(length), L'\0');
    if (MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, bytes.data(), count, wide.data(), length) != length) return false;
    text.assign(wide.begin(), wide.end());
    return true;
}

bool tpw_win_encode(unsigned codePage, const std::u16string& text, std::string& bytes) {
    bytes.clear();
    if (text.empty()) return true;
    const std::wstring wide(text.begin(), text.end());
    const int count = static_cast<int>(wide.size());
    BOOL usedDefault = FALSE;
    const int size = WideCharToMultiByte(codePage, WC_NO_BEST_FIT_CHARS, wide.data(), count, nullptr, 0, nullptr, &usedDefault);
    if (size <= 0 || usedDefault) return false;
    bytes.assign(static_cast<size_t>(size), '\0');
    return WideCharToMultiByte(codePage, WC_NO_BEST_FIT_CHARS, wide.data(), count, bytes.data(), size, nullptr, &usedDefault) ==
               size &&
           !usedDefault;
}

namespace {
// `wide` in the ANSI code page, with no character lost or replaced by a look-alike.
bool Ansi(const std::wstring& wide, std::string& narrow) {
    const bool utf8 = GetACP() == CP_UTF8;   // which takes no default character
    BOOL usedDefault = FALSE;
    const DWORD flags = utf8 ? 0 : WC_NO_BEST_FIT_CHARS;
    BOOL* used = utf8 ? nullptr : &usedDefault;
    const int size = WideCharToMultiByte(CP_ACP, flags, wide.c_str(), -1, nullptr, 0, nullptr, used);
    if (size <= 0 || usedDefault) return false;
    narrow.assign(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(CP_ACP, flags, wide.c_str(), -1, narrow.data(), size, nullptr, used) != size || usedDefault) return false;
    narrow.resize(static_cast<size_t>(size - 1));   // without the NUL
    return true;
}
}  // namespace

bool tpw_win_narrow_path(const wchar_t* path, std::string& narrow) {
    const std::wstring wide(path);
    if (Ansi(wide, narrow)) return true;
    // The folder by its short name (it exists: the file itself may not yet), then the file's own name.
    const size_t cut = wide.find_last_of(L"\\/");
    if (cut == std::wstring::npos) return false;
    const std::wstring folder = wide.substr(0, cut);
    const DWORD length = GetShortPathNameW(folder.c_str(), nullptr, 0);
    if (length == 0) return false;
    std::wstring shortFolder(length, L'\0');
    const DWORD written = GetShortPathNameW(folder.c_str(), shortFolder.data(), length);
    if (written == 0 || written >= length) return false;
    shortFolder.resize(written);
    return Ansi(shortFolder + wide.substr(cut), narrow);
}

std::string tpw_win_ansi_name(const wchar_t* name) {
    const bool utf8 = GetACP() == CP_UTF8;   // which takes no default character
    const DWORD flags = utf8 ? 0 : WC_NO_BEST_FIT_CHARS;
    const char* fallback = utf8 ? nullptr : "?";
    const int size = WideCharToMultiByte(CP_ACP, flags, name, -1, nullptr, 0, fallback, nullptr);
    if (size <= 0) return std::string();
    std::string narrow(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(CP_ACP, flags, name, -1, narrow.data(), size, fallback, nullptr) != size) return std::string();
    narrow.resize(static_cast<size_t>(size - 1));   // without the NUL
    return narrow;
}

#endif
