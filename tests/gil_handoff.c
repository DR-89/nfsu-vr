/* Tests the actual bridge GIL with the Android handoff path on a POSIX host.
 * cc -O1 -ffunction-sections -fdata-sections -Ixboxrecomp/src
 * -Ixboxrecomp/src/kernel tests/gil_handoff.c
 * xboxrecomp/src/kernel/recomp_context.c
 * xboxrecomp/src/platform/win32_compat.c -Wl,--gc-sections -pthread -lm
 */
#include "platform/win32_compat.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
static void check_signal(PCONDITION_VARIABLE cv);
#define WakeConditionVariable check_signal
#define NFSU_GIL_HANDOFF_TEST 1
#define NFSU_PERF_HOST_TEST 1
#include "../xboxrecomp/src/kernel/kernel_bridge.c"
#undef WakeConditionVariable
#include "../src/android_perf.c"

int xbox_thread_holds_dispatch(void) { return 0; }
static unsigned signals, order_count, order[8];
static unsigned active, increments;
static int slow_handoff = 1;

static void check_signal(PCONDITION_VARIABLE cv)
{
    assert(s_gil_queue.head && cv == &s_gil_queue.head->wake);
    assert(s_gil_queue.head->granted_ns != 0);
    signals++;
    if (slow_handoff) Sleep(2); /* Delay AFTER the ticket grant, BEFORE wakeup. */
    WakeConditionVariable(cv);
}

static void *ordered_worker(void *arg)
{
    xbox_gil_enter();
    assert(perf.metrics[NFSU_PERF_GIL_QUEUE].count == 1);
    assert(perf.metrics[NFSU_PERF_GIL_HANDOFF].count == 1);
    assert(perf.metrics[NFSU_PERF_GIL_HANDOFF].sum >= 1000000);
    assert(active++ == 0);
    order[order_count++] = (unsigned)(uintptr_t)arg;
    assert(--active == 0);
    xbox_gil_leave();
    return NULL;
}

static void *stress_worker(void *arg)
{
    (void)arg;
    for (unsigned i = 0; i < 3000; i++) {
        xbox_gil_enter();
        xbox_gil_enter(); /* Nested entry must not enqueue a second waiter. */
        assert(active++ == 0);
        increments++;
        assert(--active == 0);
        xbox_gil_leave();
        xbox_gil_leave();
        if (!(i % 31)) {
            xbox_gil_enter();
            int depth = xbox_gil_suspend();
            sched_yield();
            xbox_gil_resume(depth);
            xbox_gil_leave();
        }
    }
    return NULL;
}

int main(int argc, char **argv)
{
    nfsu_perf_enabled = 1;
    assert(setenv("RECOMP_GIL_EAGER", argc > 1 ? argv[1] : "1", 1) == 0);
    assert(setenv("RECOMP_GIL_MAIN_QUANTUM_US", argc > 2 ? argv[2] : "0", 1) == 0);
    xbox_gil_enter(); /* Initialize before workers, and hold the first ticket. */
    uint64_t configured_quantum = s_gil_main_quantum_ns;
    s_gil_main_quantum_ns = 100000;
    t_gil_main = 1;
    t_gil_quantum_start = 1000000;
    assert(gil_main_quantum_pending(1000000));
    assert(gil_main_quantum_pending(1099999));
    assert(!gil_main_quantum_pending(1100000));
    assert(!gil_main_quantum_pending(0));
    t_gil_main = 0;
    assert(!gil_main_quantum_pending(1000000));
    s_gil_main_quantum_ns = 0;
    t_gil_main = 1;
    assert(!gil_main_quantum_pending(1000000));
    t_gil_main = 0;
    s_gil_main_quantum_ns = configured_quantum;
    recomp_spin_yield();
    assert(perf.metrics[NFSU_PERF_SPIN_WAIT].count == 1);
    assert(perf.metrics[NFSU_PERF_PREEMPT_WAIT].count == 0);
    recomp_preempt();
    assert(perf.metrics[NFSU_PERF_PREEMPT_WAIT].count == 1);
    assert(perf.metrics[NFSU_PERF_SPIN_WAIT].count == 1);
    assert(perf.metrics[NFSU_PERF_SPIN_TOTAL].count == 1);
    assert(perf.metrics[NFSU_PERF_PREEMPT_TOTAL].count == 1);
    assert(t_gil_depth == 1);
    if (configured_quantum) {
        t_gil_main = 1;
        t_gil_quantum_start = 1; /* Expired: the real preempt path must run. */
        uint64_t preempts = perf.metrics[NFSU_PERF_PREEMPT_WAIT].count;
        recomp_preempt();
        assert(perf.metrics[NFSU_PERF_PREEMPT_WAIT].count == preempts + 1);
        assert(t_gil_quantum_start > 1);
        assert(gil_main_quantum_pending(t_gil_quantum_start));
        uint64_t spins = perf.metrics[NFSU_PERF_SPIN_WAIT].count;
        recomp_spin_yield(); /* A fresh quantum must not suppress poll yields. */
        assert(perf.metrics[NFSU_PERF_SPIN_WAIT].count == spins + 1);
        t_gil_main = 0;
    }
    uint64_t first_waiter = s_gil_next;
    pthread_t threads[8];
    for (unsigned i = 0; i < 8; i++) {
        assert(!pthread_create(&threads[i], NULL, ordered_worker, (void *)(uintptr_t)i));
        for (;;) {
            EnterCriticalSection(&s_gil_cs);
            int queued = s_gil_next == first_waiter + i + 1;
            LeaveCriticalSection(&s_gil_cs);
            if (queued) break;
            sched_yield();
        }
    }
    /* Deliberately wake the wrong ticket: it must not acquire the GIL. */
    EnterCriticalSection(&s_gil_cs);
    WakeConditionVariable(&s_gil_queue.tail->wake);
    LeaveCriticalSection(&s_gil_cs);
    Sleep(15); /* Exercise the retained 1 ms preemption-request timeouts. */
    EnterCriticalSection(&s_gil_cs);
    assert(order_count == 0 && g_gil_contended);
    LeaveCriticalSection(&s_gil_cs);
    xbox_gil_leave();
    for (unsigned i = 0; i < 8; i++) assert(!pthread_join(threads[i], NULL));
    assert(order_count == 8 && signals == 8);
    for (unsigned i = 0; i < 8; i++) assert(order[i] == i);
    assert(!s_gil_queue.head && !s_gil_queue.tail && s_gil_next == s_gil_serving);

    slow_handoff = 0;
    for (unsigned i = 0; i < 8; i++) assert(!pthread_create(&threads[i], NULL, stress_worker, NULL));
    for (unsigned i = 0; i < 8; i++) assert(!pthread_join(threads[i], NULL));
    assert(increments == 24000 && active == 0);
    assert(!s_gil_queue.head && !s_gil_queue.tail && s_gil_next == s_gil_serving);
    printf("PASS eager=%s: FIFO, targeted signal, spurious wake, timeout, nesting, suspend/resume, 24000 entries\n",
           argc > 1 ? argv[1] : "1");
    return 0;
}
