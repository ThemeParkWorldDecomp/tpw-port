// host_linux.cpp: see host_linux.h.
#ifdef __linux__
#include "host_linux.h"

#include <pthread/qos.h>   // platform/linux
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/ucontext.h>
#include <unistd.h>

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "log_file.h"   // WriteScrubbed: the one line this writes goes to the log file as every line does

namespace tpw {
void Log(const char* format, ...) __attribute__((format(printf, 1, 2)));   // platform.h
}

// ---- The thread quality-of-service call (platform/linux/pthread/qos.h) -----------------------------------------------
// Background: the thread's nice value goes to 19 and its disk reads to the idle class, so the preload (platform/handoff.cpp)
// never slows what the player is doing. User-initiated: both are asked back (nice 0, best-effort reads); Linux lets a plain
// program raise a thread's nice value only within its RLIMIT_NICE, which is usually none, so the CPU part is then refused
// and the thread stays at 19, which only matters while something else wants every core. Every call answers 0, as the
// Mac's does.
extern "C" int pthread_set_qos_class_self_np(qos_class_t qos, int) {
    const pid_t thread = static_cast<pid_t>(syscall(SYS_gettid));
    const bool background = qos == QOS_CLASS_BACKGROUND;
    (void)setpriority(PRIO_PROCESS, static_cast<id_t>(thread), background ? 19 : 0);
    // ioprio_set(IOPRIO_WHO_PROCESS = 1, the thread, class << 13 | level): class 3 is idle, 2 best-effort at level 4 (the
    // default). <linux/ioprio.h> is not in every distribution's headers, so the numbers are written out.
    (void)syscall(SYS_ioprio_set, 1, thread, background ? (3 << 13) : ((2 << 13) | 4));
    return 0;
}

