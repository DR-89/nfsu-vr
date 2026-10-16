/* Architectural snapshots preserve the generated register ABI: normal lifted
 * execution still uses the existing TLS globals, with no per-register lookup. */
#include "recomp_context.h"
#include <string.h>
#include <errno.h>

extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp;
extern RECOMP_TLS uint32_t g_seh_ebp;
extern RECOMP_TLS int g_df, g_fp_top, g_fp_cmp;
extern RECOMP_TLS uint16_t g_fp_control_word, g_fp_cc;
extern RECOMP_TLS double g_fp_stack[8];
extern RECOMP_TLS RecompMmx g_mm0, g_mm1, g_mm2, g_mm3, g_mm4, g_mm5, g_mm6, g_mm7;
extern RECOMP_TLS RecompXmm g_xmm0, g_xmm1, g_xmm2, g_xmm3, g_xmm4, g_xmm5, g_xmm6, g_xmm7;
extern ptrdiff_t g_xbox_mem_offset;
static RECOMP_TLS RecompContext default_context;
static RECOMP_TLS RecompContext *current;

RecompContext *recomp_context_current(void)
{
    if (!current) current = &default_context;
    return current;
}

int recomp_context_init(RecompContext *context, uint32_t fs_base)
{
    int saved_errno = errno;
    fenv_t caller, initial;
    if (!context || context == current) return 0;
    if (fegetenv(&caller)) { errno = saved_errno; return 0; }
    if (fesetenv(FE_DFL_ENV)) {
        (void)fesetenv(&caller);
        errno = saved_errno;
        return 0;
    }
    int result = fegetenv(&initial) == 0;
    if (fesetenv(&caller)) result = 0;
    if (result) {
        memset(context, 0, sizeof(*context));
        context->cpu.fs_base = fs_base;
        context->cpu.fp_control_word = 0x037f;
        context->cpu.fp_cc = 0x4000;
        context->host_fenv = initial;
        context->initialized = 1;
        context->identity = context;
    }
    errno = saved_errno;
    return result;
}

static void save_cpu(RecompCpuState *s)
{
#define SAVE(name) s->name = g_##name
    SAVE(eax); SAVE(ecx); SAVE(edx); SAVE(esp); SAVE(ebx); SAVE(esi); SAVE(edi); SAVE(ebp);
    SAVE(fs_base); SAVE(seh_ebp); SAVE(df); SAVE(fp_top); SAVE(fp_cmp);
    SAVE(fp_control_word); SAVE(fp_cc);
#undef SAVE
    memcpy(s->fp_stack, g_fp_stack, sizeof(s->fp_stack));
#define SAVE_VECTOR(i) s->mm[i] = g_mm##i; s->xmm[i] = g_xmm##i
    SAVE_VECTOR(0); SAVE_VECTOR(1); SAVE_VECTOR(2); SAVE_VECTOR(3);
    SAVE_VECTOR(4); SAVE_VECTOR(5); SAVE_VECTOR(6); SAVE_VECTOR(7);
#undef SAVE_VECTOR
}

static void load_cpu(const RecompCpuState *s)
{
#define LOAD(name) g_##name = s->name
    LOAD(eax); LOAD(ecx); LOAD(edx); LOAD(esp); LOAD(ebx); LOAD(esi); LOAD(edi); LOAD(ebp);
    LOAD(fs_base); LOAD(seh_ebp); LOAD(df); LOAD(fp_top); LOAD(fp_cmp);
    LOAD(fp_control_word); LOAD(fp_cc);
#undef LOAD
    memcpy(g_fp_stack, s->fp_stack, sizeof(s->fp_stack));
#define LOAD_VECTOR(i) g_mm##i = s->mm[i]; g_xmm##i = s->xmm[i]
    LOAD_VECTOR(0); LOAD_VECTOR(1); LOAD_VECTOR(2); LOAD_VECTOR(3);
    LOAD_VECTOR(4); LOAD_VECTOR(5); LOAD_VECTOR(6); LOAD_VECTOR(7);
#undef LOAD_VECTOR
}

int recomp_context_switch(RecompContext *next)
{
    RecompContext *previous = recomp_context_current();
    uintptr_t native_owner = (uintptr_t)&default_context;
    if (!next || !next->initialized || next->identity != next ||
        (next->native_owner && next->native_owner != native_owner)) return 0;
    if (next == previous) return 1;
    int saved_errno = errno;
    fenv_t previous_fenv;
    if (fegetenv(&previous_fenv)) {
        errno = saved_errno;
        return 0;
    }
    if (fesetenv(&next->host_fenv)) {
        (void)fesetenv(&previous_fenv);
        errno = saved_errno;
        return 0;
    }
    previous->host_fenv = previous_fenv;
    previous->host_errno = saved_errno;
    save_cpu(&previous->cpu);
    previous->initialized = 1;
    previous->native_owner = native_owner;
    previous->identity = previous;
    next->native_owner = native_owner;
    load_cpu(&next->cpu);
    current = next;
    errno = next->host_errno;
    return 1;
}

jmp_buf *recomp_setjmp_slot(uint32_t buf_va)
{
    RecompContext *context = recomp_context_current();
    for (int i = 0; i < context->jump_count; i++)
        if (context->jumps[i].buf_va == buf_va) return &context->jumps[i].native;
    if (context->jump_count >= RECOMP_JMPBUF_SLOTS)
        context->jump_count = RECOMP_JMPBUF_SLOTS - 1;
    RecompJumpSlot *slot = &context->jumps[context->jump_count++];
    slot->buf_va = buf_va;
    return &slot->native;
}

int recomp_guest_longjmp(uint32_t buf_va, uint32_t value)
{
    RecompContext *context = recomp_context_current();
    const uint8_t *mem = (const uint8_t *)g_xbox_mem_offset;
    for (int i = context->jump_count - 1; i >= 0; i--) {
        if (context->jumps[i].buf_va != buf_va) continue;
        g_ebx = *(const uint32_t *)(mem + buf_va + 0x04);
        g_edi = *(const uint32_t *)(mem + buf_va + 0x08);
        g_esi = *(const uint32_t *)(mem + buf_va + 0x0c);
        g_esp = *(const uint32_t *)(mem + buf_va + 0x10) + 4;
        g_seh_ebp = *(const uint32_t *)(mem + buf_va);
        g_ebp = g_seh_ebp;
        context->jump_count = i + 1;
        longjmp(context->jumps[i].native, value ? (int)value : 1);
    }
    return 0;
}
