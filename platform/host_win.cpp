// Windows port contributed by samueleagle91 (tpw-port PR #1)
// host_win.cpp: see host_win.h.
#ifdef _WIN32
#include "host_win.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace tpw {
void Log(const char* format, ...) __attribute__((format(printf, 1, 2)));   // platform.h
}

namespace tpw::win32 {

namespace {

// The length of the div or idiv (F6 /6 /7, F7 /6 /7) at code, with its operand size; 0 when it is not one.
size_t DivideLength(const uint8_t* code, bool& wide16, bool& wide64, bool& byte) {
    size_t i = 0;
    wide16 = false;
    for (;; ++i) {   // legacy prefixes
        const uint8_t b = code[i];
        if (b == 0x66) {
            wide16 = true;
        } else if (b != 0x67 && b != 0xF0 && b != 0xF2 && b != 0xF3 && b != 0x2E && b != 0x36 && b != 0x3E &&
                   b != 0x26 && b != 0x64 && b != 0x65) {
            break;
        }
        if (i == 4) return 0;
    }
    uint8_t rex = 0;
    if ((code[i] & 0xF0) == 0x40) rex = code[i++];
    wide64 = (rex & 8) != 0;
    const uint8_t opcode = code[i++];
    if (opcode != 0xF6 && opcode != 0xF7) return 0;
    byte = opcode == 0xF6;
    const uint8_t modrm = code[i++];
    const int reg = (modrm >> 3) & 7, mod = modrm >> 6, rm = modrm & 7;
    if (reg != 6 && reg != 7) return 0;
    if (mod != 3) {
        if (rm == 4 && (code[i++] & 7) == 5 && mod == 0) i += 4;   // a SIB byte, with a disp32 base
        else if (rm == 5 && mod == 0) i += 4;                     // RIP-relative
        i += mod == 1 ? 1 : mod == 2 ? 4 : 0;
    }
    return i;
}

LONG CALLBACK OnIntegerTrap(EXCEPTION_POINTERS* info) {
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_INT_OVERFLOW) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = info->ContextRecord;
    bool wide16, wide64, byte;
    const size_t length = DivideLength(reinterpret_cast<const uint8_t*>(c->Rip), wide16, wide64, byte);
    if (length == 0) return EXCEPTION_CONTINUE_SEARCH;
    // By zero: quotient 0, remainder the dividend. INT_MIN / -1: quotient INT_MIN (the dividend), remainder 0.
    const bool overflow = code == EXCEPTION_INT_OVERFLOW;
    static bool told = false;   // where the game counts on it, once (the addresses name functions with llvm-symbolizer)
    if (!told) {
        told = true;
        tpw::Log("%s at %p (exe base %p) answered as the Mac does", overflow ? "INT_MIN / -1" : "a division by zero",
                 reinterpret_cast<void*>(c->Rip), static_cast<void*>(GetModuleHandleW(nullptr)));
    }
    if (byte) {   // AX by r/m8: AL the quotient, AH the remainder
        const DWORD64 al = c->Rax & 0xFF;
        c->Rax = (c->Rax & ~DWORD64(0xFFFF)) | (overflow ? al : 0) | ((overflow ? 0 : al) << 8);
    } else if (wide64) {
        const DWORD64 dividend = c->Rax;
        c->Rax = overflow ? dividend : 0;
        c->Rdx = overflow ? 0 : dividend;
    } else if (wide16) {
        const DWORD64 ax = c->Rax & 0xFFFF;
        c->Rax = (c->Rax & ~DWORD64(0xFFFF)) | (overflow ? ax : 0);
        c->Rdx = (c->Rdx & ~DWORD64(0xFFFF)) | (overflow ? 0 : ax);
    } else {   // a 32-bit result zero-extends, as the instruction's would
        const DWORD64 eax = c->Rax & 0xFFFFFFFF;
        c->Rax = overflow ? eax : 0;
        c->Rdx = overflow ? 0 : eax;
    }
    c->Rip += length;
    return EXCEPTION_CONTINUE_EXECUTION;
}

}  // namespace

void DivideLikeTheMac() {
    static PVOID handler = AddVectoredExceptionHandler(1, OnIntegerTrap);
    (void)handler;
}

