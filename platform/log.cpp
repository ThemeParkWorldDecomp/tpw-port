// log.cpp: the platform log and the game's own logging calls (ULogManager, UDebug,
// DebugStr).
#include <cstdio>
#include <cstdlib>
#include <string>

#include "log_file.h"
#include "platform.h"

namespace tpw {

Config& config() {
    static Config instance;
    return instance;
}

void LogLine(const char* prefix, const char* text) {
    LogToFile(prefix, text);   // the log file takes every line, whatever logToStderr says (log_file.h)
    if (!config().logToStderr) return;
    std::fprintf(stderr, "%s%s\n", prefix, text);
}

void Log(const char* format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    LogLine("platform: ", text);
}

}  // namespace tpw

namespace {
// The context of the next ULogManager::Log line. The game has one logger and sets the
// context right before each line.
const char* gLogModule = "";
const char* gLogFile = "";
int32_t gLogLineNumber = 0;
}  // namespace

namespace SamsUtilities {

ULogManager* gLogger = nullptr;

ULogManager::ULogManager() : mOpaque{} {}

void ULogManager::SetContext(const char* module, const char* file, int32_t line, uint32_t) {
    gLogModule = module != nullptr ? module : "";
    gLogFile = file != nullptr ? file : "";
    gLogLineNumber = line;
}

void ULogManager::Log(const char* format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    char prefix[160];
    std::snprintf(prefix, sizeof prefix, "[%s %s:%d] ", gLogModule, gLogFile, gLogLineNumber);
    tpw::LogLine(prefix, text);
}

// The main app's UDebug is a no-op in the 2.04b3 build (0x10000e40); the messages are worth
// seeing in a port, so they are logged.
void UDebug(const char* format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    tpw::LogLine("debug: ", text);
}

}  // namespace SamsUtilities

// The Mac dropped into the debugger with the message; here it is logged and the game goes on.
void DebugStr(const unsigned char* message) {
    const std::string text(reinterpret_cast<const char*>(message + 1), message[0]);
    tpw::LogLine("DebugStr: ", text.c_str());
}
