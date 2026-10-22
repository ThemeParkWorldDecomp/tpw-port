// sys/statvfs.h for Windows: the volume's size and free space that GetDiskFreeSpace (platform/macdoze_system.cpp) asks of
// statvfs, from GetDiskFreeSpaceExW (platform/posix_win.cpp), counted in 4 KB blocks.
#pragma once

struct statvfs {
    unsigned long f_bsize;
    unsigned long f_frsize;          // the unit of the counts below
    unsigned long long f_blocks;     // the volume's size
    unsigned long long f_bfree;      // free
    unsigned long long f_bavail;     // free for this user (quotas)
};

int statvfs(const char* path, struct statvfs* info);      // a UTF-8 path
int statvfs(const wchar_t* path, struct statvfs* info);   // a std::filesystem::path's own string
