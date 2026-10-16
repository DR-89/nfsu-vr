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

typedef struct {
    RecompCpuState cpu;
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
 * Switching contexts does not switch C stacks or kernel/platform TLS. */
RecompContext *recomp_context_current(void);
int recomp_context_init(RecompContext *context, uint32_t fs_base);
int recomp_context_switch(RecompContext *next);
jmp_buf *recomp_setjmp_slot(uint32_t buf_va);
int recomp_guest_longjmp(uint32_t buf_va, uint32_t value);
#ifdef __cplusplus
}
#endif
#endif
