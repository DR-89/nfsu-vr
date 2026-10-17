/* Real HAL/RTL/thread helpers against explicit contexts; native GIL callbacks
 * are forbidden here because a single test thread never contends DISPATCH. */
#include "kernel/kernel.h"
#include "kernel/recomp_context.h"
#include "recomp_context_registers.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

uint32_t g_xbox_code_lo, g_xbox_code_hi;
int xbox_gil_suspend(void) { assert(0); return 0; }
void xbox_gil_resume(int depth) { (void)depth; assert(0); }
void xbox_log(int level, const char *subsystem, const char *format, ...)
{ (void)level; (void)subsystem; (void)format; assert(0); }
static RecompContext a, b;
int main(void)
{
    void *ram = calloc(1, 0x20000);
    assert(ram);
    g_xbox_mem_offset = (ptrdiff_t)ram;
    DWORD identity = GetCurrentThreadId();
    HANDLE native = GetCurrentThread();
    assert(recomp_context_init(&a, 0x4000) && recomp_context_init(&b, 0x5000));
    RecompContext *original = recomp_context_current();
    assert(recomp_context_switch(&a));
    assert(xbox_KfRaiseIrql(2) == 0);
    assert(a.kernel.irql == 2 && a.kernel.dispatch_held && a.kernel.busy_depth == 1);
    assert(!recomp_context_switch(&b));
    xbox_KfLowerIrql(0);
    assert(!a.kernel.irql && !a.kernel.dispatch_held && !a.kernel.busy_depth);
    xbox_kernel_busy(1);
    assert(a.kernel.busy_slot && a.kernel.busy_depth == 1);
    assert(!recomp_context_switch(&b));
    xbox_kernel_busy(-1);
    PRTL_CRITICAL_SECTION cs = (PRTL_CRITICAL_SECTION)((unsigned char *)ram + 0x11000);
    xbox_RtlInitializeCriticalSection(cs);
    xbox_RtlEnterCriticalSection(cs);
    assert(xbox_RtlTryEnterCriticalSection(cs));
    assert(a.kernel.guest_cs_depth == 2 && !recomp_context_switch(&b));
    xbox_RtlLeaveCriticalSection(cs);
    assert(a.kernel.guest_cs_depth == 1 && !recomp_context_switch(&b));
    xbox_RtlLeaveCriticalSection(cs);
    assert(!a.kernel.guest_cs_depth);
    void *a_slot = a.kernel.busy_slot;
    SetLastError(42);
    assert(recomp_context_switch(&b));
    assert(!b.kernel.busy_slot && !b.kernel.busy_depth && GetLastError() == 0);
    assert(GetCurrentThreadId() == identity && GetCurrentThread() == native);
    xbox_kernel_busy(1); xbox_kernel_busy(-1);
    assert(recomp_context_switch(&a));
    assert(a.kernel.busy_slot == a_slot && GetLastError() == 42);
    assert(GetCurrentThreadId() == identity && GetCurrentThread() == native);
    assert(recomp_context_switch(original));
    free(ram);
    puts("PASS: real IRQL/DISPATCH lock, kernel busy slot, recursive RTL locks, last-error isolation, preserved native identity");
    return 0;
}