#if defined(__x86_64__)
namespace tpw::host_linux {

namespace {

// The div or idiv at `code`: its length, its operand size in bytes, and the divisor's value, read from the register or
// the memory the instruction names (the fault comes after the operand was fetched, so the memory can be read). False
// when the instruction is not a div or idiv this decodes (0x67, an address-size prefix, is not expected in 64-bit code).
struct Divide {
    size_t length = 0;
    int size = 4;
    uint64_t divisor = 0;
};

// The gregs index of a register in the instruction's numbering (rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi, r8..r15).
const int kRegisters[16] = {REG_RAX, REG_RCX, REG_RDX, REG_RBX, REG_RSP, REG_RBP, REG_RSI, REG_RDI,
                            REG_R8,  REG_R9,  REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15};

uint64_t ReadMemory(uintptr_t address, int size) {
    uint64_t value = 0;
    std::memcpy(&value, reinterpret_cast<const void*>(address), static_cast<size_t>(size));
    return value;
}

bool DecodeDivide(const uint8_t* code, const greg_t* gregs, Divide& out) {
    size_t i = 0;
    bool wide16 = false;
    for (;; ++i) {   // legacy prefixes
        const uint8_t b = code[i];
        if (b == 0x66) {
            wide16 = true;
        } else if (b == 0x67) {
            return false;
        } else if (b != 0xF0 && b != 0xF2 && b != 0xF3 && b != 0x2E && b != 0x36 && b != 0x3E && b != 0x26 && b != 0x64 &&
                   b != 0x65) {
            break;
        }
        if (i == 4) return false;
    }
    uint8_t rex = 0;
    if ((code[i] & 0xF0) == 0x40) rex = code[i++];
    const bool wide64 = (rex & 8) != 0;
    const uint8_t opcode = code[i++];
    if (opcode != 0xF6 && opcode != 0xF7) return false;
    const bool byte = opcode == 0xF6;
    const uint8_t modrm = code[i++];
    const int reg = (modrm >> 3) & 7, mod = modrm >> 6, rm = modrm & 7;
    if (reg != 6 && reg != 7) return false;
    out.size = byte ? 1 : wide64 ? 8 : wide16 ? 2 : 4;
    if (mod == 3) {
        const int index = rm | ((rex & 1) << 3);
        if (byte && rex == 0 && rm >= 4) {   // ah, ch, dh, bh
            out.divisor = (static_cast<uint64_t>(gregs[kRegisters[rm - 4]]) >> 8) & 0xFF;
        } else {
            const uint64_t mask = out.size == 8 ? ~0ull : (1ull << (out.size * 8)) - 1;
            out.divisor = static_cast<uint64_t>(gregs[kRegisters[index]]) & mask;
        }
        out.length = i;
        return true;
    }
    // A memory operand: [base + index * scale + displacement], or RIP-relative.
    int64_t address = 0;
    bool ripRelative = false;
    if (rm == 4) {   // a SIB byte
        const uint8_t sib = code[i++];
        const int scale = 1 << (sib >> 6);
        const int index = ((sib >> 3) & 7) | ((rex & 2) << 2);
        const int base = (sib & 7) | ((rex & 1) << 3);
        if (index != 4) address += static_cast<int64_t>(gregs[kRegisters[index]]) * scale;
        if ((sib & 7) == 5 && mod == 0) {
            int32_t disp = 0;
            std::memcpy(&disp, code + i, 4);
            i += 4;
            address += disp;
        } else {
            address += static_cast<int64_t>(gregs[kRegisters[base]]);
        }
    } else if (rm == 5 && mod == 0) {
        ripRelative = true;
        int32_t disp = 0;
        std::memcpy(&disp, code + i, 4);
        i += 4;
        address += disp;
    } else {
        address += static_cast<int64_t>(gregs[kRegisters[rm | ((rex & 1) << 3)]]);
    }
    if (mod == 1) {
        address += static_cast<int8_t>(code[i++]);
    } else if (mod == 2) {
        int32_t disp = 0;
        std::memcpy(&disp, code + i, 4);
        i += 4;
        address += disp;
    }
    if (ripRelative) address += static_cast<int64_t>(reinterpret_cast<uintptr_t>(code) + i);   // the next instruction's
    out.length = i;
    out.divisor = ReadMemory(static_cast<uintptr_t>(address), out.size);
    return true;
}

struct sigaction gPrevious;

// Where the program is loaded (the linker's own symbol, as every ELF program has it): the log's addresses are offsets
// from it, which addr2line -e tpw_launcher names.
extern "C" const char __executable_start[] __attribute__((weak));

void OnDivideTrap(int signal, siginfo_t* info, void* context) {
    auto* uc = static_cast<ucontext_t*>(context);
    greg_t* g = uc->uc_mcontext.gregs;
    Divide divide;
    if (info->si_code == FPE_INTDIV || info->si_code == FPE_INTOVF) {
        if (DecodeDivide(reinterpret_cast<const uint8_t*>(g[REG_RIP]), g, divide)) {
            // By zero: quotient 0, remainder the dividend. INT_MIN / -1: quotient INT_MIN (the dividend), remainder 0.
            const bool overflow = divide.divisor != 0;
            static bool told = false;   // where the game counts on it, once (addr2line -e tpw_launcher <address - base> names the function)
            if (!told) {
                told = true;
                static char text[256];
                const int length =
                    std::snprintf(text, sizeof text, "platform: %s at %p (exe base %p) answered as the Mac does\n",
                                  overflow ? "INT_MIN / -1" : "a division by zero", reinterpret_cast<void*>(g[REG_RIP]),
                                  static_cast<const void*>(__executable_start));
                if (length > 0) {
                    (void)!write(2, text, static_cast<size_t>(length));
                    const int fd = tpw::LogFileDescriptor();
                    if (fd >= 0) tpw::WriteScrubbed(fd, text, static_cast<size_t>(length));
                }
            }
            const uint64_t rax = static_cast<uint64_t>(g[REG_RAX]);
            const uint64_t rdx = static_cast<uint64_t>(g[REG_RDX]);
            switch (divide.size) {
                case 1: {   // AX by r/m8: AL the quotient, AH the remainder
                    const uint64_t al = rax & 0xFF;
                    g[REG_RAX] = static_cast<greg_t>((rax & ~uint64_t(0xFFFF)) | (overflow ? al : 0) | ((overflow ? 0 : al) << 8));
                    break;
                }
                case 2: {
                    const uint64_t ax = rax & 0xFFFF;
                    g[REG_RAX] = static_cast<greg_t>((rax & ~uint64_t(0xFFFF)) | (overflow ? ax : 0));
                    g[REG_RDX] = static_cast<greg_t>((rdx & ~uint64_t(0xFFFF)) | (overflow ? 0 : ax));
                    break;
                }
                case 4: {   // a 32-bit result zero-extends, as the instruction's would
                    const uint64_t eax = rax & 0xFFFFFFFF;
                    g[REG_RAX] = static_cast<greg_t>(overflow ? eax : 0);
                    g[REG_RDX] = static_cast<greg_t>(overflow ? 0 : eax);
                    break;
                }
                default: {
                    g[REG_RAX] = static_cast<greg_t>(overflow ? rax : 0);
                    g[REG_RDX] = static_cast<greg_t>(overflow ? 0 : rax);
                    break;
                }
            }
            g[REG_RIP] += static_cast<greg_t>(divide.length);
            return;
        }
    }
    // Not a division this answers: the fault goes to whatever handled SIGFPE before (none: the process ends with it).
    if ((gPrevious.sa_flags & SA_SIGINFO) != 0 && gPrevious.sa_sigaction != nullptr) {
        gPrevious.sa_sigaction(signal, info, context);
        return;
    }
    if (gPrevious.sa_handler != SIG_IGN && gPrevious.sa_handler != SIG_DFL && gPrevious.sa_handler != nullptr) {
        gPrevious.sa_handler(signal);
        return;
    }
    // The disposition that was there is put back (SIG_IGN stays SIG_IGN; the kernel still ends the process on a fault
    // it cannot ignore), and the instruction runs again under it.
    std::signal(signal, gPrevious.sa_handler == SIG_IGN ? SIG_IGN : SIG_DFL);
}

}  // namespace

void DivideLikeTheMac() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    struct sigaction action{};
    action.sa_sigaction = OnDivideTrap;
    action.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&action.sa_mask);
    sigaction(SIGFPE, &action, &gPrevious);
}

}  // namespace tpw::host_linux
#else
namespace tpw::host_linux {
void DivideLikeTheMac() {}   // AArch64 divides as the Mac does
}  // namespace tpw::host_linux
#endif
#endif
