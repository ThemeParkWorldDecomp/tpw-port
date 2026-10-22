// host_win32.cpp: see host_win32.h.
#ifdef _WIN32
#include "host_win32.h"

#include <windows.h>

namespace tpw::win32 {

bool InLoadedImage(const void* address) {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              static_cast<LPCWSTR>(address), &module) != 0;
}

void* SystemGLProc(const char* name) {
    static HMODULE gl = LoadLibraryW(L"opengl32.dll");
    return gl != nullptr ? reinterpret_cast<void*>(GetProcAddress(gl, name)) : nullptr;
}

}  // namespace tpw::win32
#endif
