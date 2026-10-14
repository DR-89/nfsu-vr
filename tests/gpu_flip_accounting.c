/* Run the real ISR wrappers with an interleaving during IRQL acquisition.
 * cc -ffunction-sections -fdata-sections -Isrc/recomp/gen
 *    tests/gpu_flip_accounting.c src/recomp_manual.c -Wl,--gc-sections -lm
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include "recomp_types.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_ecx;
static const uint32_t block = 0x10000;
static uint8_t irql;
static unsigned retired, own_retire, other_retire;
static int trap_locked, interleave_at_trap;

void sub_001D9BB0(void);
void sub_001DA120(void);
uint8_t xbox_CurrentIrql(void) { return irql; }
int xbox_gil_suspend(void) { return 1; }
void xbox_gil_resume(int depth) { assert(depth == 1); }
uint8_t xbox_KfRaiseIrql(uint8_t next)
{
    uint8_t old = irql;
    /* Another handler completes while the caller releases the guest lock. */
    MEM32(block + 0x1BC) += other_retire;
    irql = next;
    return old;
}
void xbox_KfLowerIrql(uint8_t old) { irql = old; }
void xbox_Nv2aVblankTaken(void) { assert(irql == 2); }
void nv2a_pb_flip_retired(unsigned n) { retired += n; }
void nv2a_pb_trap_lock(void)
{
    assert(irql == 2);
    trap_locked = 1;
    if (interleave_at_trap)
        MEM32(block + 0x1BC) += other_retire;
}
void nv2a_pb_trap_unlock(void) { trap_locked = 0; }
void nv2a_pb_trap_taken(void) { assert(trap_locked); }
void sub_001D9BB0_gen(void)
{
    assert(irql == 2);
    MEM32(block + 0x1BC) += own_retire;
}
void sub_001DA120_gen(void)
{
    assert(irql == 2 && trap_locked);
    MEM32(block + 0x1BC) += own_retire;
}

int main(void)
{
    void *memory = mmap(NULL, UINT64_C(0x100000000), PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    assert(memory != MAP_FAILED);
    g_xbox_mem_offset = (ptrdiff_t)memory;
    for (unsigned handler = 0; handler < 2; handler++) {
        for (unsigned count = 0; count < 3; count++) {
            for (unsigned nested = 0; nested < 2; nested++) {
                g_ecx = block;
                irql = nested ? 2 : 0;
                own_retire = count;
                other_retire = 1;
                retired = 0;
                interleave_at_trap = handler;
                MEM32(block + 0x1BC) = UINT32_MAX; /* also exercise wrap */
                if (handler) sub_001DA120();
                else sub_001D9BB0();
                assert(retired == own_retire);
                assert(irql == (nested ? 2 : 0));
                assert(!trap_locked);
            }
        }
    }
    munmap(memory, UINT64_C(0x100000000));
    puts("GPU flip accounting: passed");
    return 0;
}
