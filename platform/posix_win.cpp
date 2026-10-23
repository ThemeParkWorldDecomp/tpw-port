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
#endif
