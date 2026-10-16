/* Test the actual bridge's scheduler path without an XBE. See docs/guest-scheduler.md. */
#define NFSU_GUEST_SCHEDULER_TEST 1
#define NFSU_PERF_HOST_TEST 1
#include "../xboxrecomp/src/kernel/kernel_bridge.c"
#include "../src/android_perf.c"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>

int xbox_thread_holds_dispatch(void) { return 0; }
ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp;
static unsigned active, total;
static HANDLE parked_event;
static DWORD parked_tid;
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
    xbox_gil_enter(); xbox_gil_enter();
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
