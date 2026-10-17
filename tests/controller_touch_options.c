/* cc -O1 -D__ANDROID__ -ffunction-sections -fdata-sections -Isrc
 * -Iandroid/app/build/generated/recomp tests/controller_touch_options.c
 * src/input_mode.c -Wl,--gc-sections -lm -o /tmp/nfsu-controller-options-test */
#include <assert.h>
#include "../src/recomp_manual.c"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
RECOMP_TLS uint32_t g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp;
static unsigned char ram[0x300000];
static const uint32_t screen = 0x20000, package = 0x21000;
static uint32_t hashes[8], objects[8];
static unsigned colors[8];

uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment)
{
    assert(size <= 0x800 && alignment == 16);
    return 0x100000;
}
void sub_000DD490(void)
{
    assert(MEM32(g_esp + 4) == package);
    const char *text = (const char *)XBOX_PTR(MEM32(g_esp + 12));
    assert(!strstr(text, "locked") && !strstr(text, "Unavailable"));
    g_esp += 4;
}
static unsigned part(uint32_t hash)
{
    for (unsigned i = 0; i < 8; i++) if (hashes[i] == hash) return i;
    assert(!"unknown option object");
    return 0;
}
void sub_000DC340(void)
{
    assert(MEM32(g_esp + 4) == package);
    g_eax = objects[part(MEM32(g_esp + 8))];
    g_esp += 4;
}
void sub_000DC220(void)
{
    assert(MEM32(g_esp + 4) == package);
    uint32_t object = objects[part(MEM32(g_esp + 8))];
    if (MEM32(g_esp + 12)) MEM32(object + 0x1C) &= ~0x04000000u;
    else MEM32(object + 0x1C) |= 0x04000000u;
    g_esp += 4;
}
void sub_000DCD30(void)
{
    uint32_t object = MEM32(g_esp + 4), color = MEM32(g_esp + 8);
    for (unsigned i = 0; i < 8; i++) {
        if (objects[i] != object) continue;
        colors[i] = color;
        uint32_t rgba = MEM32(object + 0x2C);
        for (unsigned channel = 0; channel < 4; channel++)
            MEM32(rgba + channel * 4) = (color >> (channel * 8)) & 255;
    }
    g_esp += 4;
}
static void check_rows(int sensor, int overlay)
{
    g_esp = 0x10000;
    controller_touch_text(screen);
    assert(g_esp == 0x10000);
    for (unsigned i = 0; i < 8; i++) {
        int enabled = i < 4 ? sensor : overlay;
        assert(!!(MEM32(objects[i] + 0x1C) & 0x04000000u) == !enabled);
        assert(colors[i] == (enabled ? 0xFFBBAA99u : 0xFF606060u));
    }
}
int main(void)
{
    g_xbox_mem_offset = (ptrdiff_t)ram;
    MEM32(screen + 0xC) = package;
    const char *const parts[] = {"OptionName", "OptionData", "LeftArrow", "RightArrow"};
    for (unsigned i = 0; i < 8; i++) {
        char name[32];
        snprintf(name, sizeof(name), "%s_%u", parts[i % 4], 5 + i / 4);
        hashes[i] = fe_object_hash(name);
        objects[i] = 0x30000 + i * 0x100;
        MEM32(objects[i] + 0x2C) = 0x40000 + i * 16;
        colors[i] = 0xFFBBAA99;
        for (unsigned c = 0; c < 4; c++)
            MEM32(0x40000 + i * 16 + c * 4) = (colors[i] >> (c * 8)) & 255;
    }
    input_mode_configure(INPUT_SENSOR_AUTO, 0);
    check_rows(0, 0); /* no pad: both rows unavailable */
    g_ecx = screen; g_esp = 0x10000; MEM32(g_esp + 4) = FE_PAD_RIGHT;
    controller_touch_input(1);
    assert(!input_mode_get().overlay_requested && g_esp == 0x10008);
    input_mode_set_controller_present(1);
    check_rows(1, 0); /* sensor Off: overlay unavailable */
    input_mode_configure(INPUT_SENSOR_ON, 0);
    check_rows(1, 1); /* pad + sensor On: restore original colors and editing */
    input_mode_set_controller_present(0);
    check_rows(0, 0); /* unplug while settings are open */
    g_ecx = screen; g_esp = 0x10000; MEM32(g_esp + 4) = 0x0C407210u;
    touch_layout_input();
    assert(g_esp == 0x10008 && race_touch_take_editor_request());
    assert(!race_touch_take_editor_request());
    g_esp = 0x10000; MEM32(g_esp + 4) = 0x911AB364u; /* Back cannot open editor. */
    touch_layout_input();
    assert(!race_touch_take_editor_request());
    puts("Controller native disabled rows, color restoration and input guards: OK");
}
