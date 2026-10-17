#ifndef GUEST_DISPATCHER_H
#define GUEST_DISPATCHER_H
#include <stdint.h>
typedef struct {
    void *(*current)(void);
    int (*priority)(void *continuation);
    int (*prepare)(void *continuation);
    int (*park)(void);
    void (*notify)(void *continuation);
    int (*yield)(void);
} XgdWaitBackend;
/* Optional immutable backend; native pthread waits remain the default. */
int xgd_set_wait_backend(const XgdWaitBackend *backend);
/* Service continuation deadlines from the executor; returns milliseconds to
 * the next deadline, or UINT32_MAX when none. Never call under executor locks. */
uint32_t xgd_poll_timeouts(void);
/* Install once alongside the guest scheduler, before title object creation. */
void xgd_init(void);
#endif
