// pthread/qos.h for Linux: the two classes platform/handoff.cpp gives its preload thread, which the Mac's pthread
// library has (platform/win/pthread/qos.h is the Windows copy). platform/host_linux.cpp defines the call: background
// lowers the thread's CPU and disk priority, user-initiated gives back what the system lets a plain program give back.
#pragma once

typedef enum { QOS_CLASS_BACKGROUND = 0x09, QOS_CLASS_USER_INITIATED = 0x19 } qos_class_t;

#ifdef __cplusplus
extern "C" {
#endif
int pthread_set_qos_class_self_np(qos_class_t qos, int relative_priority);
#ifdef __cplusplus
}
#endif
