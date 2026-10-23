// Windows port contributed by samueleagle91 (tpw-port PR #1)
// posix_win.h: the POSIX calls of the platform layer that MinGW's own headers lack, included ahead of every file of
// tpw_platform on Windows (CMakeLists.txt). The POSIX headers MinGW has no copy of at all are beside this one.
#pragma once

#define _POSIX_THREAD_SAFE_FUNCTIONS 200112L   // MinGW's <time.h> then declares localtime_r

#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>

// The stdio stream lock, under the C runtime's names.
inline void flockfile(FILE* file) { _lock_file(file); }
inline void funlockfile(FILE* file) { _unlock_file(file); }

// A block free() takes back (platform/posix_win.cpp).
extern "C" int posix_memalign(void** block, size_t alignment, size_t size);

// stat of a std::filesystem::path, whose c_str() is UTF-16 on Windows.
inline int stat(const wchar_t* path, struct stat* info) { return wstat(path, info); }
