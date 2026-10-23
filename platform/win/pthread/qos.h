// Windows port contributed by samueleagle91 (tpw-port PR #1)
// pthread/qos.h for Windows: the two classes platform/handoff.cpp gives its preload thread, as Windows thread modes
// (platform/posix_win.cpp). Background lowers the thread's CPU and disk priority, as the Mac's class does; user-initiated
// gives it back the normal ones.
#pragma once

typedef enum { QOS_CLASS_BACKGROUND = 0x09, QOS_CLASS_USER_INITIATED = 0x19 } qos_class_t;

#ifdef __cplusplus
extern "C" {
#endif
int pthread_set_qos_class_self_np(qos_class_t qos, int relative_priority);
#ifdef __cplusplus
}
#endif
