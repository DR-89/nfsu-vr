/* Execute the retail XPP routine, rather than duplicating its port formula. */
#include <assert.h>
#include <stdio.h>
#include "recomp_types.h"
ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
RECOMP_TLS uint32_t g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top;
volatile int g_gil_contended;
void recomp_preempt(void) { assert(0); }
uint32_t xbox_mmio_read(uint32_t address, unsigned size) { (void)address; (void)size; assert(0); return 0; }
void xbox_mmio_write(uint32_t address, uint32_t value, unsigned size) { (void)address; (void)value; (void)size; assert(0); }
static unsigned char ram[0x200000];
void sub_001EF01B(void);
int main(void)
{
    g_xbox_mem_offset = (ptrdiff_t)ram;
    const unsigned expected[4] = {2, 3, 0, 1};
    for (unsigned port = 0; port < 4; port++) {
        g_esp = 0x10000;
        g_ecx = 0x20000;
        MEM32(g_esp + 4) = 0x30000;
        MEM32(g_esp + 8) = 1;
        MEM32(0x30004) = 0x40000;
        MEM8(0x40000) = 1;
        MEM8(0x40004) = port + 1; /* native root port is one-based */
        sub_001EF01B();
        assert(MEM32(0x20014) == expected[port]);
        assert(g_esp == 0x1000C);
    }
    puts("retail XPP root ports 1,2,3,4 -> Xbox slots 2,3,0,1: OK");
}
