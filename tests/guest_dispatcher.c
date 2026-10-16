/* Host regression for real platform handles backed by guest wait transactions.
 * See docs/guest-scheduler.md for the build command. */
#include <signal.h> /* Android headers also expose the POSIX signal function. */
#include "../xboxrecomp/src/kernel/guest_dispatcher.c"
#include <assert.h>
#include <stdatomic.h>
#include <sched.h>
#include <stdio.h>

static volatile LONG preempt;
static atomic_uint finished, used;
static unsigned execution_order[32];
static HANDLE event, second;
static HANDLE ping[2];
static unsigned ping_active, ping_total;
static DWORD WINAPI ping_pong(void *arg)
{
    unsigned side = (unsigned)(uintptr_t)arg;
    xgs_acquire(GetCurrentThreadId(), 0, 0);
    for (unsigned i = 0; i < 1000; i++) {
        assert(WaitForSingleObject(ping[side], INFINITE) == WAIT_OBJECT_0);
        assert(ping_active++ == 0);
        ping_total++;
        assert(--ping_active == 0);
        SetEvent(ping[1 - side]);
    }
    xgs_release(0);
    return 0;
}
typedef struct { int priority; unsigned index; int all; DWORD ms; DWORD result; } Start;
static Start starts[8];
static uint64_t clock_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec;
}
static void enrolled(unsigned expected)
{
    uint64_t deadline = clock_ns() + 2000000000ull;
    for (;;) {
        unsigned count = 0;
        pthread_mutex_lock(&lock);
        for (Waiter *w = waiters; w; w = w->next) if (!w->done) count++;
        pthread_mutex_unlock(&lock);
        if (count == expected) return;
        assert(clock_ns() < deadline);
        sched_yield();
    }
}
static DWORD WINAPI waiting(void *arg)
{
    Start *s = arg;
    xgs_acquire(GetCurrentThreadId(), s->priority, 0);
    HANDLE handles[] = {event, second};
    DWORD result = s->all ? WaitForMultipleObjects(2, handles, TRUE, s->ms)
        : WaitForSingleObject(event, s->ms);
    assert(result == s->result);
    assert(xgs_state(GetCurrentThreadId()) == XGS_RUNNING);
    execution_order[atomic_fetch_add(&used, 1)] = s->index;
    atomic_fetch_add(&finished, 1);
    xgs_release(0);
    return 0;
}
static HANDLE start(unsigned i, int priority, int all, DWORD ms, DWORD result)
{
    starts[i] = (Start){priority, i, all, ms, result};
    HANDLE h = CreateThread(NULL, 0, waiting, &starts[i], CREATE_SUSPENDED, NULL);
    assert(h);
    SetThreadPriority(h, priority);
    ResumeThread(h);
    return h;
}
static void join(HANDLE h)
{
    assert(WaitForSingleObject(h, 2000) == WAIT_OBJECT_0);
    CloseHandle(h);
}
int main(void)
{
    xgs_init(&preempt);
    HANDLE h[3];
    event = CreateEventW(NULL, FALSE, FALSE, NULL);
    assert(host_dispatcher_manages(event));
    /* Auto-reset: one grant, priority then FIFO, independent of native wake order. */
    h[0] = start(0, -1, 0, INFINITE, WAIT_OBJECT_0); enrolled(1);
    h[1] = start(1, 4, 0, INFINITE, WAIT_OBJECT_0); enrolled(2);
    h[2] = start(2, 4, 0, INFINITE, WAIT_OBJECT_0); enrolled(3);
    for (unsigned i = 0; i < 3; i++) {
        SetEvent(event);
        unsigned winner = i == 0 ? 1 : i == 1 ? 2 : 0;
        join(h[winner]);
        assert(execution_order[i] == winner);
    }
    assert(WaitForSingleObject(event, 0) == WAIT_TIMEOUT);
    CloseHandle(event);

    /* WaitAll cannot consume the first semaphore until the entire set is ready. */
    event = CreateSemaphoreW(NULL, 1, 2, NULL);
    second = CreateEventW(NULL, FALSE, FALSE, NULL);
    HANDLE pair[] = {event, second};
    assert(WaitForMultipleObjects(2, pair, TRUE, 0) == WAIT_TIMEOUT);
    assert(WaitForSingleObject(event, 0) == WAIT_OBJECT_0);
    assert(WaitForSingleObject(event, 0) == WAIT_TIMEOUT);
    SetEvent(second);
    assert(WaitForMultipleObjects(2, pair, FALSE, 0) == WAIT_OBJECT_0 + 1);
    assert(ReleaseSemaphore(event, 1, NULL));
    SetEvent(second);
    assert(WaitForMultipleObjects(2, pair, TRUE, 0) == WAIT_OBJECT_0);
    assert(WaitForSingleObject(event, 0) == WAIT_TIMEOUT);
    assert(WaitForSingleObject(second, 0) == WAIT_TIMEOUT);
    assert(!ReleaseSemaphore(event, 3, NULL));
    assert(WaitForSingleObject(event, 0) == WAIT_TIMEOUT);
    HANDLE duplicate[] = {event, event};
    assert(WaitForMultipleObjects(2, duplicate, TRUE, 0) == WAIT_FAILED);

    /* Blocked WaitAll is reevaluated atomically on every signal. */
    h[0] = start(0, 0, 1, INFINITE, WAIT_OBJECT_0); enrolled(1);
    SetEvent(second);
    assert(WaitForSingleObject(second, 0) == WAIT_OBJECT_0);
    assert(ReleaseSemaphore(event, 1, NULL));
    enrolled(1);
    SetEvent(second);
    join(h[0]);
    CloseHandle(event); CloseHandle(second);

    /* Manual events release all enrolled waiters and remain signalled. */
    event = CreateEventW(NULL, TRUE, FALSE, NULL);
    h[0] = start(0, 0, 0, INFINITE, WAIT_OBJECT_0); enrolled(1);
    h[1] = start(1, 0, 0, INFINITE, WAIT_OBJECT_0); enrolled(2);
    SetEvent(event);
    join(h[0]); join(h[1]);
    assert(WaitForSingleObject(event, 0) == WAIT_OBJECT_0);
    ResetEvent(event);
    assert(WaitForSingleObject(event, 0) == WAIT_TIMEOUT);
    h[0] = start(0, 0, 0, INFINITE, WAIT_OBJECT_0); enrolled(1);
    PulseEvent(event); join(h[0]);
    assert(WaitForSingleObject(event, 0) == WAIT_TIMEOUT);
    h[0] = start(0, 0, 0, 1, WAIT_TIMEOUT); join(h[0]);
    enrolled(0);
    SetEvent(event); /* A timed-out stack is no longer in the wait list. */
    CloseHandle(event);
    assert(!waiters);
    ping[0] = CreateEventW(NULL, FALSE, TRUE, NULL);
    ping[1] = CreateEventW(NULL, FALSE, FALSE, NULL);
    h[0] = CreateThread(NULL, 0, ping_pong, (void *)(uintptr_t)0, 0, NULL);
    h[1] = CreateThread(NULL, 0, ping_pong, (void *)(uintptr_t)1, 0, NULL);
    join(h[0]); join(h[1]);
    assert(ping_total == 2000 && ping_active == 0);
    CloseHandle(ping[0]); CloseHandle(ping[1]);
    puts("PASS: guest dispatcher priority/FIFO, auto/manual/pulse, atomic WaitAll, semaphore limits, duplicate rejection, timeout cleanup");
    return 0;
}
