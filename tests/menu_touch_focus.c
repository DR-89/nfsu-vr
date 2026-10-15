/* Exercise actual draw/hit/input hooks with synthetic cached quads and a
 * focus adapter. See docs/native-menu-touch.md. */
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "recomp_types.h"
#include "menu_touch.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
RECOMP_TLS uint32_t g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top;
static unsigned char ram[0x300000];
static const uint32_t feng = 0x12000, package = 0x18000;
static const uint32_t yes = 0x20000, no = 0x21000;
static uint32_t expected;
static uint32_t expected_message = 0x406415E3u;
static int presses, focus_changes, steal_focus, controller_selects;
static int palette_updates, palette_previews;
static unsigned draw_mask = 3;
static int keyboard_layout, stale_mouse_edge;
void sub_000D8B10(void);
void sub_000D8730(void);
void sub_000DB860(void);
int xbox_video_widescreen(void) { return 1; }

void nfsu_touch_hit(void);
void sub_000DDB60(void) { assert(!MEM8(feng + 2)); g_eax = 0; g_esp += 16; }
void sub_000D9D00(void);
void sub_000F8B80(void);
void sub_000DC090(void);
void sub_000F8B80_gen(void) { g_esp += 4; }
void sub_000D9D00_gen(void)
{
    uint32_t entry = g_esp;
    g_ecx = MEM32(g_esp + 4) == yes ? 0x24000 : 0x25000;
    g_esp -= 4;
    sub_000F8B80();
    g_esp = entry + 8;
}
void sub_000DC090_gen(void)
{
    uint32_t entry = g_esp;
    uint32_t pair[2] = { yes, no };
    for (unsigned i = 0; i < 2; i++) {
        if (!(draw_mask & (1u << i))) continue;
        g_esp = entry - 8;
        MEM32(g_esp + 4) = pair[i];
        sub_000D9D00();
    }
    g_esp = entry + 4;
}
static void draw_bounds(void)
{
    for (unsigned i = 0; i < 2; i++) {
        uint32_t cache = 0x24000 + i * 0x1000, node = cache + 0x100;
        MEM32(cache + 0x18) = node;
        MEM32(node) = cache + 0x18;
        for (unsigned j = 0; j < 4; j++) {
            float x = 120.0f + i * 200.0f + (j & 1) * 200.0f;
            float y = (j / 2) * 480.0f;
            if (keyboard_layout) {
                x = (i ? 400.0f : 430.0f) + (j & 1) * 30.0f;
                y = (i ? 275.0f : 240.0f) + (j / 2) * 25.0f;
            }
            memcpy((void *)XBOX_PTR(node + 0x20 + j * 16), &x, 4);
            memcpy((void *)XBOX_PTR(node + 0x24 + j * 16), &y, 4);
        }
    }
    g_esp = 0x10000;
    sub_000DC090();
}
void sub_000C74C0(void)
{
    assert(g_ecx == 0x28000);
    assert(MEM32(g_ecx + 0xB08) == 0 && MEM32(g_ecx + 0xB0C) == 3);
    palette_updates++;
    g_esp += 4;
}
void sub_000C7B40(void) { assert(g_ecx == 0x28000); g_esp += 4; }
void sub_000C7A80(void)
{
    assert(g_ecx == 0x28000 && MEM32(g_esp + 4) == 100);
    palette_previews++;
    g_esp += 8;
}
void sub_000DE540(void)
{
    assert(g_ecx == 0x28000);
    assert(MEM32(g_esp + 4) == expected_message);
    assert(MEM32(g_esp + 8) == expected);
    assert(MEM32(g_esp + 12) == 1 && MEM32(g_esp + 16) == package);
    controller_selects++; presses++;
    g_eax = 0xBAD; g_ecx = 0xDEAD; g_edx = 0xBEEF;
    g_esp += 20;
}
void sub_000D7A00(void) { g_esp += 4; }
void sub_000D6D30(void)
{
    assert(g_ecx == feng);
    assert(MEM32(g_esp + 4) == expected_message);
    assert(MEM32(g_esp + 8) == expected);
    assert(MEM32(g_esp + 12) == package);
    assert(MEM32(g_esp + 16) == (uint32_t)-2 && MEM32(g_esp + 20) == 1);
    if (expected_message == 0x406415E3u) assert(MEM32(package + 0x80) == expected);
    controller_selects++; presses++;
    g_eax = 0xBAD; g_ecx = 0xDEAD; g_edx = 0xBEEF;
    g_esp += 24;
}

