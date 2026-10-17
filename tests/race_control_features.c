/* Native capability sampling, player ownership and stale touch release. */
#include <assert.h>
#include "../src/recomp_manual.c"
ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
RECOMP_TLS uint32_t g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp;
static unsigned char ram[0x300000];
static const uint32_t player = 0x20000;
static unsigned native_calls;
static int selected_transmission;
static void native_transmission_change(void)
{
    assert(g_ecx == 0x21000 && MEM32(g_esp + 4) == 0xB5971BF1u);
    MEM32(0x295B74) = selected_transmission;
    g_eax = 0x5678;
    g_esp += 8;
}
void sub_000C0F50_gen(void) { native_transmission_change(); }
void sub_000CBE90_gen(void) { native_transmission_change(); }
static void change_transmission(void (*handler)(void), int manual)
{
    selected_transmission = manual;
    g_ecx = 0x21000;
    g_esp = 0x10000;
    MEM32(g_esp + 4) = 0xB5971BF1u;
    handler();
    assert(g_esp == 0x10008 && g_eax == 0x5678);
}
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
    const uint32_t vehicle = 0x25000;
    /* Real race-entry crash: HUD constructor A33BC supplies zero secondary
     * flags while the player's vehicle slot still contains allocator poison. */
    MEM32(player + 4) = 0xAAAAAAAAu;
    sample(player, 0, 0, 0, 0); expect(0, 0);
    sample(player, 0, 1, 0, 1); expect(0, 0); /* Disabled drift NOS also skips it. */
    MEM32(player + 4) = vehicle;
    MEM32(vehicle + 0x3F0) = 5000; /* Installed total NOS duration. */
    sample(player, 0, 0, 0, 0); expect(0, 0); /* Auto, stock car. */
    sample(player, 0, 0, 0, 1); expect(0, 1); /* Auto with NOS. */
    sample(player, 0, 0, 1, 1); expect(1, 1); /* Manual with NOS. */
    sample(player, 1, 0, 0, 1); expect(1, 1); /* Drag requires shifting even with Auto selected. */
    MEM32(vehicle + 0x3F0) = 0;
    sample(player, 1, 0, 0, 1); expect(1, 0); /* Stock drag car still has the HUD flag. */
    MEM32(vehicle + 0x3F0) = INT32_MAX;
    sample(player, 1, 0, 0, 1); expect(1, 0); /* Native unavailable sentinel. */
    MEM32(player + 4) = 0;
    sample(player, 1, 0, 0, 1); expect(1, 0); /* Vehicle not ready. */
    MEM32(player + 4) = vehicle;
    MEM32(vehicle + 0x3F0) = 5000;
    MEM32(vehicle + 0x3F8) = 0; /* Remaining charge may be empty. */
    sample(player, 1, 0, 0, 1); expect(1, 1);
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
    /* No new HUD sample between a pause-menu change and resuming the race. */
    change_transmission(sub_000C0F50, 1); expect(1, 0);
    race_touch_set(RACE_SHIFT_UP | RACE_GAS);
    change_transmission(sub_000C0F50, 0); expect(0, 0);
    assert(race_touch_consume(1) == RACE_GAS);
    sample(player, 0, 0, 0, 1); expect(0, 1);
    change_transmission(sub_000C0F50, 1); expect(1, 1);
    change_transmission(sub_000CBE90, 0); expect(0, 1);
    change_transmission(sub_000CBE90, 1); expect(1, 1);
    sample(player, 1, 0, 0, 1); expect(1, 1);
    change_transmission(sub_000C0F50, 0); expect(1, 1);
    MEM8(0x273784) = 0;
    MEM32(0x265BC8) = 0x23000;
    change_transmission(sub_000C0F50, 0); expect(1, 1);
    assert(native_calls == 18);
    puts("Race button capabilities: auto/manual/drag/drift/NOS, player ownership and release: OK");
}
