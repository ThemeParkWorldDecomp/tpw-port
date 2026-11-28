#include "packed_pixel.h"

#include <algorithm>
#include <array>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {
// A source buffer that ends at an unreadable page exposes a word read past the last packed pixel.
class GuardedPages {
public:
    GuardedPages() {
#ifdef _WIN32
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        pageSize = info.dwPageSize;
        pages = static_cast<uint8_t*>(VirtualAlloc(nullptr, 2 * pageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!pages) return;
        DWORD oldProtection;
        if (VirtualProtect(pages + pageSize, pageSize, PAGE_NOACCESS, &oldProtection)) end = pages + pageSize;
#else
        const long size = sysconf(_SC_PAGESIZE);
        if (size <= 0) return;
        pageSize = static_cast<size_t>(size);
        void* mapped = mmap(nullptr, 2 * pageSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapped == MAP_FAILED) return;
        pages = static_cast<uint8_t*>(mapped);
        if (mprotect(pages + pageSize, pageSize, PROT_NONE) == 0) end = pages + pageSize;
#endif
    }

    ~GuardedPages() {
        if (!pages) return;
#ifdef _WIN32
        VirtualFree(pages, 0, MEM_RELEASE);
#else
        munmap(pages, 2 * pageSize);
#endif
    }

    uint8_t* end = nullptr;

private:
    size_t pageSize = 0;
    uint8_t* pages = nullptr;
};
}

int main() {
    GuardedPages memory;
    if (!memory.end) {
        std::fputs("Could not create a guard page\n", stderr);
        return 1;
    }

    const std::array<uint8_t, 4> bytes{0x12, 0x34, 0x56, 0x78};
    const std::array<uint32_t, 4> expected{0x12, 0x3412, 0x563412, 0x78563412};
    for (uint32_t count = 1; count <= bytes.size(); ++count) {
        uint8_t* source = memory.end - count;
        std::copy_n(bytes.begin(), count, source);
        if (tpw::ReadPackedPixel(source, count) != expected[count - 1]) {
            std::fprintf(stderr, "Incorrect read of a %u-byte packed pixel\n", count);
            return 1;
        }
    }
}