void sub_000E53C0(void)
{
    assert(g_ecx == package && MEM32(g_esp + 4) == feng);
    assert(MEM32(g_esp + 12) == 1);
    MEM32(package + 0x80) = MEM32(g_esp + 8);
    focus_changes++;
    g_eax = 0xDEAD; g_ecx = 0xBEEF; g_edx = 0xBAD;
    g_esp += 16;
}
void sub_000D8730_gen(void)
{
    assert(g_ecx == feng && MEM32(g_esp + 4) == package);
    assert(g_eax == 0x1234 && g_edx == 0x5678);
    assert(g_esi == 0x1111 && g_fp_top == 2 && g_fp_stack[2] == 42);
    if (!MEM32(package + 0x88)) { g_esp += 8; return; }
    if ((MEM16(feng + 0x1A) & 1) && !(MEM16(feng + 0x18) & 1)) {
        assert(MEM32(package + 0x80) == expected);
        presses++;
    }
    /* Retail mouse dispatch returns before scanning when focus is null. */
    if (!MEM32(package + 0x80)) { g_esp += 8; return; }
    uint32_t entry = g_esp;
    uint32_t pair[2] = { MEM32(0x22000), MEM32(0x22004) };
    for (unsigned i = 0; i < 2; i++) {
        if (MEM32(pair[i] + 0x1C) & 0x04000000u) continue;
        g_esp = entry - 16;
        MEM32(g_esp + 12) = pair[i];
        nfsu_touch_hit();
        assert(g_esp == entry);
        if (g_eax & 0xFF) break;
    }
    g_esp = entry + 8;
}
void sub_000DB860_gen(void) { g_esp += 8; }
void sub_000D8B10_gen(void)
{
    uint32_t entry = g_esp;
    g_esp -= 8;
    MEM32(g_esp) = 0xD8B5E;
    MEM32(g_esp + 4) = 0x11000;
    sub_000DB860();
    assert(g_esp == entry);
    MEM16(feng + 0x18) = MEM16(feng + 0x1A);
    MEM16(feng + 0x1A) = MEM16(0x11006);
    if (stale_mouse_edge) MEM16(feng + 0x18) = MEM16(feng + 0x1A);
    /* Simulate pad polling/dialog code changing focus after hover. */
    if (steal_focus) MEM32(package + 0x80) = expected == yes ? no : yes;
    g_esp -= 8;
    MEM32(g_esp) = 0x12345;
    MEM32(g_esp + 4) = package;
    g_ecx = feng; g_eax = 0x1234; g_edx = 0x5678;
    g_esi = 0x1111; g_fp_top = 2; g_fp_stack[2] = 42;
    sub_000D8730();
    assert(g_esp == entry);
    g_esp += 8;
}
static void update(void)
{
    g_esp = 0x10000;
    g_ecx = feng;
    MEM32(g_esp) = 0x12345;
    sub_000D8B10();
    assert(g_esp == 0x10008);
    assert(MEM8(feng + 2) == 0);
}
static void tap_at(float x, float y, uint32_t target)
{
    expected = target;
    menu_touch_event(MENU_TOUCH_DOWN, x, y, 1280, 720);
    menu_touch_event(MENU_TOUCH_UP, x, y, 1280, 720);
    update(); /* hover */
    steal_focus = 1;
    update(); /* press must resolve position again */
    steal_focus = 0;
    update(); /* release */
    update(); /* idle */
}
static void tap(float x, uint32_t target) { tap_at(x, .6f, target); }
int main(void)
{
    g_xbox_mem_offset = (ptrdiff_t)ram;
    MEM32(0x283434) = 3;
    float scale = 1;
    memcpy((void *)XBOX_PTR(0x295B00), &scale, 4);
    MEM32(package + 0x1C) = 0xFF;
    MEM32(package + 0x84) = 0x22000;
    MEM32(package + 0x88) = 2;
    MEM32(0x22000) = yes;
    MEM32(0x22004) = no;
    MEM32(yes + 0x10) = 0xB8A7C6CCu;
    MEM32(no + 0x10) = 0xB8A7C6CDu;
    MEM32(package + 0x80) = no;
    MEM32(yes + 0x3C) = MEM32(no + 0x3C) = 0x26000;
    MEM32(0x2600C) = 0x0C407210u;
    draw_bounds();
    tap(.3f, yes);
    tap(.7f, no);
    tap(.3f, yes);
    assert(presses == 3 && focus_changes == 3);
    /* Misses and disabled buttons cannot select the previous focus. */
    tap(.99f, yes);
    MEM32(no + 0x1C) = 0x04000000u;
    tap(.7f, no);
    assert(presses == 3 && focus_changes == 3);
    MEM32(no + 0x1C) = 0;
    keyboard_layout = 1;
    MEM32(yes + 0x10) = 0x11111111; MEM32(no + 0x10) = 0x22222222;
    draw_bounds();
    /* P and L lie on different rows. Hit the actual rendered centres,
     * independent of the stale layout descriptors (never consulted). */
    tap_at(445.0f / 640, 252.5f / 480, yes);
    tap_at(415.0f / 640, 287.5f / 480, no);
    assert(presses == 5 && focus_changes == 5);
    uint32_t group = 0x23000;
    MEM32(group + 0x3C) = 0x26000;
    MEM32(group + 0x18) = 5;
    MEM32(group + 0x60) = yes;
    MEM32(yes + 4) = 0;
    MEM32(0x22000) = group;
    tap_at(445.0f / 640, 252.5f / 480, group);
    assert(presses == 6);
    draw_mask = 1;
    draw_bounds(); /* hidden/not-drawn L must not reuse the previous frame */
    tap_at(415.0f / 640, 287.5f / 480, no);
    assert(presses == 6);
    /* Profile lists can start without FEng focus. A miss must leave it null,
     * while the visible row must bootstrap focus and activate normally. */
    MEM32(package + 0x80) = 0;
    expected = no;
    menu_touch_event(MENU_TOUCH_DOWN, .99f, .99f, 1280, 720);
    menu_touch_event(MENU_TOUCH_UP, .99f, .99f, 1280, 720);
    for (unsigned i = 0; i < 4; i++) update();
    assert(MEM32(package + 0x80) == 0 && presses == 6);
    tap_at(445.0f / 640, 252.5f / 480, group);
    assert(presses == 7 && controller_selects == 0);
    /* Xbox list rows with no mouse scripts still notify their real screen
     * controller once; neither a miss nor a disabled row may do so. */
    MEM32(group + 0x3C) = 0;
    tap_at(445.0f / 640, 252.5f / 480, group);
    assert(presses == 8 && controller_selects == 1);
    tap_at(.99f, .99f, group);
    assert(presses == 8 && controller_selects == 1);
    /* The profile controller ignores MouseClick even with object scripts.
     * Resolve the tapped visual row against the visible data window. */
    MEM32(group + 0x3C) = 0x26000;
    uint32_t registry = 0x27000, screen = 0x28000;
    uint32_t row = 0x29000, cell = 0x29100, data = 0x29200;
    MEM32(0x2BCB9C) = registry;
    MEM32(registry) = 0x2BCB9C;
    MEM32(registry - 4 + 0x18) = package;
    MEM32(registry - 4 + 0x14) = screen;
    MEM32(screen) = 0x1FC384;
    MEM32(screen + 0x3C) = 0xBD7E0;
    MEM32(screen + 0x88) = row;
    MEM32(screen + 0x9C) = data;
    MEM32(row) = screen + 0x88;
    MEM32(row + 8) = cell;
    MEM32(cell) = row + 8;
    MEM32(cell + 8) = group;
    MEM32(data) = screen + 0x90;
    tap_at(445.0f / 640, 252.5f / 480, group);
    assert(presses == 9 && controller_selects == 2);
    assert(MEM32(screen + 0x98) == data && MEM32(screen + 0xA0) == row);
    /* Statistics has no selectable rows. Its base-screen arrows still
     * dispatch one directional action per tap, independent of pad focus. */
    MEM32(package + 0x88) = 0;
    MEM32(screen + 0x24) = yes;
    MEM32(screen + 0x28) = no;
    MEM32(yes + 0x10) = 0xAA68BFCEu;
    MEM32(no + 0x10) = 0xAA68BFD4u;
    keyboard_layout = 0;
    draw_mask = 3;
    draw_bounds();
    expected_message = 0x9120409Eu;
    tap(.3f, yes);
    expected_message = 0xB5971BF1u;
    tap(.7f, no);
    assert(presses == 11 && controller_selects == 4);
    tap_at(.99f, .99f, no);
    assert(presses == 11);
    draw_mask = 1;
    draw_bounds();
    tap(.7f, no); /* hidden arrow has no current rendered geometry */
    assert(presses == 11);
    /* Only an empty-space drag in the active garage can drive the camera. */
    MEM32(0x1FC384 + 4) = 0xEEB80;
    MEM32(screen + 0x48) = 0x1FF870;
    MEM32(registry) = 0x2A000;
    MEM32(0x2A000) = 0x2BCB9C;
    MEM32(0x2A000 - 4 + 0x14) = 0x2B000;
    MEM32(0x2B000) = 0x1FB0CC;
    MEM32(0x2B05C) = 0x2C000;
    MEM32(0x2C000) = 0x1F8E90;
    float pitch = 100, initial_yaw = 115;
    memcpy((void *)XBOX_PTR(0x2C040), &pitch, 4);
    memcpy((void *)XBOX_PTR(0x2C044), &initial_yaw, 4);
    menu_touch_event(MENU_TOUCH_DOWN, .05f, .5f, 1280, 720);
    update(); update();
    menu_touch_event(MENU_TOUCH_MOVE, .07f, .5f, 1280, 720);
    update();
    float yaw;
    memcpy(&yaw, (void *)XBOX_PTR(0x2C044), 4);
    assert(yaw > 118.59f && yaw < 118.61f);
    assert(MEM32(0x2C184) == 0 && MEM32(0x2C188) == 0);
    update(); /* stationary finger has no continued rotation */
    float stationary;
    memcpy(&stationary, (void *)XBOX_PTR(0x2C044), 4);
    assert(stationary == yaw);
    menu_touch_event(MENU_TOUCH_MOVE, .06f, .5f, 1280, 720); update();
    menu_touch_event(MENU_TOUCH_MOVE, .05f, .5f, 1280, 720); update();
    memcpy(&stationary, (void *)XBOX_PTR(0x2C044), 4);
    assert(fabsf(stationary - initial_yaw) < .001f);
    yaw = stationary;
    MEM32(0x1FC384 + 4) = 0xD3720;
    menu_touch_event(MENU_TOUCH_MOVE, .09f, .5f, 1280, 720);
    update();
    float after;
    memcpy(&after, (void *)XBOX_PTR(0x2C044), 4);
    assert(after == yaw);
    menu_touch_cancel(); update();
    /* Customize's visible arrows live in the shared browser backdrop,
     * unlike its hidden mouse_over markers. Category icons open the node. */
    MEM32(0x1FC384 + 4) = 0xEEE00; /* nested garage category screen */
    MEM32(screen + 0x24) = MEM32(screen + 0x28) = 0;
    MEM32(yes + 0x10) = 0x543398D3;
    MEM32(no + 0x10) = 0xDB157AC6;
    draw_mask = 3; draw_bounds();
    int before_actions = controller_selects;
    stale_mouse_edge = 1; /* gesture actions do not depend on retail mouse edges */
    expected_message = 0x9120409E; tap(.3f, yes);
    expected_message = 0xB5971BF1; tap(.7f, no);
    assert(controller_selects == before_actions + 2);
    MEM32(yes + 0x10) = 0x11111111;
    MEM32(screen + 0x54) = 0x2D000;
    MEM32(0x2D00C) = yes;
    expected_message = 0x0C407210; tap(.3f, yes);
    assert(controller_selects == before_actions + 3);
    MEM32(0x2D00C) = 0; /* separate text label opens the same category */
    MEM32(yes + 0x1C) = 0x04000000; /* noninteractive label is still drawn */
    MEM32(yes + 0x10) = 0x4AB1E821;
    tap(.3f, yes);
    assert(controller_selects == before_actions + 4);
    expected = yes;
    menu_touch_event(MENU_TOUCH_DOWN, .3f, .6f, 1280, 720);
    update(); update();
    for (unsigned i = 0; i < 5; i++) update();
    assert(controller_selects == before_actions + 4); /* wait for tap vs swipe */
    menu_touch_event(MENU_TOUCH_UP, .3f, .6f, 1280, 720);
    update(); update();
    assert(controller_selects == before_actions + 5);
    expected_message = 0xB5971BF1;
    menu_touch_event(MENU_TOUCH_DOWN, .3f, .6f, 1280, 720);
    update(); update();
    menu_touch_event(MENU_TOUCH_MOVE, .2f, .6f, 1280, 720); update();
    menu_touch_event(MENU_TOUCH_UP, .2f, .6f, 1280, 720); update(); update();
    assert(controller_selects == before_actions + 6); /* swipe left -> next */
    expected_message = 0x9120409E;
    menu_touch_event(MENU_TOUCH_DOWN, .3f, .6f, 1280, 720);
    update(); update();
    menu_touch_event(MENU_TOUCH_MOVE, .4f, .6f, 1280, 720); update();
    menu_touch_event(MENU_TOUCH_UP, .4f, .6f, 1280, 720); update(); update();
    assert(controller_selects == before_actions + 7); /* swipe right -> previous */
    menu_touch_event(MENU_TOUCH_DOWN, .3f, .6f, 1280, 720);
    update(); update();
    menu_touch_event(MENU_TOUCH_MOVE, .35f, .8f, 1280, 720); update();
    menu_touch_event(MENU_TOUCH_UP, .35f, .8f, 1280, 720); update(); update();
    assert(controller_selects == before_actions + 7); /* vertical drag is no tap */
    /* Paint's own controller owns rotation and its native swatch records.
     * A click selects a rendered, populated swatch and previews without accept. */
    MEM32(screen) = 0x1FDD74;
    uint32_t swatch = screen + 0x58 + 3 * 0x2C;
    memset((void *)XBOX_PTR(screen + 0x58), 0, 60 * 0x2C);
    MEM32(swatch) = 0x1FDD70;
    MEM32(swatch + 0xC) = yes;
    MEM32(swatch + 0x10) = 0x2E000;
    MEM32(swatch + 0x24) = 0;
    MEM32(swatch + 0x28) = 3;
    tap(.3f, yes);
    assert(palette_updates == 1 && palette_previews == 1);
    assert(MEM8(screen + 0xAFC) == 1);
    MEM32(swatch + 0x10) = 0;
    tap(.3f, yes); /* empty cell cannot select the old color */
    assert(palette_updates == 1 && palette_previews == 1);
    MEM32(swatch + 0x10) = 0x2E000;
    menu_touch_event(MENU_TOUCH_DOWN, .3f, .6f, 1280, 720);
    update(); update();
    menu_touch_event(MENU_TOUCH_MOVE, .4f, .6f, 1280, 720); update();
    menu_touch_event(MENU_TOUCH_UP, .4f, .6f, 1280, 720); update(); update();
    assert(palette_previews == 1); /* swatch drag is neither carousel nor camera */
    float paint_yaw;
    memcpy(&paint_yaw, (void *)XBOX_PTR(0x2C044), 4);
    menu_touch_event(MENU_TOUCH_DOWN, .05f, .5f, 1280, 720);
    update(); update();
    menu_touch_event(MENU_TOUCH_MOVE, .07f, .5f, 1280, 720); update();
    memcpy(&after, (void *)XBOX_PTR(0x2C044), 4);
    assert(fabsf(after - paint_yaw - 3.6f) < .001f);
    menu_touch_cancel(); update();
    /* Replace the retail Y Help footer with localized Back. Its label and
     * retained icon geometry both invoke B on the active screen, once. */
    MEM32(screen) = 0x1FC384;
    MEM32(0x1FC384 + 4) = 0xD3720;
    MEM32(yes + 0x18) = MEM32(no + 0x18) = 2;
    MEM32(yes + 0x10) = 0xA87927BE;
    MEM32(yes + 0x5C) = 0x169268;
    MEM32(no + 0x10) = 0x7492B591;
    MEM32(no + 0x5C) = 0x349EB9C2;
    draw_mask = 3; draw_bounds();
    assert(MEM32(yes + 0x5C) == 0x1335F0);
    expected_message = 0x911AB364;
    before_actions = controller_selects;
    tap(.3f, yes);
    tap(.7f, no);
    assert(controller_selects == before_actions + 2);
    expected = yes;
    menu_touch_event(MENU_TOUCH_DOWN, .3f, .6f, 1280, 720);
    update(); update();
    menu_touch_event(MENU_TOUCH_MOVE, .4f, .6f, 1280, 720); update();
    menu_touch_event(MENU_TOUCH_UP, .4f, .6f, 1280, 720); update(); update();
    assert(controller_selects == before_actions + 2); /* Back drag does not swipe */
    puts("rendered touch bounds, keyboard rows, stale focus and misses: OK");
}
