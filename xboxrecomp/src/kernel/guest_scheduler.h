#ifndef XBOX_GUEST_SCHEDULER_H
#define XBOX_GUEST_SCHEDULER_H
#include "platform/win32_compat.h"
#include <stdint.h>

typedef enum {
    XGS_HOST_CALL, XGS_READY, XGS_RUNNING, XGS_WAITING, XGS_SUSPENDED, XGS_EXITED
} XgsState;
typedef struct { uint64_t queued_ns, handoff_ns; int switched; } XgsAcquire;

/* Native stacks/TLS are retained. Exactly one registered thread owns the
 * guest execution token. Wait callbacks observe only registered threads. */
void xgs_init(volatile LONG *preempt_request);
XgsAcquire xgs_acquire(DWORD tid, int priority, int dispatch);
void xgs_release(int dispatch);
XgsAcquire xgs_yield(void);
/* spin=0 is a safe-point preemption; spin=1 is an explicit polling yield. */
XgsAcquire xgs_yield_reason(int spin);
/* Configure once at boot before workers start. */
void xgs_diagnostics(int enabled);
void xgs_mark_main(void);
void xgs_poll(void);
int xgs_should_preempt(void);
void xgs_dispatch_changed(int raised);
void xgs_unregister(void);
XgsState xgs_state(DWORD tid);
int xgs_current_priority(void);
void xgs_wait_begin(uintptr_t object);
void xgs_wait_end(void);
/* Dispatcher grants are published before the native waiter is woken. */
void xgs_wait_complete(DWORD tid);
int xgs_thread_priority(DWORD tid, int fallback);
void xgs_wait_accounting(void (*park)(void), void (*resume)(uint64_t, uint64_t));
#endif
