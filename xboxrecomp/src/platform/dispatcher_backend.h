#ifndef HOST_DISPATCHER_BACKEND_H
#define HOST_DISPATCHER_BACKEND_H
#include "win32_compat.h"
/* Optional backend installed before title objects are created. Native handles
 * keep their identity/refcount; only event/semaphore state and waits move. */
typedef struct HostDispatcherBackend {
    void *(*create)(int semaphore, int manual, LONG initial, LONG limit);
    void (*destroy)(void *object);
    BOOL (*signal)(void *object, LONG count, PLONG previous, int pulse);
    BOOL (*reset)(void *object);
    DWORD (*wait)(unsigned count, void *const *objects, int all, DWORD ms);
} HostDispatcherBackend;
void host_set_dispatcher_backend(const HostDispatcherBackend *backend);
int host_dispatcher_manages(HANDLE handle);
#endif