namespace {

char gFatalText[4096];

// "tpw_launcher.exe+0x1daed" for an address in a loaded image, else the address itself.
void Describe(DWORD64 address, char* out, size_t size) {
    HMODULE module = nullptr;
    char path[MAX_PATH] = "";
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(address), &module) &&
        GetModuleFileNameA(module, path, sizeof path) != 0) {
        const char* name = path;
        for (const char* p = path; *p != '\0'; ++p)
            if (*p == '\\' || *p == '/') name = p + 1;
        std::snprintf(out, size, "%s+0x%llx", name, static_cast<unsigned long long>(address - reinterpret_cast<DWORD64>(module)));
    } else {
        std::snprintf(out, size, "0x%llx", static_cast<unsigned long long>(address));
    }
}

LONG CALLBACK OnFatalException(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    const DWORD code = record->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_PRIV_INSTRUCTION && code != EXCEPTION_IN_PAGE_ERROR) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    size_t used = 0;
    auto add = [&used](const char* text) {
        const size_t length = std::strlen(text);
        if (used + length + 1 >= sizeof gFatalText) return;
        std::memcpy(gFatalText + used, text, length + 1);
        used += length;
    };
    char line[256], where[160];
    Describe(reinterpret_cast<DWORD64>(record->ExceptionAddress), where, sizeof where);
    std::snprintf(line, sizeof line, "exception 0x%08lx at %s", static_cast<unsigned long>(code), where);
    add(line);
    if (code == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        std::snprintf(line, sizeof line, " %s 0x%llx", record->ExceptionInformation[0] == 1 ? "writing" : "reading",
                      static_cast<unsigned long long>(record->ExceptionInformation[1]));
        add(line);
    }
    add("\nstack:");
    CONTEXT context = *info->ContextRecord;   // walked with the unwind tables, as a debugger would
    for (int frame = 0; frame < 32 && context.Rip != 0; ++frame) {
        Describe(context.Rip, where, sizeof where);
        std::snprintf(line, sizeof line, "\n  %s", where);
        add(line);
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
        if (function == nullptr) {   // a leaf: the return address is on top of the stack
            context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
            context.Rsp += 8;
        } else {
            void* handlerData = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData, &establisher, nullptr);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void RecordFatalExceptions() {
    static PVOID handler = AddVectoredExceptionHandler(0, OnFatalException);
    (void)handler;
}

const char* LastFatalException() { return gFatalText[0] != '\0' ? gFatalText : nullptr; }

bool HasStandardError() {
    const HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
    return handle != nullptr && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN;
}

bool LogToFile(const std::string& path) {
    std::wstring wide(static_cast<size_t>(MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0)), L'\0');
    if (wide.empty() || MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), static_cast<int>(wide.size())) == 0)
        return false;
    // Emptied first, then both streams append, so their lines interleave in the order they were written.
    if (_wfreopen(wide.c_str(), L"w", stderr) == nullptr || _wfreopen(wide.c_str(), L"a", stderr) == nullptr ||
        _wfreopen(wide.c_str(), L"a", stdout) == nullptr) {
        return false;
    }
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    return true;
}

}  // namespace tpw::win32

// The OpenGL calls past 1.1, the most opengl32.dll exports: GL_EXT_compiled_vertex_array's (include/platform/opengl.h) and
// glWindowPos2i (OpenGL 1.4, platform/quicktime.cpp's films). They go to the driver's own entry points, asked of the
// current context at the first call (both are made while drawing). A driver without them leaves them doing nothing: the
// arrays' lock is a hint, and every driver of the last twenty years has glWindowPos2i.
namespace {
template <class Function> Function DriverGLProc(const char* name) {
    const PROC proc = wglGetProcAddress(name);
    const auto value = reinterpret_cast<intptr_t>(proc);
    // Some drivers answer 1, 2, 3 or -1 for a name they do not have.
    return value == 0 || value == 1 || value == 2 || value == 3 || value == -1 ? nullptr : reinterpret_cast<Function>(proc);
}
}  // namespace

extern "C" {
void glLockArraysEXT(int32_t first, int32_t count) {
    static const auto function = DriverGLProc<void(APIENTRY*)(int32_t, int32_t)>("glLockArraysEXT");
    if (function != nullptr) function(first, count);
}

void glUnlockArraysEXT() {
    static const auto function = DriverGLProc<void(APIENTRY*)()>("glUnlockArraysEXT");
    if (function != nullptr) function();
}

void glWindowPos2i(int x, int y) {
    static const auto function = DriverGLProc<void(APIENTRY*)(int, int)>("glWindowPos2i");
    if (function != nullptr) function(x, y);
}
}
#endif
