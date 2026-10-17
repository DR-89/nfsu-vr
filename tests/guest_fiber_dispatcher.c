#include "kernel/guest_fiber.h"
#include "kernel/guest_dispatcher.h"
#include "kernel/guest_scheduler.h"
#include "kernel/recomp_context.h"
#include "recomp_context_registers.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>

static volatile LONG preempt;
static XgfExecutor *executor;
static HANDLE event, semaphore;
static HANDLE native_mutex;
static DWORD native_tid;
static unsigned value, produced, consumed, finished;
static DWORD wait_result;
static void producer(void *unused)
{
    (void)unused;
    for (unsigned i = 1; i <= 1000; i++) {
        assert(GetCurrentThreadId() == native_tid);
        assert(WaitForSingleObject(semaphore, INFINITE) == WAIT_OBJECT_0);
        value = i; produced++;
        SetEvent(event);
    }
}
static void consumer(void *unused)
{
    (void)unused;
    for (unsigned i = 1; i <= 1000; i++) {
        assert(WaitForSingleObject(event, INFINITE) == WAIT_OBJECT_0);
        assert(GetCurrentThreadId() == native_tid && value == i);
        consumed++;
        assert(ReleaseSemaphore(semaphore, 1, NULL));
    }
}
static void timed(void *unused)
{ (void)unused; wait_result = WaitForSingleObject(event, 5); }
static void single(void *unused)
{ (void)unused; assert(WaitForSingleObject(event, INFINITE) == WAIT_OBJECT_0); finished++; }
static void all(void *unused)
{
    (void)unused;
    HANDLE pair[] = {semaphore, event};
    assert(WaitForMultipleObjects(2, pair, TRUE, INFINITE) == WAIT_OBJECT_0);
    finished++;
}
static void invalid_boundary(void *unused)
{
    (void)unused;
    recomp_context_current()->kernel.native_kernel_frames = 1;
    assert(WaitForSingleObject(event, INFINITE) == WAIT_FAILED);
    recomp_context_current()->kernel.native_kernel_frames = 0;
    /* A notification before park must be retained, without switching stacks. */
    assert(xgf_prepare_wait(xgf_current()));
    xgf_notify(xgf_current());
    assert(xgf_park());
    assert(xgf_current());
    assert(WaitForSingleObject(native_mutex, 0) == WAIT_FAILED);
    assert(WaitForSingleObject(native_mutex, INFINITE) == WAIT_FAILED);
    HANDLE mixed[] = {event, native_mutex};
    assert(WaitForMultipleObjects(2, mixed, FALSE, INFINITE) == WAIT_FAILED);
    assert(WaitForSingleObjectEx(event, INFINITE, TRUE) == WAIT_FAILED);
}
static void sleeping(void *unused)
{
    (void)unused;
    Sleep(0);
    Sleep(5);
    finished++;
}
static XgfFiber *make(XgfEntry fn)
{
    XgfFiber *f = xgf_create(executor, fn, NULL, 256 * 1024, 0x4000);
    assert(f);
    return f;
}
static void drive(XgfFiber **fibers, unsigned count)
{
    assert(xgf_executor_run(executor, 3000, xgd_poll_timeouts) == XGF_RUN_DONE);
    for (unsigned i = 0; i < count; i++) assert(xgf_state(fibers[i]) == XGF_EXITED);
}
static void *signal_from_native(void *unused)
{ (void)unused; Sleep(1); SetEvent(event); return NULL; }
int main(void)
{
    xgs_init(&preempt);
    assert(xgf_use_dispatcher_waits() && xgf_use_dispatcher_waits());
    executor = xgf_executor_create(); assert(executor);
    native_tid = GetCurrentThreadId();
    event = CreateEventW(NULL, FALSE, FALSE, NULL);
    semaphore = CreateSemaphoreW(NULL, 1, 1, NULL);
    XgfFiber *pair[] = {make(producer), make(consumer)};
    drive(pair, 2);
    assert(produced == 1000 && consumed == 1000);
    assert(xgf_destroy(pair[0]) && xgf_destroy(pair[1]));
    CloseHandle(semaphore); CloseHandle(event);

    event = CreateEventW(NULL, FALSE, FALSE, NULL);
    XgfFiber *f = make(timed);
    assert(xgf_resume(executor, f) == XGF_WAITING);
    assert(!xgf_destroy(f) && xgf_resume(executor, f) == XGF_ERROR);
    drive(&f, 1);
    assert(wait_result == WAIT_TIMEOUT && xgf_destroy(f));
    SetEvent(event); /* Timeout removed its waiter without consuming the later signal. */
    assert(WaitForSingleObject(event, 0) == WAIT_OBJECT_0);
    f = make(timed);
    assert(xgf_resume(executor, f) == XGF_WAITING);
    Sleep(10); /* Let the deadline expire before signal wins the dispatcher lock. */
    SetEvent(event);
    drive(&f, 1);
    assert(wait_result == WAIT_TIMEOUT && xgf_destroy(f));
    assert(WaitForSingleObject(event, 0) == WAIT_OBJECT_0);

    semaphore = CreateSemaphoreW(NULL, 1, 1, NULL);
    f = make(all);
    assert(xgf_resume(executor, f) == XGF_WAITING);
    assert(WaitForSingleObject(semaphore, 0) == WAIT_OBJECT_0);
    assert(ReleaseSemaphore(semaphore, 1, NULL));
    SetEvent(event);
    drive(&f, 1);
    assert(finished == 1 && xgf_destroy(f));
    CloseHandle(semaphore);

    pair[0] = make(single); pair[1] = make(single);
    assert(xgf_resume(executor, pair[0]) == XGF_WAITING);
    assert(xgf_resume(executor, pair[1]) == XGF_WAITING);
    assert(xgf_set_priority(pair[1], 4));
    SetEvent(event);
    assert(xgf_state(pair[0]) == XGF_WAITING && xgf_state(pair[1]) == XGF_SUSPENDED);
    assert(xgf_resume(executor, pair[1]) == XGF_EXITED);
    SetEvent(event);
    assert(xgf_executor_wait(executor, 0)); /* Signal already arrived before sleeping. */
    drive(pair, 2);
    assert(finished == 3 && xgf_destroy(pair[0]) && xgf_destroy(pair[1]));

    f = make(single);
    assert(xgf_resume(executor, f) == XGF_WAITING);
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, signal_from_native, NULL));
    assert(xgf_executor_wait(executor, 2000));
    drive(&f, 1);
    assert(!pthread_join(thread, NULL));
    assert(finished == 4 && xgf_destroy(f));

    native_mutex = CreateMutexW(NULL, FALSE, NULL);
    f = make(invalid_boundary);
    drive(&f, 1);
    assert(xgf_destroy(f));
    CloseHandle(native_mutex);
    f = make(sleeping);
    drive(&f, 1);
    assert(finished == 5 && xgf_destroy(f));
    SetEvent(event);
    assert(WaitForSingleObject(event, 0) == WAIT_OBJECT_0); /* No leaked failed waiter. */
    CloseHandle(event);
    assert(xgf_executor_destroy(executor));
    puts("PASS: one-native-thread producer/consumer, atomic WaitAll, deadlines, priority grants, native async wake, notification-before-park, invalid-boundary cleanup");
    return 0;
}
