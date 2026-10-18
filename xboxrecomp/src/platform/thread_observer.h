#ifndef HOST_THREAD_OBSERVER_H
#define HOST_THREAD_OBSERVER_H
#include "win32_compat.h"
#include <stdint.h>

/* Optional observer, installed before observed threads start. Native-only
 * users need no observer. Callbacks never run under an object's mutex. */
typedef struct HostThreadObserver {
    void (*wait_begin)(DWORD tid, uintptr_t object);
    void (*wait_end)(DWORD tid);
    void (*priority)(DWORD tid, int priority);
    void (*suspend)(DWORD tid, unsigned count);
    void (*exit)(DWORD tid);
} HostThreadObserver;
void host_set_thread_observer(const HostThreadObserver *observer);
/* Include changes made before lazy guest registration. */
int host_current_thread_state(int *priority, unsigned *suspended);
#endif
