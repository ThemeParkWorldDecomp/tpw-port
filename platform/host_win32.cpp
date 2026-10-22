// host_win32.cpp: see host_win32.h.
#ifdef _WIN32
#include "host_win32.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace tpw::win32 {

bool InLoadedImage(const void* address) {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              static_cast<LPCWSTR>(address), &module) != 0;
}

void* SystemGLProc(const char* name) {
    static HMODULE gl = LoadLibraryW(L"opengl32.dll");
    return gl != nullptr ? reinterpret_cast<void*>(GetProcAddress(gl, name)) : nullptr;
}

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

}  // namespace tpw::win32
#endif
