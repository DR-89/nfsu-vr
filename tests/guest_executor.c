#define _GNU_SOURCE
#include "kernel/guest_fiber.h"
#include "kernel/guest_dispatcher.h"
#include "kernel/guest_scheduler.h"
#include "kernel/recomp_context.h"
#include "recomp_context_registers.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

static volatile LONG preempt;
static XgfExecutor *executor;
static pthread_t owner;
static DWORD native_tid, tids[2];
static HANDLE handles[2], ready, consumed;
static XgfFiber *fibers[2], *child;
static unsigned value, messages;
static int trace[16], trace_count;
static unsigned callbacks_done;
static uintptr_t native_low, native_high;
static void native_stack_probe(void *argument)
{
    char marker;
    assert((uintptr_t)&marker >= native_low && (uintptr_t)&marker < native_high);
    assert(GetCurrentThreadId() == *(DWORD *)argument && xgf_current());
    RecompKernelState *k = &recomp_context_current()->kernel;
    assert(k->native_kernel_frames == 1 && k->busy_depth == 1 && k->gil_depth == 1 && k->guest_cs_depth == 1);
    assert(!xgf_yield() && !xgf_prepare_wait(xgf_current()));
    assert(!xgf_native_call(native_stack_probe, argument));
    errno = EIO;
    SetLastError(123);
}
static DWORD WINAPI native_stack_entry(void *unused)
{
    (void)unused;
    char marker;
    assert((uintptr_t)&marker < native_low || (uintptr_t)&marker >= native_high);
    DWORD identity = GetCurrentThreadId();
    RecompKernelState *k = &recomp_context_current()->kernel;
    k->native_kernel_frames = k->busy_depth = k->gil_depth = k->guest_cs_depth = 1;
    for (unsigned i = 0; i < 100; i++) {
        assert(xgf_native_call(native_stack_probe, &identity));
        assert(errno == EIO && GetLastError() == 123);
        assert(GetCurrentThreadId() == identity);
    }
    k->native_kernel_frames = k->busy_depth = k->gil_depth = k->guest_cs_depth = 0;
    return 41;
}
static DWORD WINAPI device_callback(void *argument)
{
    assert(pthread_equal(pthread_self(), owner));
    assert(GetCurrentThreadId() != native_tid);
    assert(xgf_current_executor() == executor && g_eax == 0);
    g_eax = 123;
    return *(unsigned *)argument + 7;
}
static void *device_native(void *unused)
{
    (void)unused;
    for (unsigned i = 0; i < 100; i++) {
        DWORD result;
        assert(xgf_invoke(executor, device_callback, &i, 8, &result));
        assert(result == i + 7);
    }
    __atomic_store_n(&callbacks_done, 1, __ATOMIC_RELEASE);
    return NULL;
}
static DWORD WINAPI await_callbacks(void *unused)
{
    (void)unused;
    DWORD ignored;
    assert(!xgf_invoke(executor, device_callback, NULL, 8, &ignored));
    while (!__atomic_load_n(&callbacks_done, __ATOMIC_ACQUIRE)) Sleep(1);
    return 37;
}

