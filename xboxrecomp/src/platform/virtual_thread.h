#ifndef HOST_VIRTUAL_THREAD_H
#define HOST_VIRTUAL_THREAD_H
#include "win32_compat.h"

/* A virtual thread has a regular reference-counted, waitable thread handle,
 * but never owns a pthread. The owner completes it after its stack has exited.
 * Callbacks run serially, outside object locks; update callbacks must not
 * switch stacks. They may be called from a native completion thread. */
typedef struct HostVirtualThreadOps {
    void (*priority)(void *thread, int priority);
    void (*suspend)(void *thread, unsigned count);
    void (*exit)(void *thread, DWORD code);
} HostVirtualThreadOps;
HANDLE host_virtual_thread_create(const HostVirtualThreadOps *ops, void *thread,
                                  DWORD flags, LPDWORD tid);
int host_virtual_thread_complete(HANDLE thread, DWORD code);
/* Bind only on the executor's native thread. NULL restores the native thread
 * identity. Binding neither acquires nor releases a handle reference. */
int host_virtual_thread_bind(HANDLE thread);
int host_virtual_thread_is(HANDLE thread);
#endif
