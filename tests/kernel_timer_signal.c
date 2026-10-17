/* Deterministically preempt arming at ResetEvent while expiry races it.
 * cc -O1 -ffunction-sections -fdata-sections -Ixboxrecomp/src
 * -Ixboxrecomp/src/kernel tests/kernel_timer_signal.c
 * xboxrecomp/src/kernel/recomp_context.c
 * xboxrecomp/src/platform/win32_compat.c -Wl,--gc-sections -pthread -lm
 */
#include "platform/win32_compat.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef NFSU_DISPATCHER_TIMER_TEST
#include "kernel/guest_scheduler.h"
#include "platform/dispatcher_backend.h"
#endif
static BOOL intercepted_reset(HANDLE);
#define ResetEvent intercepted_reset
#define CreateThread(a,b,c,d,e,f) ((HANDLE)0)
#ifndef TIMER_RUNTIME_SOURCE
#define TIMER_RUNTIME_SOURCE "../xboxrecomp/src/kernel/kernel_bridge.c"
#endif
#include TIMER_RUNTIME_SOURCE
#undef ResetEvent
#undef CreateThread
ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp;
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static int at_reset, attempted, expiry_finished;
static HANDLE event;
static BOOL intercepted_reset(HANDLE h)
{
    pthread_mutex_lock(&gate);
    at_reset = 1;
    pthread_cond_broadcast(&changed);
    while (!attempted) pthread_cond_wait(&changed, &gate);
    /* With the old arm order, expiry can finish before ResetEvent. With
     * the fixed order it waits for g_timer_lock. Give it a bounded window. */
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += 100000000L;
    if (deadline.tv_nsec >= 1000000000L) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000L; }
    while (!expiry_finished)
        if (pthread_cond_timedwait(&changed, &gate, &deadline)) break;
    pthread_mutex_unlock(&gate);
    return ResetEvent(h);
}
static void *expire(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&gate);
    while (!at_reset) pthread_cond_wait(&changed, &gate);
    attempted = 1; pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&gate);
    uint32_t dpc;
    assert(kernel_timer_expire(0, INT64_MAX, &dpc));
    pthread_mutex_lock(&gate);
    expiry_finished = 1; pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&gate);
    return NULL;
}
int main(void)
{
#ifdef NFSU_DISPATCHER_TIMER_TEST
    static volatile LONG preempt;
    xgs_init(&preempt);
#endif
    void *ram = calloc(1, 0x20000);
    assert(ram);
    g_xbox_mem_offset = (ptrdiff_t)ram;
    InitializeCriticalSection(&g_timer_lock);
    g_timer_started = 1;
    event = CreateEventW(NULL, FALSE, FALSE, NULL);
    assert(event);
#ifdef NFSU_DISPATCHER_TIMER_TEST
    assert(host_dispatcher_manages(event));
#endif
    ke_shadow_insert(0x10000, event);
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, expire, NULL));
    kernel_set_timer(0x10000, 0, 0, 0);
    assert(!pthread_join(thread, NULL));
    DWORD result = WaitForSingleObject(event, 0);
    printf("expiry signal after concurrent arm: %s\n", result == WAIT_OBJECT_0 ? "PRESENT" : "LOST");
    fflush(stdout);
    assert(result == WAIT_OBJECT_0);
    assert(BRIDGE_MEM32(0x10004) == 1);
    assert(g_timers[0].timer_va == 0);
    /* Rearm clears both representations; a periodic expiry retains the
     * active entry and reports its DPC for execution outside the lock. */
    kernel_set_timer(0x10000, -10000, 10, 0x1234);
    assert(BRIDGE_MEM32(0x10004) == 0);
    assert(WaitForSingleObject(event, 0) == WAIT_TIMEOUT);
    uint32_t dpc = 0;
    long long now = (long long)GetTickCount64() + 100;
    assert(kernel_timer_expire(0, now, &dpc));
    assert(dpc == 0x1234 && g_timers[0].timer_va == 0x10000);
    assert(g_timers[0].due_ms == now + 10);
    assert(WaitForSingleObject(event, 0) == WAIT_OBJECT_0);
    CloseHandle(event);
    free(ram);
    return 0;
}