static void identity(unsigned side)
{
    assert(pthread_equal(pthread_self(), owner));
    assert(GetCurrentThreadId() == tids[side]);
    assert(GetThreadId(GetCurrentThread()) == tids[side]);
    assert(GetCurrentThread() == handles[side]);
    assert(tids[side] != native_tid && tids[side] != tids[1 - side]);
}
static DWORD WINAPI producer(void *unused)
{
    (void)unused;
    for (unsigned i = 1; i <= 1000; i++) {
        identity(0);
        assert(WaitForSingleObject(consumed, INFINITE) == WAIT_OBJECT_0);
        value = i;
        SetEvent(ready);
    }
    return 17;
}
static DWORD WINAPI consumer(void *unused)
{
    (void)unused;
    for (unsigned i = 1; i <= 1000; i++) {
        assert(WaitForSingleObject(ready, INFINITE) == WAIT_OBJECT_0);
        identity(1);
        assert(value == i);
        messages++;
        SetEvent(consumed);
    }
    return 19;
}
static HANDLE make(LPTHREAD_START_ROUTINE entry, void *arg, DWORD flags, XgfFiber **f, DWORD *tid)
{
    HANDLE h = xgf_create_thread(executor, entry, arg, 256 * 1024, 0x4000, flags, tid, f);
    assert(h && *f);
    return h;
}
static void run(void)
{
    assert(xgf_executor_run(executor, 3000, xgd_poll_timeouts) == XGF_RUN_DONE);
    assert(GetCurrentThreadId() == native_tid);
    assert(pthread_equal(pthread_self(), owner));
}
static void reap(HANDLE h, XgfFiber *f, DWORD expected)
{
    DWORD code;
    assert(WaitForSingleObject(h, 0) == WAIT_OBJECT_0);
    assert(GetExitCodeThread(h, &code) && code == expected);
    assert(xgf_destroy(f));
    assert(GetThreadId(h)); /* Handle outlives the stack. */
    assert(CloseHandle(h));
}
static DWORD WINAPI ordered(void *arg)
{
    for (int i = 0; i < 2; i++) {
        trace[trace_count++] = (int)(uintptr_t)arg;
        Sleep(0);
    }
    return 0;
}
static DWORD WINAPI child_entry(void *unused)
{
    (void)unused;
    Sleep(3);
    ExitThread(42);
    assert(0);
    return 0;
}
static DWORD WINAPI parent(void *unused)
{
    (void)unused;
    DWORD parent_tid = GetCurrentThreadId();
    HANDLE h = make(child_entry, NULL, 0, &child, NULL), duplicate;
    assert(DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &duplicate,
                           0, FALSE, DUPLICATE_SAME_ACCESS));
    assert(CloseHandle(h)); /* The executor retains the running object's life. */
    assert(WaitForSingleObject(duplicate, INFINITE) == WAIT_OBJECT_0);
    assert(GetCurrentThreadId() == parent_tid);
    DWORD code;
    assert(GetExitCodeThread(duplicate, &code) && code == 42);
    CloseHandle(duplicate);
    return 23;
}
static DWORD WINAPI suspended(void *unused)
{
    (void)unused;
    trace[trace_count++] = 7;
    assert(SuspendThread(GetCurrentThread()) == 0);
    SetEvent(ready);
    Sleep(0);
    trace[trace_count++] = 8;
    return 27;
}
static void *resume_native(void *arg)
{
    assert(WaitForSingleObject(ready, 1000) == WAIT_OBJECT_0);
    assert(SetThreadPriority(arg, 4));
    assert(ResumeThread(arg) == 1);
    return NULL;
}
static DWORD WINAPI timeout_entry(void *unused)
{
    (void)unused;
    assert(WaitForSingleObject(ready, 4) == WAIT_TIMEOUT);
    return 31;
}
static DWORD WINAPI wait_ordered(void *arg)
{
    unsigned side = (unsigned)(uintptr_t)arg;
    assert(WaitForSingleObject(side == 1 ? ready : consumed, INFINITE) == WAIT_OBJECT_0);
    trace[trace_count++] = (int)side;
    return 0;
}
static DWORD WINAPI preempt_peer(void *unused)
{
    (void)unused;
    assert(0); /* The probe removes this peer without running it. */
    return 0;
}
static DWORD WINAPI preempt_waiting_peer(void *unused)
{
    (void)unused;
    assert(xgf_prepare_wait(xgf_current()));
    assert(xgf_park());
    return 44;
}
static void *preempt_native_notify(void *peer)
{
    xgf_notify(peer);
    return NULL;
}
static DWORD WINAPI preempt_cache_probe(void *unused)
{
    (void)unused;
    assert(!xgf_should_preempt());
    XgfFiber *peer;
    HANDLE h = make(preempt_peer, NULL, 0, &peer, NULL);
    assert(SetThreadPriority(h, -1));
    usleep(5000); /* Expired slice alone must not preempt for lower priority. */
    for (unsigned i = 0; i < 1000; i++) assert(!xgf_should_preempt());
    assert(SetThreadPriority(h, 0));
    assert(xgf_should_preempt()); /* Equal priority still observes slice expiry. */
    assert(SetThreadPriority(h, 1));
    assert(xgf_should_preempt());
    assert(SuspendThread(h) == 0);
    assert(!xgf_should_preempt());
    assert(ResumeThread(h) == 1);
    assert(xgf_should_preempt());
    assert(SetThreadPriority(GetCurrentThread(), 2));
    assert(!xgf_should_preempt());
    assert(SetThreadPriority(GetCurrentThread(), 0));
    assert(xgf_should_preempt());
    assert(xgf_destroy(peer));
    assert(!xgf_should_preempt());
    assert(CloseHandle(h));
    h = make(preempt_waiting_peer, NULL, 0, &peer, NULL);
    assert(SetThreadPriority(h, 1));
    assert(xgf_yield());
    assert(xgf_state(peer) == XGF_WAITING);
    assert(!xgf_should_preempt()); /* Cache the sleeping high-priority peer. */
    pthread_t notifier;
    assert(!pthread_create(&notifier, NULL, preempt_native_notify, peer));
    assert(!pthread_join(notifier, NULL));
    assert(xgf_should_preempt()); /* Native notification must invalidate it. */
    assert(xgf_yield());
    reap(h, peer, 44);
    assert(!xgf_should_preempt());
    return 43;
}
int main(void)
{
    alarm(15);
    owner = pthread_self(); native_tid = GetCurrentThreadId();
    pthread_attr_t attr;
    void *stack; size_t stack_size;
    assert(!pthread_getattr_np(owner, &attr));
    assert(!pthread_attr_getstack(&attr, &stack, &stack_size));
    pthread_attr_destroy(&attr);
    native_low = (uintptr_t)stack; native_high = native_low + stack_size;
    xgs_init(&preempt);
    assert(xgf_use_dispatcher_waits());
    executor = xgf_executor_create(); assert(executor);
    HANDLE probe = make(preempt_cache_probe, NULL, 0, &fibers[0], NULL);
    run(); reap(probe, fibers[0], 43);
    ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    consumed = CreateEventW(NULL, FALSE, TRUE, NULL);
    handles[0] = make(producer, NULL, 0, &fibers[0], &tids[0]);
    handles[1] = make(consumer, NULL, 0, &fibers[1], &tids[1]);
    assert(xgf_executor_run(executor, 0, xgd_poll_timeouts) == XGF_RUN_PENDING);
    run();
    assert(messages == 1000);
    assert(WaitForMultipleObjects(2, handles, TRUE, 0) == WAIT_OBJECT_0);
    reap(handles[0], fibers[0], 17); reap(handles[1], fibers[1], 19);

    HANDLE ordered_handles[3]; XgfFiber *ordered_fibers[3];
    for (unsigned i = 0; i < 3; i++)
        ordered_handles[i] = make(ordered, (void *)(uintptr_t)(i + 1), 0, &ordered_fibers[i], NULL);
    assert(SetThreadPriority(ordered_handles[2], 4));
    run();
    const int expected[] = {3, 3, 1, 2, 1, 2};
    for (unsigned i = 0; i < 6; i++) assert(trace[i] == expected[i]);
    for (unsigned i = 0; i < 3; i++) reap(ordered_handles[i], ordered_fibers[i], 0);

    trace_count = 0;
    ResetEvent(ready); ResetEvent(consumed);
    for (unsigned i = 0; i < 2; i++)
        handles[i] = make(wait_ordered, (void *)(uintptr_t)(i + 1), 0, &fibers[i], NULL);
    assert(xgf_executor_run(executor, 2, xgd_poll_timeouts) == XGF_RUN_PENDING);
    assert(xgf_state(fibers[0]) == XGF_WAITING && xgf_state(fibers[1]) == XGF_WAITING);
    SetEvent(ready); SetEvent(consumed);
    run();
    assert(trace_count == 2 && trace[0] == 1 && trace[1] == 2);
    for (unsigned i = 0; i < 2; i++) reap(handles[i], fibers[i], 0);

    trace_count = 0;

    HANDLE h = make(parent, NULL, 0, &fibers[0], NULL);
    run(); reap(h, fibers[0], 23);
    assert(xgf_destroy(child));

    trace_count = 0;
    h = make(suspended, NULL, CREATE_SUSPENDED, &fibers[0], NULL);
    assert(SuspendThread(h) == 1);
    assert(ResumeThread(h) == 2);
    assert(xgf_executor_run(executor, 2, xgd_poll_timeouts) == XGF_RUN_PENDING);
    assert(trace_count == 0 && !xgf_executor_wait(executor, 0));
    assert(ResumeThread(h) == 1);
    pthread_t native;
    assert(!pthread_create(&native, NULL, resume_native, h));
    run();
    assert(!pthread_join(native, NULL));
    assert(trace_count == 2 && trace[0] == 7 && trace[1] == 8);
    reap(h, fibers[0], 27);

    h = make(timeout_entry, NULL, 0, &fibers[0], NULL);
    run(); reap(h, fibers[0], 31);
    /* No native start and no dangling callbacks when an unused stack is discarded. */
    h = make(child_entry, NULL, CREATE_SUSPENDED, &fibers[0], NULL);
    assert(!TerminateThread(h, 0));
    assert(xgf_destroy(fibers[0]));
    DWORD code;
    assert(GetExitCodeThread(h, &code) && code == 995);
    assert(WaitForSingleObject(h, 0) == WAIT_OBJECT_0);
    CloseHandle(h);
    h = make(await_callbacks, NULL, 0, &fibers[0], NULL);
    assert(!pthread_create(&native, NULL, device_native, NULL));
    run();
    assert(!pthread_join(native, NULL));
    reap(h, fibers[0], 37);
    h = make(native_stack_entry, NULL, 0, &fibers[0], NULL);
    run(); reap(h, fibers[0], 41);
    CloseHandle(ready); CloseHandle(consumed);
    assert(xgf_executor_destroy(executor));
    puts("PASS: automatic executor, virtual identities, 1000 messages, FIFO/priority, child creation/join/ExitThread, suspend/resume, native wake, deadlines and handle lifetime");
}
