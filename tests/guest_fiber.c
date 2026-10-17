#include "kernel/guest_fiber.h"
#include "kernel/recomp_context.h"
#include "recomp_context_registers.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static XgfExecutor *executor;
static XgfFiber *fibers[2];
static DWORD native_tid;
static unsigned steps[2];
static uintptr_t stack_address[2];
static uint32_t ram[128];

typedef struct { unsigned side, rounds; } Start;
static Start starts[2] = {{0, 1000}, {1, 1000}};

__attribute__((noinline))
static void nested(unsigned side, unsigned depth)
{
    volatile uint64_t canary[16];
    for (unsigned i = 0; i < 16; i++) canary[i] = (uint64_t)side << 32 | depth << 16 | i;
    if (depth) nested(side, depth - 1);
    else {
        stack_address[side] = (uintptr_t)&canary[0];
        for (unsigned i = 0; i < starts[side].rounds; i++) {
            assert(GetCurrentThreadId() == native_tid);
            g_eax = 0x1000 + side; g_esp = 0x2000 + side;
            g_xmm7.q[0] = 0x7ff8000000000000ull + side;
            g_fp_stack[3] = 42.0 + side;
            recomp_context_current()->kernel.kernel_caller = 0x3000 + side;
            strcpy(recomp_context_current()->kernel.path, side ? "second" : "first");
            SetLastError(100 + side); errno = side ? ERANGE : EDOM;
            assert(!fesetround(side ? FE_UPWARD : FE_DOWNWARD));
            steps[side]++;
            assert(xgf_yield());
            assert(g_eax == 0x1000 + side && g_esp == 0x2000 + side);
            assert(g_xmm7.q[0] == 0x7ff8000000000000ull + side);
            assert(g_fp_stack[3] == 42.0 + side && g_fs_base == 0x4000 + side);
            assert(recomp_context_current()->kernel.kernel_caller == 0x3000 + side);
            assert(!strcmp(recomp_context_current()->kernel.path, side ? "second" : "first"));
            assert(GetLastError() == 100 + side && errno == (side ? ERANGE : EDOM));
            assert(fegetround() == (side ? FE_UPWARD : FE_DOWNWARD));
        }
    }
    for (unsigned i = 0; i < 16; i++)
        assert(canary[i] == ((uint64_t)side << 32 | depth << 16 | i));
}
static void entry(void *argument)
{
    Start *s = argument;
    unsigned side = s->side;
    assert(xgf_state(fibers[side]) == XGF_RUNNING);
    assert(!xgf_destroy(fibers[side]) && !xgf_executor_destroy(executor));
    assert(xgf_resume(executor, fibers[1 - side]) == XGF_ERROR);
    /* A guest lock/raised section must not be left on another C stack. */
    recomp_context_current()->kernel.guest_cs_depth = 1;
    assert(!xgf_yield());
    recomp_context_current()->kernel.guest_cs_depth = 0;
    /* Both fibers use the same guest jump-buffer VA on different C stacks. */
    int result = setjmp(*recomp_setjmp_slot(64));
    if (!result) {
        nested(side, 8);
        recomp_guest_longjmp(64, 7);
        assert(0);
    }
    assert(result == 7);
    assert(g_ebp == 11 && g_esp == 104);
}
static void *foreign(void *unused)
{
    (void)unused;
    assert(xgf_resume(executor, fibers[0]) == XGF_ERROR);
    assert(!xgf_destroy(fibers[0]) && !xgf_executor_destroy(executor));
    return NULL;
}
static void never_run(void *unused) { (void)unused; assert(0); }
int main(void)
{
    g_xbox_mem_offset = (ptrdiff_t)ram;
    ram[16] = 11; ram[17] = 12; ram[18] = 13; ram[19] = 14; ram[20] = 100;
    native_tid = GetCurrentThreadId();
    RecompContext *original = recomp_context_current();
    executor = xgf_executor_create(); assert(executor);
    assert(!xgf_yield());
    assert(!xgf_create(executor, NULL, NULL, 0, 0));
    assert(!xgf_create(executor, never_run, NULL, SIZE_MAX, 0));
    XgfFiber *unused = xgf_create(executor, never_run, NULL, 0, 0);
    assert(unused && xgf_destroy(unused));
    for (unsigned i = 0; i < 2; i++) {
        fibers[i] = xgf_create(executor, entry, &starts[i], 256 * 1024, 0x4000 + i);
        assert(fibers[i] && xgf_state(fibers[i]) == XGF_NEW);
    }
    assert(!xgf_executor_destroy(executor));
    original->kernel.gil_depth = 1;
    assert(xgf_resume(executor, fibers[0]) == XGF_ERROR);
    assert(xgf_state(fibers[0]) == XGF_NEW && recomp_context_current() == original);
    original->kernel.gil_depth = 0;
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, foreign, NULL));
    assert(!pthread_join(thread, NULL));
    for (unsigned i = 0; i < 1000; i++) {
        for (unsigned side = 0; side < 2; side++) {
            g_eax = 0xaabbccdd;
            SetLastError(321); errno = EINVAL;
            assert(!fesetround(FE_TONEAREST));
            assert(xgf_resume(executor, fibers[side]) == XGF_SUSPENDED);
            assert(steps[side] == i + 1);
            assert(recomp_context_current() == original && g_eax == 0xaabbccdd);
            assert(GetLastError() == 321 && errno == EINVAL && fegetround() == FE_TONEAREST);
            assert(!xgf_destroy(fibers[side]));
        }
    }
    assert(stack_address[0] != stack_address[1]);
    for (unsigned side = 0; side < 2; side++) {
        assert(xgf_resume(executor, fibers[side]) == XGF_EXITED);
        assert(xgf_resume(executor, fibers[side]) == XGF_ERROR);
        assert(xgf_destroy(fibers[side]));
    }
    assert(xgf_executor_destroy(executor));
    puts("PASS: two C stacks, 2000 yields, nested locals, per-fiber longjmp/registers/FP/kernel state, root restoration, exit/lifecycle and native ownership");
    return 0;
}
