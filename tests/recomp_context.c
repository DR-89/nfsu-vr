/* cc -O1 -Wall -Wextra -Werror -ffunction-sections -fdata-sections
 * -Ixboxrecomp/src tests/recomp_context.c
 * xboxrecomp/src/kernel/recomp_context.c -Wl,--gc-sections -pthread -lm
 * -o /tmp/nfsu-recomp-context-test */
#include "kernel/recomp_context.h"
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>

#include "recomp_context_registers.h"
static RecompContext a, b;

static void *foreign_thread(void *unused)
{
    (void)unused;
    RecompContext local;
    assert(recomp_context_init(&local, 0x9000));
    assert(recomp_context_switch(&local));
    g_eax = 99;
    assert(!recomp_context_switch(&a)); /* Live native jump targets cannot migrate. */
    assert(g_eax == 99 && recomp_context_current() == &local);
    return NULL;
}

int main(void)
{
    uint32_t ram[64] = {0};
    g_xbox_mem_offset = (ptrdiff_t)ram;
    RecompContext *original = recomp_context_current();
    g_eax = 123;
    errno = EDOM;
    SetLastError(1234);
    assert(fesetround(FE_DOWNWARD) == 0);
    assert(recomp_context_init(&a, 0x5000));
    assert(recomp_context_init(&b, 0x6000));
    assert(g_eax == 123 && errno == EDOM && GetLastError() == 1234 && fegetround() == FE_DOWNWARD);
    assert(recomp_context_switch(&a));
    assert(g_eax == 0 && g_fs_base == 0x5000 && g_fp_control_word == 0x037f);
    assert(g_fp_cc == 0x4000 && fegetround() == FE_TONEAREST);
    assert(!recomp_context_init(&a, 0)); /* Must not erase the active context. */
    g_eax = 1; g_ecx = 2; g_edx = 3; g_esp = 4;
    g_ebx = 5; g_esi = 6; g_edi = 7; g_ebp = 8;
    g_seh_ebp = 9; g_df = 1; g_fp_top = 7; g_fp_cmp = -1;
    g_fp_control_word = 0x077f; g_fp_cc = 0x4500;
    uint64_t nan_payload = 0x7ff8000000001234ull;
    memcpy(&g_fp_stack[7], &nan_payload, sizeof(nan_payload));
    g_mm7.q = 0x0123456789abcdefull;
    g_xmm7.q[0] = 0x7ff800000000aabbull; g_xmm7.q[1] = ~0ull;
    errno = ERANGE;
    SetLastError(5678);
    a.kernel.main_thread = 1;
    a.kernel.kernel_caller = 0x123456;
    a.kernel.stack_top = 0x90000;
    a.kernel.dispatch_slot = 17;
    strcpy(a.kernel.path, "context A");
    int *guards[] = {&a.kernel.gil_depth, &a.kernel.gil_nopreempt, &a.kernel.in_vblank_isr,
        &a.kernel.irql, &a.kernel.dispatch_held, &a.kernel.busy_depth,
        &a.kernel.native_kernel_frames, &a.kernel.guest_cs_depth};
    for (unsigned i = 0; i < sizeof(guards) / sizeof(guards[0]); i++) {
        *guards[i] = 1;
        assert(!recomp_context_switch(&b));
        assert(recomp_context_current() == &a && g_eax == 1 && GetLastError() == 5678);
        *guards[i] = 0;
    }
    b.kernel.irql = 2;
    assert(!recomp_context_switch(&b));
    b.kernel.irql = 0;
    a.kernel.native_kernel_frames = a.kernel.busy_depth = 1;
    assert(!recomp_context_can_switch(&b));
    a.kernel.continuation_wait_scope = 1;
    assert(recomp_context_can_switch(&b));
    for (unsigned i = 0; i < sizeof(guards) / sizeof(guards[0]); i++) {
        if (guards[i] == &a.kernel.native_kernel_frames || guards[i] == &a.kernel.busy_depth) continue;
        *guards[i] = 1;
        assert(!recomp_context_can_switch(&b));
        *guards[i] = 0;
    }
    a.kernel.continuation_wait_scope = a.kernel.native_kernel_frames = a.kernel.busy_depth = 0;
    assert(fesetround(FE_UPWARD) == 0);
    assert(feraiseexcept(FE_INVALID) == 0);
    jmp_buf *a_slot = recomp_setjmp_slot(32);
    int jumped = setjmp(*a_slot);
    if (!jumped) {
        (void)recomp_setjmp_slot(64); /* Inner target should disappear on longjmp. */
        assert(recomp_context_switch(&b));
        assert(g_eax == 0 && g_fs_base == 0x6000 && errno == 0 && GetLastError() == 0);
        assert(!recomp_context_current()->kernel.main_thread && b.kernel.dispatch_slot == -1);
        strcpy(b.kernel.path, "context B");
        assert(fegetround() == FE_TONEAREST && !(fetestexcept(FE_INVALID)));
        jmp_buf *b_slot = recomp_setjmp_slot(32); /* Same guest VA, independent target. */
        assert(b_slot != a_slot);
        assert(setjmp(*b_slot) == 0);
        g_eax = 456; g_df = 0; g_xmm7.q[0] = 0;
        assert(recomp_context_switch(&a));
        assert(g_eax == 1 && g_ecx == 2 && g_edx == 3 && g_esp == 4);
        assert(g_ebx == 5 && g_esi == 6 && g_edi == 7 && g_ebp == 8);
        assert(g_fs_base == 0x5000 && g_seh_ebp == 9 && g_df == 1);
        assert(g_fp_top == 7 && g_fp_cmp == -1 && g_fp_control_word == 0x077f && g_fp_cc == 0x4500);
        assert(memcmp(&g_fp_stack[7], &nan_payload, 8) == 0);
        assert(g_mm7.q == 0x0123456789abcdefull && g_xmm7.q[0] == 0x7ff800000000aabbull && g_xmm7.q[1] == ~0ull);
        assert(errno == ERANGE && GetLastError() == 5678 && fegetround() == FE_UPWARD && fetestexcept(FE_INVALID));
        assert(recomp_context_current()->kernel.kernel_caller == 0x123456);
        assert(a.kernel.stack_top == 0x90000 && a.kernel.dispatch_slot == 17);
        assert(!strcmp(a.kernel.path, "context A"));
        assert(recomp_setjmp_slot(32) == a_slot && a.jump_count == 2);
        /* Guest jmp_buf fields: ebp, ebx, edi, esi, esp. */
        ram[8] = 11; ram[9] = 12; ram[10] = 13; ram[11] = 14; ram[12] = 100;
        recomp_guest_longjmp(32, 7);
        assert(0);
    }
    assert(jumped == 7 && a.jump_count == 1);
    assert(g_ebp == 11 && g_seh_ebp == 11 && g_ebx == 12 && g_edi == 13 && g_esi == 14 && g_esp == 104);
    assert(recomp_guest_longjmp(64, 1) == 0);
    assert(recomp_context_switch(&b));
    assert(g_eax == 456 && g_df == 0 && b.jump_count == 1);
    assert(!strcmp(b.kernel.path, "context B"));
    assert(recomp_context_switch(original));
    assert(g_eax == 123 && errno == EDOM && fegetround() == FE_DOWNWARD);
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, foreign_thread, NULL));
    assert(!pthread_join(thread, NULL));
    assert(g_eax == 123 && recomp_context_current() == original);
    assert(!recomp_context_switch(NULL));
    RecompContext moved = a;
    assert(!recomp_context_switch(&moved));
    puts("PASS: CPU/FS/SEH, x87/MMX/SSE bits, rounding/exceptions/errno, stable per-context longjmp, native ownership, unchanged default context");
    return 0;
}
