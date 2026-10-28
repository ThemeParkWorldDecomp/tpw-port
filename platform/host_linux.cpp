// host_linux.cpp: what the Linux build needs of the host that the Mac's libraries had: the thread quality-of-service
// call of the Mac's pthread library (platform/linux/pthread/qos.h). Linux has the other POSIX calls the platform layer
// makes (dlfcn.h, sys/statvfs.h, backtrace, sigaction).
#ifdef __linux__
#include <pthread/qos.h>   // platform/linux
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

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
#endif
