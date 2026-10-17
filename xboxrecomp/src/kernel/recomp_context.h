#ifndef RECOMP_CONTEXT_H
#define RECOMP_CONTEXT_H
#include "xbox_memory_layout.h"
#include <setjmp.h>
#include <fenv.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RECOMP_JMPBUF_SLOTS 32

typedef struct {
    uint32_t eax, ecx, edx, esp, ebx, esi, edi, ebp;
    uint32_t fs_base, seh_ebp;
    int df;
    double fp_stack[8];
    int fp_top, fp_cmp;
    uint16_t fp_control_word, fp_cc;
    RecompMmx mm[8];
    RecompXmm xmm[8];
} RecompCpuState;

typedef struct {
    uint32_t buf_va;
    jmp_buf native;
} RecompJumpSlot;

/* Guest-owned kernel state. Native thread handles/IDs, native lock ownership
 * and the scheduler's pthread_key are deliberately not virtualized here. */
typedef struct {
    int gil_depth, gil_nopreempt, main_thread;
    int spawned_thread, in_vblank_isr, dispatch_slot;
    int irql, dispatch_held, busy_depth, native_kernel_frames, guest_cs_depth;
    /* Granted by audited kernel wait entry points, never by arbitrary native
     * frames. busy/frame counters are bookkeeping; native ownership guards
     * (GIL, IRQL, host-backed guest CS) still forbid switching. */
    int continuation_wait_scope;
    uint32_t kernel_caller, stack_top;
    uint64_t quantum_start, perf_held;
    void *busy_slot;
    char path[65536];
} RecompKernelState;

typedef struct {
    RecompCpuState cpu;
    RecompKernelState kernel;
    uint32_t host_last_error;
    fenv_t host_fenv;
    int host_errno;
    int initialized;
    uintptr_t native_owner;
    const void *identity;
    /* Native jump buffers have stable addresses for the lifetime of the
     * associated native stack. Never copy/move an active context. */
    RecompJumpSlot jumps[RECOMP_JMPBUF_SLOTS];
    int jump_count;
} RecompContext;

/* Existing native threads use a private default context automatically.
 * Switching contexts does not switch C stacks or native thread identity. */
extern RECOMP_TLS RecompContext g_recomp_default_context;
extern RECOMP_TLS RecompContext *g_recomp_current_context;
static inline RecompContext *recomp_context_current(void)
{
    if (!g_recomp_current_context) g_recomp_current_context = &g_recomp_default_context;
    return g_recomp_current_context;
}
int recomp_context_init(RecompContext *context, uint32_t fs_base);
int recomp_context_switch(RecompContext *next);
int recomp_context_can_switch(const RecompContext *next);
jmp_buf *recomp_setjmp_slot(uint32_t buf_va);
int recomp_guest_longjmp(uint32_t buf_va, uint32_t value);
#ifdef __cplusplus
}
#endif
#endif
