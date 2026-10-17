#ifndef GUEST_FIBER_H
#define GUEST_FIBER_H
#include <stddef.h>
#include <stdint.h>
#include "platform/win32_compat.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct XgfExecutor XgfExecutor;
typedef struct XgfFiber XgfFiber;
typedef void (*XgfEntry)(void *argument);
typedef enum { XGF_ERROR = -1, XGF_NEW, XGF_SUSPENDED, XGF_RUNNING, XGF_EXITED, XGF_WAITING } XgfState;
typedef enum { XGF_RUN_ERROR = -1, XGF_RUN_DONE, XGF_RUN_PENDING } XgfRunResult;
typedef uint32_t (*XgfPoll)(void);

/* Single-native-thread continuations. The title adapter is explicit opt-in;
 * resume/yield require a safe recomp_context boundary. */
XgfExecutor *xgf_executor_create(void);
int xgf_executor_destroy(XgfExecutor *executor);
/* Stop accepting native submissions and reject queued callbacks. Running
 * stacks are not cancelled. The owner must keep driving those to completion. */
int xgf_executor_close(XgfExecutor *executor);
XgfFiber *xgf_create(XgfExecutor *executor, XgfEntry entry, void *argument,
                     size_t stack_size, uint32_t fs_base);
XgfState xgf_resume(XgfExecutor *executor, XgfFiber *fiber);
int xgf_yield(void);
/* Cooperative safe-point query: higher priority ready work, self suspension,
 * or an equal-priority peer after the shared 1 ms slice. */
int xgf_should_preempt(void);
/* Synchronous host call on the pthread's original stack (e.g. ART/JNI).
 * No guest scheduling/context switch occurs. Callback must not park/yield.
 * Zero means already on a native stack; caller executes its normal path. */
int xgf_native_call(void (*callback)(void *), void *argument);
XgfExecutor *xgf_current_executor(void);
int xgf_detach(XgfFiber *fiber);
/* A native device/completion thread submits a synchronous callback; only the
 * executor runs its C stack. The caller owns argument storage until return.
 * The executor must outlive all submitters. Owner-thread submission is
 * rejected (it would wait on itself). Successful callbacks return a DWORD. */
int xgf_invoke(XgfExecutor *executor, LPTHREAD_START_ROUTINE callback,
               void *argument, int priority, DWORD *result);
XgfState xgf_state(const XgfFiber *fiber);
int xgf_destroy(XgfFiber *fiber);
XgfFiber *xgf_current(void);
int xgf_priority(const XgfFiber *fiber);
int xgf_set_priority(XgfFiber *fiber, int priority);
int xgf_prepare_wait(XgfFiber *fiber);
int xgf_park(void);
/* The waiter must remain alive until its dispatcher transaction is removed. */
void xgf_notify(XgfFiber *fiber);
int xgf_executor_wait(XgfExecutor *executor, uint32_t timeout_ms);
/* Drive until every entry returns or the root's time budget expires. Poll
 * expires waits and returns the next deadline in milliseconds (UINT32_MAX
 * means none). Scheduling is cooperative, highest priority then FIFO; budget
 * expiry is checked between resumptions, not by interrupting native code.
 * Exited stacks remain owned until explicitly destroyed. */
XgfRunResult xgf_executor_run(XgfExecutor *executor, uint32_t budget_ms, XgfPoll poll);
/* Normal stackful exit, only at a safe context boundary. Returns zero if
 * unsafe; a successful exit does not return. No suspended stack cancellation. */
int xgf_exit(void);
/* Explicit virtual thread creation; ordinary platform CreateThread stays
 * native. The returned handle belongs to the caller, the stack to executor.
 * It supports identity, priority, suspend/resume, completion waits and normal
 * ExitThread. The caller reaps the stack with xgf_destroy after exit. */
HANDLE xgf_create_thread(XgfExecutor *executor, LPTHREAD_START_ROUTINE entry,
                         void *argument, size_t stack_size, uint32_t fs_base,
                         DWORD flags, LPDWORD tid, XgfFiber **fiber);
/* Explicitly opt into the dispatcher continuation adapter. */
int xgf_use_dispatcher_waits(void);
#ifdef __cplusplus
}
#endif
#endif
