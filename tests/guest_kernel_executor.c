/* Actual thunk dispatch, GIL boundary, RTL and kernel wait/sleep helpers on
 * stackful virtual threads. No XBE and no Android runtime is required. */
#define NFSU_GUEST_EXECUTOR 1
#define NFSU_GUEST_SCHEDULER_TEST 1
#define NFSU_PERF_HOST_TEST 1
#include "../xboxrecomp/src/kernel/kernel_bridge.c"
#include "../src/android_perf.c"
#include "kernel/guest_dispatcher.h"
#include "recomp_context_registers.h"
#include <assert.h>
#include <pthread.h>
#include <unistd.h>

uint32_t g_xbox_code_lo, g_xbox_code_hi;
void xbox_log(int level, const char *system, const char *format, ...)
{ (void)level; (void)system; (void)format; }
static HANDLE ready, mutant;
static unsigned waiter_acquired, owner_left;
static pthread_t owner_native;
static unsigned freed_stacks, returned_worker, terminated_worker;
static int device_isr(void *argument)
{
    assert(pthread_equal(owner_native, pthread_self()));
    assert(GetCurrentThreadId() != *(DWORD *)argument && g_fs_base == 0x8f00);
    XgfFiber *self = xgf_current();
    xbox_gil_isr_begin();
    recomp_spin_yield();
    assert(xgf_current() == self && t_gil_nopreempt == 1);
    xbox_gil_isr_end();
    SetEvent(ready);
    return 73;
}
static void *device_thread(void *unused)
{
    (void)unused;
    g_fs_base = 0x8f00;
    DWORD native_id = GetCurrentThreadId();
    int result;
    assert(xbox_guest_executor_invoke(device_isr, &native_id, &result) && result == 73);
    assert(GetCurrentThreadId() == native_id && g_fs_base == 0x8f00);
    return NULL;
}
uint32_t xbox_AllocThreadTib(void)
{ static uint32_t next = 0x8000; next += 0x100; return next; }
void xbox_FreeThreadStack(uint32_t top)
{ assert(top == 0x18000 || top == 0x1a000); freed_stacks++; }
static uint32_t call(unsigned slot, unsigned count, const uint32_t *args)
{
    int frames = recomp_context_current()->kernel.native_kernel_frames;
    int busy = recomp_context_current()->kernel.busy_depth;
    uint32_t old_sp = g_esp;
    g_esp -= (count + 1) * 4;
    BRIDGE_MEM32(g_esp) = 0;
    for (unsigned i = 0; i < count; i++) BRIDGE_MEM32(g_esp + 4 + i * 4) = args[i];
    g_kernel_dispatch_slot = slot;
    kernel_thunk_dispatch();
    assert(t_gil_depth == 1);
    assert(recomp_context_current()->kernel.native_kernel_frames == frames);
    assert(!recomp_context_current()->kernel.continuation_wait_scope);
    assert(recomp_context_current()->kernel.busy_depth == busy);
    assert(pthread_equal(owner_native, pthread_self()));
    g_esp = old_sp;
    return g_eax;
}
static DWORD WINAPI holder(void *unused)
{
    (void)unused;
    g_esp = 0x10000;
    xbox_gil_enter();
    /* An audited inner Wait must not authorize an unaudited outer native
     * callback frame to migrate, even though the object itself is managed. */
    RecompKernelState *k = &recomp_context_current()->kernel;
    k->native_kernel_frames++;
    xbox_kernel_busy(1);
    const uint32_t unsafe_wait[] = {0x6000, 0, 0, 0, 0};
    assert(call(3, 5, unsafe_wait) == (uint32_t)STATUS_UNSUCCESSFUL);
    xbox_kernel_busy(-1);
    k->native_kernel_frames--;
    const uint32_t cs[] = {0x5000};
    call(0, 1, cs);
    assert(call(2, 1, cs)); /* TryEnter recurses on the SAME shadow. */
    assert(!recomp_context_current()->kernel.guest_cs_depth);
    const uint32_t wait[] = {0x6000, 0, 0, 0, 0};
    uint64_t canary = 0xfedcba9876543210ull;
    assert(call(3, 5, wait) == 0); /* Wait while owning a guest CS. */
    assert(canary == 0xfedcba9876543210ull && !waiter_acquired);
    call(1, 1, cs);
    recomp_spin_yield();
    assert(!waiter_acquired); /* One recursion is still owned. */
    call(1, 1, cs);
    owner_left = 1;
    xbox_gil_leave();
    return 11;
}
static DWORD WINAPI contender(void *unused)
{
    (void)unused;
    g_esp = 0x12000;
    xbox_gil_enter();
    const uint32_t cs[] = {0x5000};
    assert(!call(2, 1, cs));
    call(0, 1, cs); /* Must park, not recursively acquire by pthread identity. */
    assert(owner_left);
    waiter_acquired++;
    call(1, 1, cs);
    xbox_gil_leave();
    return 13;
}
static DWORD WINAPI signaler(void *unused)
{
    (void)unused;
    g_esp = 0x14000;
    xbox_gil_enter();
    const uint32_t delay[] = {0, 0, 0x7000};
    assert(call(4, 3, delay) == 0);
    SetEvent(ready);
    xbox_gil_leave();
    return 17;
}
static DWORD WINAPI abandoner(void *unused)
{
    (void)unused;
    mutant = CreateMutexW(NULL, TRUE, NULL);
    assert(mutant && WaitForSingleObject(mutant, 0) == WAIT_OBJECT_0);
    assert(ReleaseMutex(mutant)); /* One of two recursions remains. */
    assert(xgf_yield());
    return 19;
}
static DWORD WINAPI abandoned_waiter(void *unused)
{
    (void)unused;
    assert(!ReleaseMutex(mutant) && GetLastError() == ERROR_NOT_OWNER);
    assert(WaitForSingleObject(mutant, INFINITE) == WAIT_ABANDONED_0);
    assert(ReleaseMutex(mutant));
    assert(!ReleaseMutex(mutant));
    return 23;
}
static void guest_worker_return(void)
{
    assert(BRIDGE_MEM32(g_esp + 4) == 51 && BRIDGE_MEM32(g_esp + 8) == 53);
    assert(g_fs_base >= 0x8100 && g_is_spawned_thread);
    assert(!recomp_context_current()->kernel.native_kernel_frames);
    recomp_spin_yield();
    returned_worker++;
}
static void guest_worker_terminate(void)
{
    terminated_worker++;
    const uint32_t exit[] = {7};
    call(6, 1, exit);
    assert(0);
}
static void guest_main_loop(void)
{
    RecompKernelState *k = &recomp_context_current()->kernel;
    assert(!k->native_kernel_frames && !k->busy_depth && t_gil_depth == 1);
    HANDLE workers[] = {
        bridge_spawn_thread(guest_worker_return, 51, 53, 0x18000, 0),
        bridge_spawn_thread(guest_worker_terminate, 0, 0, 0x1a000, 1)
    };
    assert(workers[0] && workers[1]);
    pending_start_flush(GetCurrentThreadId());
    assert(ResumeThread(workers[1]) == 1);
    int depth = xbox_gil_suspend();
    assert(WaitForMultipleObjects(2, workers, TRUE, INFINITE) == WAIT_OBJECT_0);
    xbox_gil_resume(depth);
    DWORD code;
    assert(GetExitCodeThread(workers[1], &code) && code == 7);
    CloseHandle(workers[0]); CloseHandle(workers[1]);
    assert(returned_worker == 1 && terminated_worker == 1 && freed_stacks == 2);
    depth = xbox_gil_suspend();
    assert(WaitForSingleObject(ready, INFINITE) == WAIT_OBJECT_0);
    xbox_gil_resume(depth);
}
static void initial_thunk(void) { bridge_run_initial_guest(guest_main_loop); }
static void title_boot(void) { call(5, 0, NULL); }
int main(void)
{
    alarm(15);
    setenv("RECOMP_QUIET", "1", 1);
    setenv("RECOMP_KERNEL_LOG_BUDGET", "0", 1);
    g_xbox_mem_offset = (ptrdiff_t)calloc(1, 0x20000);
    assert(g_xbox_mem_offset);
    *(int64_t *)((uintptr_t)g_xbox_mem_offset + 0x7000) = -30000;
    owner_native = pthread_self();
    DWORD native_tid = GetCurrentThreadId();
    xgs_init((volatile LONG *)&g_gil_contended);
    assert(xgf_use_dispatcher_waits());
    s_guest_scheduler_on = 1; /* Prove native scheduler paths are bypassed. */
    ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    ke_shadow_insert(0x6000, ready);
    const ULONG ordinals[] = {277, 294, 306, 159, 99};
    bridge_func_t bridges[] = {bridge_RtlEnterCriticalSection, bridge_RtlLeaveCriticalSection,
        bridge_RtlTryEnterCriticalSection, bridge_KeWaitForSingleObject, bridge_KeDelayExecutionThread};
    for (unsigned i = 0; i < 5; i++) { g_slot_ordinals[i] = ordinals[i]; g_slot_bridges[i] = bridges[i]; }
    XgfExecutor *executor = xgf_executor_create(); assert(executor);
    XgfFiber *fibers[3]; HANDLE handles[3];
    LPTHREAD_START_ROUTINE entries[] = {holder, contender, signaler};
    for (unsigned i = 0; i < 3; i++) {
        handles[i] = xgf_create_thread(executor, entries[i], NULL, 256 * 1024, 0x4000, 0, NULL, &fibers[i]);
        assert(handles[i]);
    }
    assert(xgf_executor_run(executor, 3000, xgd_poll_timeouts) == XGF_RUN_DONE);
    assert(waiter_acquired == 1 && GetCurrentThreadId() == native_tid);
    for (unsigned i = 0; i < 3; i++) { assert(xgf_destroy(fibers[i])); CloseHandle(handles[i]); }
    handles[0] = xgf_create_thread(executor, abandoner, NULL, 256 * 1024, 0, 0, NULL, &fibers[0]);
    handles[1] = xgf_create_thread(executor, abandoned_waiter, NULL, 256 * 1024, 0, 0, NULL, &fibers[1]);
    assert(handles[0] && handles[1]);
    assert(xgf_executor_run(executor, 3000, xgd_poll_timeouts) == XGF_RUN_DONE);
    for (unsigned i = 0; i < 2; i++) { assert(xgf_destroy(fibers[i])); CloseHandle(handles[i]); }
    CloseHandle(mutant);
    assert(xgf_executor_destroy(executor));
    g_slot_ordinals[5] = 255; g_slot_bridges[5] = initial_thunk;
    g_slot_ordinals[6] = 258; g_slot_bridges[6] = bridge_PsTerminateSystemThread;
    setenv("RECOMP_GUEST_EXECUTOR", "1", 1);
    assert(xbox_guest_executor_prepare());
    ResetEvent(ready);
    pthread_t device;
    assert(!pthread_create(&device, NULL, device_thread, NULL));
    assert(xbox_guest_executor_run(title_boot, 0x10000, 0x4000));
    assert(!pthread_join(device, NULL));
    assert(GetCurrentThreadId() == native_tid);
    free((void *)g_xbox_mem_offset);
    puts("PASS: real kernel thunk waits/delays, virtual GIL, contended recursive RTL and TryEnter/Leave, owner abandonment and preserved native thread");
}
