#ifndef HOST_DISPATCHER_BACKEND_H
#define HOST_DISPATCHER_BACKEND_H
#include "win32_compat.h"
/* Optional backend installed before title objects are created. Native handles
 * keep their identity/refcount; only event/semaphore state and waits move. */
typedef struct HostDispatcherBackend {
    /* kind: 0 event, 1 semaphore, 2 recursive mutex */
    void *(*create)(int kind, int manual, LONG initial, LONG limit);
    void (*destroy)(void *object);
    BOOL (*signal)(void *object, LONG count, PLONG previous, int pulse);
    BOOL (*reset)(void *object);
    DWORD (*wait)(unsigned count, void *const *objects, int all, DWORD ms);
    BOOL (*bind_header)(void *object, volatile void *header);
    int (*native_wait_allowed)(void);
    int (*sleep)(DWORD ms);
    void (*thread_exit)(DWORD tid);
} HostDispatcherBackend;
void host_set_dispatcher_backend(const HostDispatcherBackend *backend);
int host_dispatcher_manages(HANDLE handle);
BOOL host_dispatcher_bind_header(HANDLE handle, volatile void *header);
int host_continuation_active(void);
#endif
