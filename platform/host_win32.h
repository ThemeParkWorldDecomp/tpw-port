// host_win32.h: the few Win32 calls the platform layer needs, behind standard types. <windows.h> stays in
// host_win32.cpp because the game's headers define many of its names themselves (the MacDoze shims: GetLocalTime,
// _SYSTEMTIME, the Winsock calls).
#pragma once
#ifdef _WIN32

#include <string>

namespace tpw::win32 {

// Whether the address lies inside a loaded image (the exe or a DLL): what dladdr answers on the Mac.
bool InLoadedImage(const void* address);

// The system OpenGL's own entry point of that name (opengl32.dll), past any the executable defines itself.
void* SystemGLProc(const char* name);

// Integer division as the Mac's processors do it, for the whole process: x86 traps a division by zero and INT_MIN / -1,
// where AArch64 (and the PowerPC before it) answers a value and goes on, and the translation relies on that (the
// mouse's position is divided by a screen size that is 0 while a park loads). A trapped div or idiv gets AArch64's
// answer: by zero, a quotient of 0 and the dividend as the remainder; INT_MIN / -1, INT_MIN and a remainder of 0.
void DivideLikeTheMac();

// Whether whoever started the launcher gave it somewhere to write: a console, or a redirection. A Windows app started
// from Explorer has neither, and its log would go nowhere.
bool HasStandardError();

// Sends stdout and stderr to the file (UTF-8 path), made new each start, unbuffered so that a crash leaves it complete.
bool LogToFile(const std::string& path);

}  // namespace tpw::win32

#endif
