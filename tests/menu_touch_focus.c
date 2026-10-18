#include "input_mode.h"
/* Exercise actual draw/hit/input hooks with synthetic cached quads and a
 * focus adapter. See docs/native-menu-touch.md. */
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "recomp_types.h"
#include "menu_touch.h"
#include "feng_touch.h"

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
static int palette_updates, palette_previews, focus_flushes;
static unsigned draw_mask = 3;
static int keyboard_layout, stale_mouse_edge, carousel_layout;
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
static void add_quad(uint32_t object, float left, float top, float right, float bottom)
{
    uint32_t cache = 0x35000, node = 0x35100;
    MEM32(cache + 0x18) = node; MEM32(node) = cache + 0x18;
    for (unsigned j = 0; j < 4; j++) {
        float x = j & 1 ? right : left, y = j / 2 ? bottom : top;
        memcpy((void *)XBOX_PTR(node + 0x20 + j * 16), &x, 4);
        memcpy((void *)XBOX_PTR(node + 0x24 + j * 16), &y, 4);
    }
    uint32_t previous = feng_touch_draw_begin(object);
    feng_touch_draw_cache(cache);
    feng_touch_draw_end(previous);
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
            if (carousel_layout) {
                x = 280.0f + i * 80.0f + (j & 1) * 40.0f;
                y = 25.0f + (j / 2) * 40.0f;
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
void sub_000DF160(void)
{
    assert(g_ecx == 0x2E000 && MEM32(g_esp + 4) == 0x2E200);
    assert(MEM32(0x28048) == 0x2E200);
    g_esp += 8;
}
void sub_000B8310(void) { assert(g_ecx == 0x28000); g_esp += 4; }
void sub_000D1AD0(void) { assert(g_ecx == 0x28000); g_esp += 4; }
void sub_000DE540(void)
{
    assert(g_ecx == 0x28000);
    assert(MEM32(g_esp + 4) == expected_message);
    assert(MEM32(g_esp + 8) == expected);
    if (MEM32(g_ecx) == 0x1FC078 && expected_message == 0x0C407210) {
        /* B88C0 calls B8880 with the button object. B8880 reads its +10
         * hash before mapping EASY/MEDIUM/HARD to the race difficulty. */
        uint32_t object = MEM32(g_esp + 8);
        uint32_t hash = MEM32(object + 0x10);
        assert(hash == 0x14DD31 || hash == 0x6BAA2200 || hash == 0x16821E);
        MEM32(0x29C460) = hash == 0x16821E ? 2 : hash == 0x6BAA2200 ? 1 : 0;
    }
    assert(MEM32(g_esp + 12) == 1 && MEM32(g_esp + 16) == package);
    controller_selects++; presses++;
    g_eax = 0xBAD; g_ecx = 0xDEAD; g_edx = 0xBEEF;
    g_esp += 20;
}
uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment)
{
    (void)size; (void)alignment;
    assert(!"unexpected options text allocation");
    return 0;
}
void sub_000DD490(void) { assert(!"unexpected options text refresh"); }
void sub_000DC340(void) { assert(!"unexpected option object lookup"); }
void sub_000DC220(void) { assert(!"unexpected option state change"); }
void sub_000DCD30(void) { assert(!"unexpected option color change"); }
void sub_000D7A00(void) { focus_flushes++; g_esp += 4; }
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
    int paint_actions = controller_selects;
    tap(.3f, yes); /* drag cancelled the previous tap pair */
    assert(controller_selects == paint_actions);
    expected_message = 0x406415E3;
    tap(.3f, yes); /* second tap confirms through the screen controller */
    assert(controller_selects == paint_actions + 1);
    tap(.3f, yes); /* third tap starts a fresh pair */
    assert(controller_selects == paint_actions + 1);
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
    /* A visible native Back caption works without the obsolete Help icon,
     * including finger tolerance just outside its glyph bounds. */
    draw_mask = 1; draw_bounds();
    before_actions = controller_selects;
    tap(.3f, yes);
    tap(.18f, yes);
    assert(controller_selects == before_actions + 2);

    /* Numbered option arrows dispatch to their own row, even when another
     * row is focused. They need no mouse scripts or selectable arrow. */
    uint32_t option = 0x23000;
    MEM32(option + 0x10) = 0x1AEC7D0A; /* OptionName_2 */
    MEM32(0x22000) = option;
    MEM32(package + 0x88) = 1;
    MEM32(package + 0x80) = no;
    MEM32(yes + 0x18) = MEM32(no + 0x18) = 1;
    MEM32(yes + 0x10) = 0x2DD56A46; /* LeftArrow_2 */
    MEM32(no + 0x10) = 0x1E558BB9; /* RightArrow_2 */
    draw_mask = 3; draw_bounds();
    before_actions = controller_selects;
    expected_message = 0x9120409E; tap(.3f, option);
    expected_message = 0xB5971BF1; tap(.7f, option);
    assert(controller_selects == before_actions + 2);
    MEM32(option + 0x1C) = 0x04000000;
    tap(.7f, option);
    assert(controller_selects == before_actions + 2);
    MEM32(option + 0x1C) = 0;
    draw_mask = 1; draw_bounds(); tap(.7f, option);
    assert(controller_selects == before_actions + 2);

    /* Quick Race row taps only focus; rendered arrows change the value.
     * The native modal heading becomes the explicit Continue target. */
    MEM32(screen) = 0x1FB64C;
    MEM32(yes + 0x10) = 0x4C61C6CE; /* Option1Text */
    MEM32(no + 0x10) = 0x4C58EF23; /* Option1Data */
    MEM32(yes + 0x18) = MEM32(no + 0x18) = 2;
    MEM32(yes + 0x5C) = MEM32(no + 0x5C) = 0;
    MEM32(yes + 0x1C) = MEM32(no + 0x1C) = 0;
    MEM32(package + 0x88) = 0;
    draw_mask = 3; draw_bounds();
    before_actions = controller_selects;
    expected_message = 0xB5971BF1;
    int before_focus = focus_changes, before_flush = focus_flushes;
    tap(.3f, yes); tap(.7f, yes);
    tap(.5f, yes); /* gap between label and value selects the row */
    assert(focus_changes == before_focus + 3);
    assert(focus_flushes == before_flush + 3);
    assert(MEM32(package + 0x80) == yes);
    assert(controller_selects == before_actions);
    MEM32(yes + 0x1C) = 0x04000000;
    tap(.3f, yes);
    assert(controller_selects == before_actions);
    MEM32(yes + 0x1C) = 0;
    MEM32(no + 0x10) = 0xAA68BFD4; /* retail mouse_over_r, without DE440 pointers */
    MEM32(package + 0x80) = yes;
    draw_bounds(); tap(.7f, yes);
    assert(controller_selects == before_actions + 1);
    MEM32(no + 0x10) = 0xAA68BFCE; /* retail mouse_over_l */
    expected_message = 0x9120409E;
    draw_bounds(); tap(.7f, yes);
    assert(controller_selects == before_actions + 2);
    /* Actual blinking glyphs have identical hashes, unlike mouse_over_l/r.
     * Their identity and geometry survive a hidden blink frame. */
    uint32_t qr_label = 0x32000, qr_value = 0x33000;
    MEM32(qr_label + 0x10) = 0x4C61C6CE;
    MEM32(qr_value + 0x10) = 0x4C58EF23;
    MEM32(qr_label + 0x18) = MEM32(qr_value + 0x18) = 2;
    MEM32(yes + 0x10) = MEM32(no + 0x10) = 0x13E9F3A9;
    MEM32(package + 0x80) = qr_label;
    draw_mask = 3; draw_bounds();
    add_quad(qr_label, 120, 200, 280, 280);
    add_quad(qr_value, 300, 200, 340, 280);
    expected_message = 0x9120409E; tap(.3f, qr_label);
    expected_message = 0xB5971BF1; tap(.7f, qr_label);
    assert(controller_selects == before_actions + 4);
    MEM32(package + 0x80) = qr_label;
    draw_mask = 0; draw_bounds(); /* hidden phase: no current arrow quads */
    add_quad(qr_label, 120, 200, 280, 280);
    add_quad(qr_value, 300, 200, 340, 280);
    expected_message = 0x9120409E; tap(.3f, qr_label);
    expected_message = 0xB5971BF1; tap(.7f, qr_label);
    assert(controller_selects == before_actions + 6);
    MEM32(package + 0x80) = yes; /* changing row invalidates old blink geometry */
    draw_mask = 0; draw_bounds();
    tap(.7f, qr_label);
    assert(controller_selects == before_actions + 6);
    before_actions += 4;
    MEM32(yes + 0x10) = 0x123456;
    MEM32(yes + 0x5C) = 0xF5599FF0; /* MODE_OPTIONS_TITLE: actual retail heading */
    draw_mask = 1; draw_bounds();
    assert(MEM32(yes + 0x5C) == 0xDE02CF84); /* localized CONTINUE */
    expected_message = 0x406415E3;
    tap(.3f, yes);
    assert(controller_selects == before_actions + 3);
    tap(.7f, yes); /* empty modal body cannot start a race */
    assert(controller_selects == before_actions + 3);
    MEM32(yes + 0x5C) = 0;
    MEM32(yes + 0x18) = MEM32(no + 0x18) = 1;
    draw_mask = 3; draw_bounds();

    /* Underground's event scroller owns a separate cursor and ignores
     * FEng focus. Select must use the touched event, not the previous one. */
    MEM32(package + 0x88) = 0;
    MEM32(screen) = 0x1FC12C;
    uint32_t list = 0x2E000, event = 0x2E100;
    MEM32(screen + 0x3C) = list;
    MEM32(list) = 0x1FF870;
    MEM32(list + 0xC) = event;
    MEM32(list + 4) = event;
    MEM32(event) = list + 4;
    MEM32(event + 8) = 0x2E200;
    MEM32(event + 0xC) = yes;
    expected_message = 0x406415E3;
    before_actions = controller_selects;
    tap(.3f, yes);
    tap(.7f, no);
    assert(MEM32(screen + 0x48) == 0x2E200);
    assert(controller_selects == before_actions + 1);
    expected = yes;
    menu_touch_event(MENU_TOUCH_DOWN, .3f, .6f, 1280, 720); update(); update();
    MEM32(list + 4) = list + 4; /* event removed while finger is held */
    menu_touch_event(MENU_TOUCH_UP, .3f, .6f, 1280, 720); update(); update();
    assert(controller_selects == before_actions + 1);

    /* Controller-layout arrows address the layout field, not the arrow. */
    MEM32(screen) = 0x1FDBC8;
    MEM32(yes + 0x10) = 0x543398D3; /* Select_arrow_left */
    MEM32(no + 0x10) = 0xEBFB7CC5; /* native layout data field */
    draw_mask = 3; draw_bounds();
    expected_message = 0x9120409E;
    before_actions = controller_selects;
    tap(.3f, no);
    assert(controller_selects == before_actions + 1);
    MEM32(yes + 0x10) = 0xDB157AC6;
    draw_bounds();
    expected_message = 0xB5971BF1;
    tap(.3f, no);
    assert(controller_selects == before_actions + 2);
    /* Quick Race reuses the same camera and rendered-arrow policies as
     * Customize, with its own native screen controller and selected row. */
    MEM32(screen) = 0x1FB8F4;
    MEM32(yes + 0x10) = 0x543398D3;
    MEM32(no + 0x10) = 0xDB157AC6;
    draw_bounds();
    before_actions = controller_selects;
    expected_message = 0x9120409E; tap(.3f, yes);
    expected_message = 0xB5971BF1; tap(.7f, no);
    assert(controller_selects == before_actions + 2);
    expected = yes;
    expected_message = 0x9120409E;
    menu_touch_event(MENU_TOUCH_DOWN, .3f, .6f, 1280, 720); update(); update();
    menu_touch_event(MENU_TOUCH_MOVE, .4f, .6f, 1280, 720); update();
    menu_touch_event(MENU_TOUCH_UP, .4f, .6f, 1280, 720); update(); update();
    assert(controller_selects == before_actions + 3); /* arrow drag uses shared swipe */
    before_actions++;
    memcpy(&yaw, (void *)XBOX_PTR(0x2C044), 4);
    menu_touch_event(MENU_TOUCH_DOWN, .05f, .5f, 1280, 720); update(); update();
    menu_touch_event(MENU_TOUCH_MOVE, .07f, .5f, 1280, 720); update();
    memcpy(&after, (void *)XBOX_PTR(0x2C044), 4);
    assert(fabsf(after - yaw - 3.6f) < .001f);
    update();
    memcpy(&stationary, (void *)XBOX_PTR(0x2C044), 4);
    assert(stationary == after);
    menu_touch_cancel(); update();
    MEM32(screen + 0x58) = 0x2E400;
    MEM32(0x2E40C) = no;
    MEM32(no + 0x10) = 0x12345678; /* selected car's rendered name */
    draw_bounds();
    expected_message = 0x406415E3;
    tap(.7f, no);
    assert(controller_selects == before_actions + 3);
    /* Underground difficulty: a new choice only focuses; tapping the
     * selected choice starts the race. Decide before hover, even when native
     * code changes focus while the finger is held. */
    MEM32(package + 0x88) = 2;
    MEM32(0x22000) = yes; MEM32(0x22004) = no;
    MEM32(screen) = 0x1FC078;
    MEM32(screen + 0xC) = package;
    MEM32(0x1FC078 + 4) = 0xB8F60;
    MEM32(yes + 0x10) = 0x14DD31; /* EASY */
    MEM32(no + 0x10) = 0x16821E; /* HARD */
    MEM32(package + 0x80) = no;
    draw_bounds();
    before_actions = controller_selects;
    int before_flushes = focus_flushes;
    expected_message = 0x0C407210;
    tap(.3f, yes);
    assert(controller_selects == before_actions);
    assert(MEM32(package + 0x80) == yes && focus_flushes > before_flushes);
    MEM32(package + 0x80) = no; /* transient native focus must not lose the touch choice */
    tap(.3f, yes);
    assert(controller_selects == before_actions + 1);
    assert(MEM32(0x29C460) == 0); /* EASY */
    tap(.7f, no);
    assert(controller_selects == before_actions + 1);
    assert(MEM32(package + 0x80) == no);
    tap(.7f, no);
    assert(controller_selects == before_actions + 2);
    assert(MEM32(0x29C460) == 2); /* HARD */
    MEM32(yes + 0x10) = 0x6BAA2200; /* MEDIUM uses the same rule */
    draw_bounds();
    tap(.3f, yes);
    assert(controller_selects == before_actions + 2);
    tap(.3f, yes);
    assert(controller_selects == before_actions + 3);
    assert(MEM32(0x29C460) == 1); /* MEDIUM */
    /* Realistic separate label/reward quads: the amount and intervening
     * gap belong to the same difficulty, beyond the text glyphs. */
    const uint32_t difficulties[] = { 0x14DD31, 0x6BAA2200, 0x16821E };
    const uint32_t rewards[] = { 0x64DDFDFF, 0x22AD620E, 0x3434E0AC };
    for (unsigned d = 0; d < 3; d++) {
        MEM32(yes + 0x10) = difficulties[d];
        MEM32(no + 0x10) = difficulties[(d + 1) % 3];
        MEM32(0x2F010) = rewards[d];
        MEM32(0x2F110) = rewards[(d + 1) % 3];
        feng_touch_frame_begin();
        add_quad(yes, 100, 330, 220, 350);
        add_quad(no, 400, 330, 520, 350);
        add_quad(0x2F000, 140, 370, 180, 390);
        add_quad(0x2F100, 440, 370, 480, 390);
        MEM32(package + 0x80) = no;
        int actions = controller_selects;
        tap_at(160.0f / 640, 380.0f / 480, yes); /* amount selects */
        assert(controller_selects == actions && MEM32(package + 0x80) == yes);
        tap_at(160.0f / 640, 360.0f / 480, yes); /* gap confirms */
        assert(controller_selects == actions + 1 && MEM32(0x29C460) == d);
        tap_at(95.0f / 640, 380.0f / 480, yes); /* beside the digits */
        assert(controller_selects == actions + 2);
    }
    draw_bounds();
    before_actions = controller_selects - 3;
    /* A drag cannot select or start; leaving the screen cancels release. */
    expected = no;
    menu_touch_event(MENU_TOUCH_DOWN, .7f, .6f, 1280, 720); update(); update();
    menu_touch_event(MENU_TOUCH_UP, .8f, .6f, 1280, 720); update(); update();
    assert(controller_selects == before_actions + 3);
    menu_touch_event(MENU_TOUCH_DOWN, .7f, .6f, 1280, 720); update(); update();
    MEM32(screen) = 0x1FC12C;
    menu_touch_event(MENU_TOUCH_UP, .7f, .6f, 1280, 720); update(); update();
    assert(controller_selects == before_actions + 3);
    /* Both track browsers share gestures and never orbit a preview car. */
    for (unsigned career_map = 0; career_map < 2; career_map++) {
        /* SELECT LOCATION has a separate controller and no preview camera.
         * Its native browser lives at +48, selected node at +54. */
        MEM32(screen) = career_map ? 0x1FC12C : 0x1FB848;
        MEM32(0x1FC12C + 4) = 0xB95D0;
        MEM32(screen + 0x3C) = 0x2E600;
        MEM32(0x2E600) = 0x1FF870;
        MEM32(0x2E60C) = 0x2E400;
        MEM32(0x1FB848 + 4) = 0xB1880;
        MEM32(screen + 0x48) = 0x1FF870;
        MEM32(screen + 0x54) = 0x2E400;
        MEM32(0x2E40C) = no;
        MEM32(yes + 0x10) = 0x543398D3;
        MEM32(no + 0x10) = 0xDB157AC6;
        draw_bounds();
        before_actions = controller_selects;
        expected_message = 0x9120409E; tap(.3f, yes);
        expected_message = 0xB5971BF1; tap(.7f, no);
        assert(controller_selects == before_actions + 2);
        MEM32(no + 0x10) = 0x12345678; /* selected track thumbnail */
        draw_bounds();
        expected_message = 0x406415E3; tap(.7f, no);
        assert(controller_selects == before_actions + 3);
        /* The location caption also accepts Select, independently of thumbnail. */
        MEM32(yes + 0x10) = career_map ? 0x007CFAB7 : 0x03C8F634;
        draw_bounds();
        expected_message = 0x406415E3; tap(.3f, yes);
        assert(controller_selects == before_actions + 4);
        /* Swipe either way is release-only; a stationary gap tap is inert. */
        carousel_layout = 1; draw_bounds();
        expected = no;
        expected_message = 0xB5971BF1;
        menu_touch_event(MENU_TOUCH_DOWN, .7f, .08f, 1280, 720); update(); update();
        menu_touch_event(MENU_TOUCH_MOVE, .6f, .08f, 1280, 720); update();
        assert(controller_selects == before_actions + 4);
        menu_touch_event(MENU_TOUCH_UP, .6f, .08f, 1280, 720); update(); update();
        assert(controller_selects == before_actions + 5);
        expected_message = 0x9120409E;
        menu_touch_event(MENU_TOUCH_DOWN, .7f, .08f, 1280, 720); update(); update();
        menu_touch_event(MENU_TOUCH_UP, .8f, .08f, 1280, 720); update(); update();
        assert(controller_selects == before_actions + 6);
        menu_touch_event(MENU_TOUCH_DOWN, .7f, .08f, 1280, 720); update(); update();
        menu_touch_event(MENU_TOUCH_UP, .7f, .08f, 1280, 720); update(); update();
        assert(controller_selects == before_actions + 6);
        /* An empty map drag must not rotate a hidden garage camera. */
        memcpy(&yaw, (void *)XBOX_PTR(0x2C044), 4);
        menu_touch_event(MENU_TOUCH_DOWN, .05f, .5f, 1280, 720); update(); update();
        menu_touch_event(MENU_TOUCH_MOVE, .1f, .5f, 1280, 720); update();
        memcpy(&after, (void *)XBOX_PTR(0x2C044), 4);
        assert(after == yaw);
        menu_touch_cancel(); update();
        carousel_layout = 0; draw_bounds();
        /* Back also dispatches on this screen, even with no selectable rows. */
        MEM32(package + 0x88) = 0;
        MEM32(yes + 0x18) = 2;
        MEM32(yes + 0x5C) = 0x001335F0;
        expected_message = 0x911AB364; tap(.3f, yes);
        assert(controller_selects == before_actions + 7);
        MEM32(yes + 0x18) = MEM32(yes + 0x5C) = 0;
        MEM32(package + 0x88) = 2;
        /* Leaving this typed screen while a thumbnail is held cancels Select. */
        expected = no;
        menu_touch_event(MENU_TOUCH_DOWN, .7f, .5f, 1280, 720); update(); update();
        MEM32(screen) = 0x1FC384;
        menu_touch_event(MENU_TOUCH_UP, .7f, .5f, 1280, 720); update(); update();
        assert(controller_selects == before_actions + 7);
    }
    /* MainMenuCarCustomize has a controller outside the tuning range,
     * but constructs the horizontal browser at +48 (selected node +54). */
    MEM32(screen) = 0x1FC1AC;
    MEM32(0x1FC1AC + 4) = 0xB9D70;
    MEM32(screen + 0x48) = 0x1FF870;
    MEM32(screen + 0x54) = 0x2E400;
    MEM32(yes + 0x10) = 0x543398D3;
    MEM32(no + 0x10) = 0xDB157AC6;
    draw_bounds();
    before_actions = controller_selects;
    expected_message = 0x9120409E; tap(.3f, yes);
    expected_message = 0xB5971BF1; tap(.7f, no);
    assert(controller_selects == before_actions + 2);
    MEM32(no + 0x10) = 0x12345678; /* selected carousel object */
    draw_bounds();
    expected_message = 0x0C407210; tap(.7f, no);
    assert(controller_selects == before_actions + 3);
    expected = no;
    expected_message = 0xB5971BF1;
    menu_touch_event(MENU_TOUCH_DOWN, .7f, .5f, 1280, 720); update(); update();
    menu_touch_event(MENU_TOUCH_MOVE, .6f, .5f, 1280, 720); update();
    assert(controller_selects == before_actions + 3);
    menu_touch_event(MENU_TOUCH_UP, .6f, .5f, 1280, 720); update(); update();
    assert(controller_selects == before_actions + 4);
    expected_message = 0x9120409E;
    menu_touch_event(MENU_TOUCH_DOWN, .7f, .5f, 1280, 720); update(); update();
    menu_touch_event(MENU_TOUCH_UP, .8f, .5f, 1280, 720); update(); update();
    assert(controller_selects == before_actions + 5);
    memcpy(&yaw, (void *)XBOX_PTR(0x2C044), 4);
    menu_touch_event(MENU_TOUCH_DOWN, .05f, .5f, 1280, 720); update(); update();
    menu_touch_event(MENU_TOUCH_MOVE, .07f, .5f, 1280, 720); update();
    memcpy(&after, (void *)XBOX_PTR(0x2C044), 4);
    assert(fabsf(after - yaw - 3.6f) < .001f);
    update();
    memcpy(&stationary, (void *)XBOX_PTR(0x2C044), 4);
    assert(stationary == after);
    assert(controller_selects == before_actions + 5);
    menu_touch_cancel(); update();
    /* Gaps beside small carousel logos capture immediately, with no hold.
     * Include the top edge, above the logo and its former 16-pixel margin.
     * A tap in a gap does nothing; its swipe cannot orbit the camera. */
    for (unsigned browser = 0; browser < 4; browser++) {
        /* Both garages, Quick Race and the first Underground car selection
         * share swipe capture and camera boundaries despite native offsets. */
        MEM32(screen) = browser == 3 ? 0x1FBFCC :
            browser == 2 ? 0x1FB8F4 : browser ? 0x1FC384 : 0x1FC1AC;
        MEM32(screen + (browser == 2 ? 0x58 : 0x54)) = 0x2E400;
        MEM32(0x1FBFCC + 4) = 0xB7DD0; /* ChooseCareerCarScreen */
        MEM32(0x1FC384 + 4) = 0xEEB80; /* Underground garage controller */
        before_actions = controller_selects;
        MEM32(no + 0x10) = 0xDB157AC6;
        draw_bounds();
        expected_message = 0x9120409E; tap(.3f, yes);
        expected_message = 0xB5971BF1; tap(.7f, no);
        MEM32(no + 0x10) = 0x12345678;
        draw_bounds();
        expected_message = browser == 2 ? 0x406415E3 : 0x0C407210;
        tap(.7f, no);
        assert(controller_selects == before_actions + 3);
        carousel_layout = 1; draw_bounds();
        before_actions = controller_selects;
        tap(.1f, no);
        assert(controller_selects == before_actions);
        memcpy(&yaw, (void *)XBOX_PTR(0x2C044), 4);
        expected = no; expected_message = 0xB5971BF1;
        menu_touch_event(MENU_TOUCH_DOWN, .2f, .001f, 1280, 720); update(); update();
        menu_touch_event(MENU_TOUCH_MOVE, .1f, .001f, 1280, 720); update();
        memcpy(&after, (void *)XBOX_PTR(0x2C044), 4);
        assert(after == yaw);
        menu_touch_event(MENU_TOUCH_UP, .1f, .001f, 1280, 720); update(); update();
        assert(controller_selects == before_actions + 1);
        menu_touch_event(MENU_TOUCH_DOWN, .1f, .001f, 1280, 720);
        menu_touch_event(MENU_TOUCH_UP, .1f, .001f, 1280, 720);
        update(); update(); update(); update();
        assert(controller_selects == before_actions + 1);
        expected_message = 0x9120409E;
        menu_touch_event(MENU_TOUCH_DOWN, .1f, .001f, 1280, 720); update(); update();
        menu_touch_event(MENU_TOUCH_UP, .2f, .001f, 1280, 720); update(); update();
        assert(controller_selects == before_actions + 2);
        memcpy(&after, (void *)XBOX_PTR(0x2C044), 4);
        assert(after == yaw);
        /* The car area below the strip still rotates normally. */
        menu_touch_event(MENU_TOUCH_DOWN, .1f, .5f, 1280, 720); update(); update();
        menu_touch_event(MENU_TOUCH_MOVE, .12f, .5f, 1280, 720); update();
        memcpy(&after, (void *)XBOX_PTR(0x2C044), 4);
        assert(fabsf(after - yaw - 3.6f) < .001f);
        /* Crossing into the panel freezes orbit, including pitch, and does not
         * hand the ongoing gesture to the carousel. Re-entry has no delta jump. */
        float panel_pitch = MEMF(0x2C040), panel_yaw = MEMF(0x2C044);
        menu_touch_event(MENU_TOUCH_MOVE, .3f, .001f, 1280, 720); update();
        menu_touch_event(MENU_TOUCH_MOVE, .5f, .001f, 1280, 720); update();
        assert(MEMF(0x2C040) == panel_pitch && MEMF(0x2C044) == panel_yaw);
        assert(controller_selects == before_actions + 2);
        menu_touch_event(MENU_TOUCH_MOVE, .5f, .5f, 1280, 720); update();
        assert(MEMF(0x2C040) == panel_pitch && MEMF(0x2C044) == panel_yaw);
        menu_touch_event(MENU_TOUCH_MOVE, .52f, .5f, 1280, 720); update();
        assert(fabsf(MEMF(0x2C044) - panel_yaw - 3.6f) < .001f);
        menu_touch_cancel(); update();
        carousel_layout = 0; draw_bounds();
    }
    /* Vertical base arrows: native Up/Down, release only, hidden/disabled
     * geometry and dragged gestures cannot scroll. */
    MEM32(screen) = 0x1FC384;
    MEM32(screen + 0x24) = MEM32(screen + 0x28) = 0;
    MEM32(0x1FC384 + 4) = 0;
    MEM32(yes + 0x1C) = MEM32(no + 0x1C) = 0;
    MEM32(screen + 0x2C) = yes;
    MEM32(screen + 0x30) = no;
    MEM32(yes + 0x10) = 0xAA68BFD7;
    MEM32(no + 0x10) = 0xAA68BFC6;
    MEM32(package + 0x88) = 0;
    draw_mask = 3; draw_bounds();
    before_actions = controller_selects;
    expected_message = 0x72619778; expected = yes;
    menu_touch_event(MENU_TOUCH_DOWN, .3f, .5f, 1280, 720); update(); update(); update();
    assert(controller_selects == before_actions);
    menu_touch_event(MENU_TOUCH_UP, .3f, .5f, 1280, 720); update(); update();
    assert(controller_selects == before_actions + 1);
    expected_message = 0x911C0A4B; tap(.7f, no);
    assert(controller_selects == before_actions + 2);
    draw_mask = 1; draw_bounds(); tap(.7f, no);
    assert(controller_selects == before_actions + 2);
    draw_mask = 3; draw_bounds();
    MEM32(no + 0x1C) |= 0x04000000; tap(.7f, no);
    MEM32(no + 0x1C) &= ~0x04000000u;
    menu_touch_event(MENU_TOUCH_DOWN, .7f, .5f, 1280, 720); update(); update();
    menu_touch_event(MENU_TOUCH_MOVE, .7f, .7f, 1280, 720); update();
    menu_touch_event(MENU_TOUCH_UP, .7f, .7f, 1280, 720); update(); update();
    assert(controller_selects == before_actions + 2);
    /* Autopress is visible slot 3 on page 8; old Option cursor is 6. */
    MEM32(screen) = 0x1FEA8C;
    MEM32(screen + 0x2C) = MEM32(screen + 0x30) = 0;
    MEM32(screen + 0x40) = 8; MEM32(screen + 0x48) = 23;
    MEM32(screen + 0x3C) = 6;
    MEM32(yes + 0x10) = 0xF1E6EA3A; /* cover slot 3 */
    MEM32(no + 0x10) = 0xF1E6EA3D; /* cover slot 6 */
    draw_bounds(); before_actions = controller_selects;
    expected_message = 0x406415E3;
    tap(.3f, yes);
    assert(MEM32(screen + 0x3C) == 3);
    assert(controller_selects == before_actions + 1);
    tap(.7f, no);
    assert(MEM32(screen + 0x3C) == 6);
    assert(controller_selects == before_actions + 2);
    expected = yes;
    menu_touch_event(MENU_TOUCH_DOWN, .3f, .5f, 1280, 720); update(); update();
    MEM32(screen + 0x40) = 10; /* scrolled while held: do not open a new item */
    menu_touch_event(MENU_TOUCH_UP, .3f, .5f, 1280, 720); update(); update();
    assert(controller_selects == before_actions + 2);
    /* Magazine swipes own the gesture: scroll once on release, never open
     * the cover or change its cursor; the grid gap is swipe-only as well. */
    draw_mask = 0; draw_bounds();
    add_quad(yes, 300, 100, 400, 180);
    add_quad(no, 420, 100, 520, 180);
    MEM32(screen + 0x3C) = 6;
    before_actions = controller_selects;
    expected = yes; expected_message = 0x911C0A4B;
    menu_touch_event(MENU_TOUCH_DOWN, 350.0f/640, 160.0f/480, 1280, 720); update(); update();
    menu_touch_event(MENU_TOUCH_MOVE, 350.0f/640, 110.0f/480, 1280, 720); update();
    assert(controller_selects == before_actions);
    menu_touch_event(MENU_TOUCH_UP, 350.0f/640, 110.0f/480, 1280, 720); update(); update();
    assert(controller_selects == before_actions + 1 && MEM32(screen + 0x3C) == 6);
    tap_at(410.0f/640, 130.0f/480, no); /* empty space between columns */
    assert(controller_selects == before_actions + 1);
    expected = no; expected_message = 0x72619778;
    menu_touch_event(MENU_TOUCH_DOWN, 410.0f/640, 110.0f/480, 1280, 720); update(); update();
    menu_touch_event(MENU_TOUCH_UP, 410.0f/640, 165.0f/480, 1280, 720); update(); update();
    assert(controller_selects == before_actions + 2 && MEM32(screen + 0x3C) == 6);
    /* A horizontal drag cannot open a cover either. */
    expected = yes;
    menu_touch_event(MENU_TOUCH_DOWN, 350.0f/640, 130.0f/480, 1280, 720); update(); update();
    menu_touch_event(MENU_TOUCH_UP, 390.0f/640, 130.0f/480, 1280, 720); update(); update();
    assert(controller_selects == before_actions + 2);
    /* Pause stays in GameFlow 6. C0040 consumes numeric row identities
     * through MouseClick; using Select silently does nothing. */
    MEM32(0x283434) = 6;
    MEM32(screen) = 0x1FD734;
    MEM32(package + 0x88) = 0;
    MEM32(yes + 0x10) = 1; MEM32(no + 0x10) = 3;
    MEM32(yes + 0x18) = MEM32(no + 0x18) = 2;
    MEM32(yes + 0x5C) = MEM32(no + 0x5C) = 0;
    MEM32(yes + 0x1C) = MEM32(no + 0x1C) = 0;
    draw_mask = 0; draw_bounds();
    add_quad(yes, 120, 200, 280, 225);
    add_quad(no, 120, 300, 210, 325);
    expected_message = 0x0C407210;
    before_actions = controller_selects;
    tap_at(.3f, .44f, yes);
    assert(feng_touch_race_menu_active());
    tap_at(.42f, .64f, no); /* full row extends past shorter Quit glyphs */
    assert(controller_selects == before_actions + 2);
    tap_at(.7f, .44f, yes);
    assert(controller_selects == before_actions + 2);
    MEM32(no + 0x1C) = 0x04000000;
    tap_at(.3f, .64f, no);
    assert(controller_selects == before_actions + 2);
    /* Accept footer becomes localized Back, with B glyph and a single
     * broad target spanning the icon, caption and their gap. */
    MEM32(screen) = 0x1FBBC8; /* pause settings selector */
    update();
    MEM32(yes + 0x10) = 0x123456;
    MEM32(no + 0x10) = 0x123457;
    MEM32(yes + 0x5C) = 0x4F876B6F;
    MEM32(no + 0x5C) = 0xE03C840A;
    MEM32(no + 0x1C) = 0;
    draw_mask = 0; draw_bounds();
    add_quad(yes, 180, 410, 260, 430);
    add_quad(no, 130, 405, 160, 435);
    assert(MEM32(yes + 0x5C) == 0x1335F0);
    assert(MEM32(no + 0x5C) == 0xE03C840B);
    expected_message = 0x911AB364;
    before_actions = controller_selects;
    tap_at(170.0f / 640, 420.0f / 480, no); /* gap */
    tap_at(125.0f / 640, 420.0f / 480, no); /* outside glyph edge */
    assert(controller_selects == before_actions + 2);
    /* Parent selector remains active behind a child settings panel. Back
     * must target the child and use its F2D0D887 close event. */
    uint32_t parent_reg = 0x37000, parent_screen = 0x36000, parent_package = 0x38000;
    MEM32(0x2BCB9C) = parent_reg; MEM32(parent_reg) = registry;
    MEM32(parent_reg - 4 + 0x14) = parent_screen;
    MEM32(parent_reg - 4 + 0x18) = parent_package;
    MEM32(parent_screen) = 0x1FBBC8; MEM32(parent_package + 0x1C) = 1;
    MEM32(screen) = 0x1FBC9C;
    expected_message = 0xF2D0D887;
    before_actions = controller_selects;
    tap_at(210.0f / 640, 420.0f / 480, yes);
    assert(controller_selects == before_actions + 1);
    MEM32(0x2BCB9C) = registry;
    /* Back cache colors are opaque white, and the native icon is omitted. */
    add_quad(yes, 180, 410, 260, 430);
    for (unsigned vertex = 0; vertex < 4; vertex++)
        assert(MEM32(0x35100 + 0x90 + vertex * 4) == 0xFFFFFFFF);
    /* Controller/Vibration accepts a broad touch across label/value/gap. */
    MEM32(yes + 0x10) = 0x1AEC7D09; /* OptionName_1 */
    MEM32(no + 0x10) = 0x7654321;
    MEM32(yes + 0x5C) = MEM32(no + 0x5C) = 0;
    draw_mask = 0; draw_bounds();
    add_quad(yes, 80, 150, 200, 170);
    add_quad(no, 550, 150, 590, 170);
    expected_message = 0xB5971BF1;
    before_actions = controller_selects;
    tap_at(350.0f / 640, 160.0f / 480, yes);
    assert(controller_selects == before_actions + 1);
    /* Actual pause layout: row container has no rendered quad; only its
     * OptionText child is drawn. Dispatch must still target OptionName_2. */
    MEM32(package + 0x84) = 0x22000;
    MEM32(package + 0x88) = 2;
    MEM32(0x22000) = yes; MEM32(0x22004) = no;
    MEM32(yes + 0x10) = 0x1AEC7D0A; /* OptionName_2 */
    MEM32(no + 0x10) = 0x293A1ACEu /* OptionText_2 */;
    draw_mask = 0; draw_bounds();
    add_quad(no, 80, 200, 350, 220);
    expected_message = 0xB5971BF1;
    before_actions = controller_selects;
    tap_at(220.0f / 640, 210.0f / 480, yes);
    assert(controller_selects == before_actions + 1);
    MEM32(package + 0x88) = 0;
    MEM32(screen) = 0x1F9404; /* countdown screen must not hide race controls */
    update();
    assert(!feng_touch_race_menu_active());
    MEM32(screen) = 0x1FD734;
    update();
    assert(feng_touch_race_menu_active());
    MEM32(package + 0x1C) = 0; /* unpaused: no active FEng menu */
    update();
    assert(!feng_touch_race_menu_active());
    /* Dialog answers and profile rows accept taps beside their text. */
    stale_mouse_edge = 0;
    MEM32(screen + 0x48) = 0;
    MEM32(0x1FC384 + 4) = 0;
    MEM32(package + 0x84) = 0x22000;
    MEM32(registry - 4 + 0x14) = screen;
    MEM32(registry - 4 + 0x18) = package;
    MEM32(screen + 0x24) = MEM32(screen + 0x28) = 0;
    MEM32(screen + 0x2C) = MEM32(screen + 0x30) = 0;
    MEM32(yes + 0x5C) = MEM32(no + 0x5C) = 0;
    MEM32(0x283434) = 3;
    MEM32(package + 0x1C) = 1;
    MEM32(package + 0x88) = 2;
    MEM32(0x22000) = yes; MEM32(0x22004) = no;
    MEM32(0x2BCB9C) = registry;
    MEM32(registry) = 0x2BCB9C;
    MEM32(screen) = 0x1FC384;
    MEM32(screen + 0x3C) = 0;
    MEM32(yes + 0x18) = MEM32(no + 0x18) = 2;
    MEM32(yes + 0x1C) = MEM32(no + 0x1C) = 0;
    MEM32(yes + 0x3C) = MEM32(no + 0x3C) = MEM32(package + 0x50) = 0;
    MEM32(yes + 0x10) = 0xB8A7C6CC;
    MEM32(no + 0x10) = 0xB8A7C6CD;
    draw_mask = 0; draw_bounds();
    add_quad(yes, 100, 200, 200, 220);
    add_quad(no, 400, 200, 500, 220);
    expected_message = 0x406415E3;
    before_actions = controller_selects;
    tap_at(90.0f / 640, 210.0f / 480, yes);
    tap_at(510.0f / 640, 210.0f / 480, no);
    assert(controller_selects == before_actions + 2);
    /* Same expansion is used when a profile list starts with no focus. */
    MEM32(screen + 0x3C) = 0xBD7E0;
    MEM32(screen + 0x88) = row; MEM32(screen + 0x9C) = data;
    MEM32(row) = screen + 0x88; MEM32(row + 8) = cell;
    MEM32(cell) = row + 8; MEM32(cell + 8) = yes;
    MEM32(data) = screen + 0x90;
    MEM32(package + 0x80) = 0;
    tap_at(90.0f / 640, 210.0f / 480, yes);
    assert(controller_selects == before_actions + 3);
    assert(MEM32(screen + 0x98) == data && MEM32(screen + 0xA0) == row);
    /* Physical-controller mode restores touch captions and leaves native
     * drawing unchanged. Switch back only after the previous edits are gone. */
    MEM32(yes + 0x10) = 0xA87927BE;
    MEM32(yes + 0x5C) = 0x169268; /* native Help */
    feng_touch_frame_begin();
    uint32_t previous = feng_touch_draw_begin(yes);
    assert(MEM32(yes + 0x5C) == 0x1335F0);
    feng_touch_draw_end(previous);
    input_mode_set_controller_present(1);
    feng_touch_frame_begin();
    assert(MEM32(yes + 0x5C) == 0x169268);
    previous = feng_touch_draw_begin(yes);
    assert(MEM32(yes + 0x5C) == 0x169268);
    assert(!feng_touch_draw_cache(0x35000));
    feng_touch_draw_end(previous);
    input_mode_set_controller_present(0);
    puts("rendered touch bounds, keyboard rows, stale focus and misses: OK");
}
