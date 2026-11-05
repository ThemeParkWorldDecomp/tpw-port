// fuzz_stubs.cpp: what the files under test name from the rest of the platform library, which the fuzz targets do not link
// (it brings SDL, the video decoder and OpenGL): the log, which is quiet unless TPW_FUZZ_LOG is set; the host path of a game
// file, which is the path as given; and the socket connection, which no target makes (each sets its own fake one).
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "platform.h"
#include "platform/online_boss.h"

namespace tpw {

void Log(const char* format, ...) {
    if (std::getenv("TPW_FUZZ_LOG") == nullptr) return;
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
}

std::filesystem::path FindHostPath(const std::string& gamePath) { return gamePath; }

namespace online::boss {
std::unique_ptr<Transport> MakeSocketTransport() { return nullptr; }
}  // namespace online::boss

}  // namespace tpw
