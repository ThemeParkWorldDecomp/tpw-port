// Windows port contributed by samueleagle91 (tpw-port PR #1)
// host_win.h: the Win32 calls the launcher needs, behind standard types. <windows.h> stays in host_win.cpp because the
// game's headers define many of its names themselves (the MacDoze shims: GetLocalTime, _SYSTEMTIME, the Winsock calls).
// host_win.cpp also defines the OpenGL extension calls that opengl32.dll does not export.
#pragma once
#ifdef _WIN32

#include <string>

namespace tpw::win32 {

// Integer division as the Mac's processors do it, for the whole process: x86 traps a division by zero and INT_MIN / -1,
// where AArch64 (and the PowerPC before it) answers a value and goes on, and the translation relies on that (the
// mouse's position is divided by a screen size that is 0 while a park loads). A trapped div or idiv gets AArch64's
// answer: by zero, a quotient of 0 and the dividend as the remainder; INT_MIN / -1, INT_MIN and a remainder of 0.
void DivideLikeTheMac();

// Keeps a description of the last access violation, illegal instruction or stack overflow: its code, the address and the
// call stack, each frame as a module and an offset (llvm-symbolizer --obj tpw_launcher.exe 0x140000000+offset names the
// exe's). The crash report prints it (LastFatalException); Windows has no backtrace() and its own record never comes.
void RecordFatalExceptions();
const char* LastFatalException();

// Whether whoever started the launcher gave it somewhere to write: a console, or a redirection. A Windows app started
// from Explorer has neither, and its log would go nowhere.
bool HasStandardError();

// Sends stdout and stderr into the open log file (platform/log_file.h, logs/launcher.log), unbuffered so that a crash
// leaves it complete: what SDL, the C runtime and the game print goes in with the log's own lines. What is written is
// scrubbed first (platform/log_scrub.h): a thread moves it, one whole line per write, from a pipe to the file. The lines
// the log file takes anyway are then not printed too (log_file.h gStderrIsLogFile). False, changing nothing, when no log
// file is open or the pipe cannot be made.
bool CaptureOutputToLog();
// Whether CaptureOutputToLog is in effect: stdout and stderr are that pipe, which ends with this process. A process this
// one starts gets no standard streams then, and captures its own into the same log file (launcher/game_app.h, the game's
// process).
bool LoggingToFile();
// Writes what is still in that pipe to the file (the crash report, before the process ends; also at exit). No effect when
// CaptureOutputToLog did not run.
void DrainLog();

}  // namespace tpw::win32

#endif
