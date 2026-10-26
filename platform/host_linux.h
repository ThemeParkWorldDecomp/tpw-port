// host_linux.h: the Linux calls the launcher needs that the Mac and Windows hosts have another way (platform/host_win.h is
// the Windows one). Linux has the POSIX calls the platform layer makes (dlfcn.h, sys/statvfs.h, backtrace, sigaction); what
// it lacks is the Mac's thread quality-of-service call (platform/linux/pthread/qos.h, defined in host_linux.cpp) and the
// Mac's answer to an integer division the x86 traps.
#pragma once
#ifdef __linux__

namespace tpw::host_linux {

// Integer division as the Mac's processors do it, for the whole process: x86 traps a division by zero and INT_MIN / -1,
// where AArch64 (and the PowerPC before it) answers a value and goes on, and the translation relies on that (the
// mouse's position is divided by a screen size that is 0 while a park loads). A trapped div or idiv gets AArch64's
// answer: by zero, a quotient of 0 and the dividend as the remainder; INT_MIN / -1, INT_MIN and a remainder of 0. A
// SIGFPE handler does it (the kernel reports both faults as FPE_INTDIV, so the divisor is read to tell them apart). On
// an AArch64 Linux nothing traps and this does nothing. Any other SIGFPE ends the process as it would have.
void DivideLikeTheMac();

}  // namespace tpw::host_linux

#endif
