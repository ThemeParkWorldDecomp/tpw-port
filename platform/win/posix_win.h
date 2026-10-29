// Windows port contributed by samueleagle91 (tpw-port PR #1)
// posix_win.h: the POSIX calls of the platform layer that MinGW's own headers lack, included ahead of every file of
// tpw_platform on Windows (CMakeLists.txt). The POSIX headers MinGW has no copy of at all are beside this one.
#pragma once

#define _POSIX_THREAD_SAFE_FUNCTIONS 200112L   // MinGW's <time.h> then declares localtime_r

#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>

#include <string>

// The stdio stream lock, under the C runtime's names.
inline void flockfile(FILE* file) { _lock_file(file); }
inline void funlockfile(FILE* file) { _unlock_file(file); }

// A block free() takes back (platform/posix_win.cpp).
extern "C" int posix_memalign(void** block, size_t alignment, size_t size);

// stat of a std::filesystem::path, whose c_str() is UTF-16 on Windows.
inline int stat(const wchar_t* path, struct stat* info) { return wstat(path, info); }

// Windows' conversions of code page `codePage` (platform/code_page.cpp): false when a byte or a character has no
// counterpart.
bool tpw_win_decode(unsigned codePage, const std::string& bytes, std::u16string& text);
bool tpw_win_encode(unsigned codePage, const std::u16string& text, std::string& bytes);
// A host path in the ANSI code page, for the C runtime's narrow fopen (platform/file_handles.cpp): as it is when the code
// page has its letters, else with the short (8.3) name of its folder. False when neither can be written in it.
bool tpw_win_narrow_path(const wchar_t* path, std::string& narrow);
// A file or folder name in the ANSI code page, a letter it lacks as '?' (as Windows' own narrow file calls give it): the
// conversion std::filesystem::path::string() makes, but one that never throws (platform/code_page.cpp).
std::string tpw_win_ansi_name(const wchar_t* name);
