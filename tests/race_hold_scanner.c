/* Retail scanner ABI and semantic event gating without a running guest. */
#include <assert.h>
#include "../src/recomp_manual.c"
ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
RECOMP_TLS uint32_t g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp;
static unsigned char ram[0x300000];
static unsigned expected;
static const uint32_t raw = 0x20000, binding = 0x21000;
static void scanner(void) {
    assert(MEM8(raw + 3) == expected);
    g_eax = 0x176;
    g_esp += 4;
}
void sub_00131050_gen(void) { scanner(); }
void sub_00130A70_gen(void) { scanner(); }
int main(void) {
    g_xbox_mem_offset = (ptrdiff_t)ram;
    race_touch_enable_hold(1);
    race_touch_set_hold(1);
    for (unsigned kind = 0; kind < 3; kind++) {
        unsigned event = kind == 0 ? 0x76 : kind == 1 ? 0xAB : 0xAC;
        MEM32(binding + 8) = event;
        MEM8(binding + 0x12) = 24; /* exercise a sample crossing beyond raw buttons */
        MEM8(raw + 3) = 0;
        g_esp = 0x10000;
        MEM32(g_esp + 4) = 0x22000; /* independent event state, never the raw sample */
        MEM32(g_esp + 8) = raw;
        MEM32(g_esp + 16) = binding;
        expected = kind == 0 ? 255 : kind == 1 ? 0 : 1; /* Start must stay untouched. */
        if (!kind) sub_00131050(); else sub_00130A70();
        assert(MEM8(raw + 3) == 0); /* never leak to another action */
        assert(g_esp == 0x10004 && g_eax == 0x176);
    }
    /* A physical Start press must still pass through unchanged. */
    MEM32(binding + 8) = 0xAB;
    MEM8(raw + 3) = 0x40;
    expected = 0x40; g_esp = 0x10000;
    sub_00130A70();
    assert(MEM8(raw + 3) == 0x40);
    MEM8(raw + 3) = 0;
    MEM32(binding + 8) = 0x99; /* handbrake/nitro/etc are untouched */
    g_esp = 0x10000; expected = 0; sub_00131050();
    race_touch_enable_hold(0);
    MEM32(binding + 8) = 0x76;
    g_esp = 0x10000; sub_00131050();
    assert(!race_touch_hold_active());
    puts("Native lookback/skip scanners, ABI, gating and raw sample restoration: OK");
}
