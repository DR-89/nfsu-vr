/* Native capability sampling, player ownership and stale touch release. */
#include <assert.h>
#include "../src/recomp_manual.c"
ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
RECOMP_TLS uint32_t g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp;
static unsigned char ram[0x300000];
static const uint32_t player = 0x20000;
static unsigned native_calls;
void sub_000A0140_gen(void)
{
    assert(g_ecx == 0x21000 && MEM32(g_esp + 8) == 0x1234);
    native_calls++;
    g_eax = 0x5678;
    g_esp += 16; /* Native thiscall, three arguments. */
}
static void sample(unsigned owner, int drag, int drift, int manual, int nos)
{
    MEM8(0x273784) = drag;
    MEM8(0x273785) = drift;
    MEM32(0x295B74) = manual;
    g_ecx = 0x21000;
    g_esp = 0x10000;
    MEM32(g_esp + 4) = owner;
    MEM32(g_esp + 8) = 0x1234;
    MEM32(g_esp + 12) = nos ? 0x40000 : 0;
    sub_000A0140();
    assert(g_esp == 0x10010 && g_eax == 0x5678);
}
static void expect(int shifting, int nos)
{
    uint32_t mask = race_touch_button_mask();
    assert(!!(mask & RACE_SHIFT_UP) == shifting);
    assert(!!(mask & RACE_SHIFT_DOWN) == shifting);
    assert(!!(mask & RACE_NITRO) == nos);
    assert((mask & (RACE_GAS | RACE_BRAKE | RACE_MENU | RACE_CAMERA)) ==
            (RACE_GAS | RACE_BRAKE | RACE_MENU | RACE_CAMERA));
}
int main(void)
{
    g_xbox_mem_offset = (ptrdiff_t)ram;
    MEM32(0x265B84) = player;
    sample(player, 0, 0, 0, 0); expect(0, 0); /* Auto, stock car. */
    sample(player, 0, 0, 0, 1); expect(0, 1); /* Auto with NOS. */
    sample(player, 0, 0, 1, 1); expect(1, 1); /* Manual with NOS. */
    sample(player, 1, 0, 0, 1); expect(1, 1); /* Drag requires shifting even with Auto selected. */
    sample(player, 0, 1, 0, 1); expect(0, 0); /* Drift disables NOS despite installed hardware. */
    sample(player, 0, 1, 1, 1); expect(1, 0);
    sample(player + 0x100, 0, 0, 0, 1); expect(1, 0); /* Player two cannot replace our snapshot. */
    MEM32(0x265BC8) = 0x23000;
    sample(player, 0, 0, 0, 0); expect(1, 0); /* Native drag controller fallback. */
    MEM32(0x265BC8) = 0;
    MEM32(0x265BC4) = 0x24000;
    sample(player, 0, 0, 1, 1); expect(1, 0); /* Native drift controller fallback. */
    MEM32(0x265BC4) = 0;
    race_touch_enable(1);
    race_touch_configure_features(1, 1);
    race_touch_set(RACE_SHIFT_UP | RACE_NITRO | RACE_GAS);
    sample(player, 0, 0, 0, 0);
    assert(race_touch_consume(1) == RACE_GAS); /* Held AND pending removed immediately. */
    race_touch_set(RACE_SHIFT_DOWN | RACE_NITRO);
    assert(race_touch_consume(1) == 0); /* Hidden controls cannot activate. */
    assert(native_calls == 10);
    puts("Race button capabilities: auto/manual/drag/drift/NOS, player ownership and release: OK");
}
