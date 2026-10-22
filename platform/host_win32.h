// host_win32.h: the few Win32 calls the platform layer needs, behind standard types. <windows.h> stays in
// host_win32.cpp because the game's headers define many of its names themselves (the MacDoze shims: GetLocalTime,
// _SYSTEMTIME, the Winsock calls).
#pragma once
#ifdef _WIN32

namespace tpw::win32 {

// Whether the address lies inside a loaded image (the exe or a DLL): what dladdr answers on the Mac.
bool InLoadedImage(const void* address);

// The system OpenGL's own entry point of that name (opengl32.dll), past any the executable defines itself.
void* SystemGLProc(const char* name);

}  // namespace tpw::win32

#endif
