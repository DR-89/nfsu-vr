/* Test the actual bridge's scheduler path without an XBE. See docs/guest-scheduler.md. */
#define NFSU_GUEST_SCHEDULER_TEST 1
#define NFSU_PERF_HOST_TEST 1
#include "../xboxrecomp/src/kernel/kernel_bridge.c"
#include "../src/android_perf.c"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>

int xbox_thread_holds_dispatch(void) { return 0; }
#include "recomp_context_registers.h"
static unsigned active, total;
static HANDLE parked_event;
static DWORD parked_tid;
static RecompContext probe_context;
static void probe_kernel_boundary(void)
{
    assert(recomp_context_current()->kernel.native_kernel_frames == 1);
    assert(t_gil_depth == 0); /* Rejection must come from the kernel frame. */
    assert(!recomp_context_switch(&probe_context));
}
static void *wake_parked(void *unused)
{
    (void)unused;
    while (xgs_state(parked_tid) != XGS_WAITING) sched_yield();
    SetEvent(parked_event);
    return NULL;
}
static void *worker(void *unused)
{
    (void)unused;
    for (unsigned i = 0; i < 2000; i++) {
        xbox_gil_enter(); xbox_gil_enter();
        assert(active++ == 0);
        total++;
        assert(--active == 0);
        if (!(i % 17)) recomp_preempt();
        if (!(i % 31)) recomp_spin_yield();
        if (!(i % 113)) {
            int depth = xbox_gil_suspend();
            Sleep(1);
            xbox_gil_resume(depth);
            assert(t_gil_depth == 2);
        }
        xbox_gil_leave(); xbox_gil_leave();
    }
    xgs_unregister();
    return NULL;
}
int main(void)
{
    setenv("RECOMP_GUEST_SCHEDULER", "1", 1);
    setenv("RECOMP_GIL_MAIN_QUANTUM_US", "250", 1); /* Ignored by the scheduler. */
    nfsu_perf_enabled = 1;
    assert(gil_enabled() && xbox_guest_scheduler_active());
    xbox_gil_mark_main();
    parked_event = CreateEventW(NULL, FALSE, TRUE, NULL);
    unsigned char ram[0x12000] = {0};
    g_xbox_mem_offset = (ptrdiff_t)ram;
    g_esp = 0x10000;
    BRIDGE_MEM32(g_esp) = 0xDEADC0DE; /* Return address is not argument zero. */
    assert(recomp_context_init(&probe_context, 0x8000));
    g_kernel_dispatch_slot = 0;
    g_slot_ordinals[0] = 125;
    g_slot_bridges[0] = probe_kernel_boundary;
    kernel_thunk_dispatch();
    assert(!recomp_context_current()->kernel.native_kernel_frames);
    assert(g_esp == 0x10004);
    g_esp = 0x10000;
    BRIDGE_MEM32(g_esp + 4) = bridge_handle_token(parked_event);
    assert(dispatcher_call_keeps_gil(234));
    assert(dispatcher_call_keeps_gil(225));
    assert(!dispatcher_call_keeps_gil(99));
    BRIDGE_MEM32(g_esp + 4) = 0;
    assert(!dispatcher_call_keeps_gil(234));
    ke_shadow_insert(0x11000, parked_event);
    BRIDGE_MEM32(g_esp + 4) = 0x11000;
    assert(dispatcher_call_keeps_gil(159));
    assert(dispatcher_call_keeps_gil(145));
    BRIDGE_MEM32(g_esp + 4) = 2;
    BRIDGE_MEM32(g_esp + 8) = 0x11100;
    BRIDGE_MEM32(0x11100) = bridge_handle_token(parked_event);
    BRIDGE_MEM32(0x11104) = bridge_handle_token(parked_event);
    assert(dispatcher_call_keeps_gil(235));
    HANDLE native_mutex = CreateMutexW(NULL, FALSE, NULL);
    BRIDGE_MEM32(0x11104) = bridge_handle_token(native_mutex);
    assert(!dispatcher_call_keeps_gil(235));
    CloseHandle(native_mutex);
    /* Inline XDK objects have no KeInitialize* bridge call. Their guest
     * header remains the authoritative state after lazy adoption. */
    BRIDGE_MEM8(0x11200) = KDISP_EVENT_SYNCHRONIZE;
    BRIDGE_MEM8(0x11202) = 4;
    BRIDGE_MEM32(g_esp + 4) = 0x11200;
    assert(dispatcher_call_keeps_gil(159));
    HANDLE inline_event = ke_shadow_lookup(0x11200);
    assert(inline_event && !kdisp_is_guest(0x11200));
    SetEvent(inline_event);
    assert(BRIDGE_MEM32(0x11204) == 1);
    BRIDGE_MEM32(0x11204) = 0; /* Inline KeClearEvent must clear the real latch. */
    assert(WaitForSingleObject(inline_event, 0) == WAIT_TIMEOUT);
    SetEvent(inline_event);
    assert(WaitForSingleObject(inline_event, 0) == WAIT_OBJECT_0);
    assert(BRIDGE_MEM32(0x11204) == 0);
    BRIDGE_MEM8(0x11200) = KDISP_EVENT_NOTIFICATION; /* Inline reinitialization. */
    SetEvent(inline_event);
    assert(WaitForSingleObject(inline_event, 0) == WAIT_OBJECT_0);
    assert(WaitForSingleObject(inline_event, 0) == WAIT_OBJECT_0);
    ResetEvent(inline_event);
    assert(BRIDGE_MEM32(0x11204) == 0);
    BRIDGE_MEM8(0x11300) = KDISP_SEMAPHORE;
    BRIDGE_MEM8(0x11302) = 5;
    BRIDGE_MEM32(0x11304) = 1;
    BRIDGE_MEM32(0x11310) = 2;
    BRIDGE_MEM32(g_esp + 4) = 0x11300;
    assert(dispatcher_call_keeps_gil(159));
    HANDLE inline_sem = ke_shadow_lookup(0x11300);
    assert(WaitForSingleObject(inline_sem, 0) == WAIT_OBJECT_0);
    assert(BRIDGE_MEM32(0x11304) == 0);
    assert(ReleaseSemaphore(inline_sem, 2, NULL));
    assert(BRIDGE_MEM32(0x11304) == 2);
    assert(!ReleaseSemaphore(inline_sem, 1, NULL));
    assert(BRIDGE_MEM32(0x11304) == 2);
    BRIDGE_MEM32(0x11310) = 3;
    assert(ReleaseSemaphore(inline_sem, 1, NULL));
    assert(BRIDGE_MEM32(0x11304) == 3);
    ke_shadow_remove(0x11200);
    ke_shadow_remove(0x11300);
    xbox_gil_enter(); xbox_gil_enter();
    RecompContext fresh;
    assert(recomp_context_init(&fresh, 0x7000));
    assert(!recomp_context_switch(&fresh)); /* Native scheduler still owns this GIL. */
    assert(t_gil_depth == 2);
    uint64_t before = nfsu_perf_gil_wait_sum();
    assert(WaitForSingleObject(parked_event, INFINITE) == WAIT_OBJECT_0);
    assert(before == nfsu_perf_gil_wait_sum()); /* Ready object never drops the token. */
    parked_tid = GetCurrentThreadId();
    pthread_t signaler;
    assert(!pthread_create(&signaler, NULL, wake_parked, NULL));
    assert(WaitForSingleObject(parked_event, INFINITE) == WAIT_OBJECT_0);
    assert(t_gil_depth == 2 && t_gil_perf_held);
    assert(nfsu_perf_gil_wait_sum() > before);
    xbox_gil_leave(); xbox_gil_leave();
    assert(!pthread_join(signaler, NULL));
    CloseHandle(parked_event);
    pthread_t threads[7];
    for (unsigned i = 0; i < 7; i++) assert(!pthread_create(&threads[i], NULL, worker, NULL));
    worker(NULL);
    for (unsigned i = 0; i < 7; i++) assert(!pthread_join(threads[i], NULL));
    assert(total == 16000 && active == 0);
    puts("PASS: bridge scheduler path, exclusion, nesting, preempt/poll, host-call waits, 16000 entries");
    return 0;
}
