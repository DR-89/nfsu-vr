/* Exercise the actual guest hooks with a synchronous draw stub. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_esp, g_ecx;
static unsigned char ram[0x300000];
static int wide = 1;
static float expected_x[4], expected_y[4], expected_projection;
static int draws;
static float expected_hud;

int xbox_video_widescreen(void) { return wide; }
void sub_000192C0(void);
void sub_00018E40(void);
void sub_00016460(void);
void sub_0014B510(void);
void sub_000F8B80(void);
void sub_000DC090(void);

static float read_float(uint32_t at)
{
    float value;
    memcpy(&value, (const void *)XBOX_PTR(at), 4);
    return value;
}

static void put_float(uint32_t at, float value)
{
    memcpy((void *)XBOX_PTR(at), &value, 4);
}

void sub_000192C0_gen(void)
{
    uint32_t quad = MEM32(g_esp + 4);
    for (int i = 0; i < 4; i++) {
        assert(fabsf(read_float(quad + 0x10 + i * 16) - expected_x[i]) < 0.001f);
        assert(read_float(quad + 0x14 + i * 16) == expected_y[i]);
    }
    draws++;
    g_esp += 4;
}

void sub_00018E40_gen(void)
{
    assert(read_float(g_ecx + 0x40) == expected_projection);
    g_esp += 4;
}

static int draw_hud;
static int draw_feng;
static int draw_menu;
void sub_000DC090_gen(void)
{
    if (draw_feng) sub_000F8B80();
    else sub_000192C0();
}
static float cached_expected_shift;
/* Reproduce the cached FEng path: queue node+0x10 -> screen-space quad. */
void sub_000F8B80_gen(void)
{
    uint32_t head = g_ecx + 0x18;
    uint32_t node = MEM32(head);
    uint32_t caller_sp = g_esp;
    while (node != head) {
        uint32_t quad = node + 0x10;
        for (int i = 0; i < 4; i++) {
            float x = read_float(quad + 0x10 + i * 16);
            int stretch = draw_menu && wide && getenv("RECOMP_HORPLUS") == NULL &&
                          MEM32(0x283434) == 6;
            expected_x[i] = stretch ? 320.0f + (x - 320.0f) * (4.0f / 3.0f) :
                                      x + cached_expected_shift;
            expected_y[i] = read_float(quad + 0x14 + i * 16);
        }
        g_esp = caller_sp - 8;
        MEM32(g_esp + 4) = quad;
        sub_000192C0();
        node = MEM32(node);
    }
    g_esp = caller_sp + 4;
    g_ecx = 0x7777; /* guest draw may clobber volatile registers */
}
void sub_0014B510_gen(void)
{
    assert(fabsf(read_float(g_ecx + 0x68) - expected_hud) < 0.001f);
    g_esp += 8; /* thiscall, ret 4 */
}
void sub_00016460_gen(void)
{
    if (draw_menu) sub_000DC090();
    else if (draw_hud) sub_0014B510();
    else if (draw_feng) sub_000F8B80();
    else sub_000192C0();
}

static void quad_case(float x0, float x1, float y0, float y1, int expand)
{
    unsigned char original[0x90];
    uint32_t quad = 0x2000;
    memset((void *)XBOX_PTR(quad), 0x5a, sizeof original);
    for (int i = 0; i < 4; i++) {
        float x = (i == 0 || i == 3) ? x0 : x1;
        float y = i < 2 ? y0 : y1;
        put_float(quad + 0x10 + i * 16, x);
        put_float(quad + 0x14 + i * 16, y);
        expected_x[i] = expand == 1 ? 320.0f + (x - 320.0f) * (4.0f / 3.0f) :
            expand == -1 ? x - 640.0f / 6.0f :
            expand == 2 ? x + 640.0f / 6.0f : x;
        expected_y[i] = y;
    }
    memcpy(original, (const void *)XBOX_PTR(quad), sizeof original);
    g_esp = 0x1000;
    MEM32(g_esp + 4) = quad;
    if (draw_feng) {
        cached_expected_shift = expand == -1 ? -640.0f / 6.0f : expand == 2 ? 640.0f / 6.0f : 0.0f;
        g_ecx = 0x6000;
        MEM32(0x6018) = quad - 0x10;
        MEM32(quad - 0x10) = 0x6018;
    }
    int before = draws;
    sub_00016460();
    assert(draws == before + 1 && g_esp == 0x1004);
    assert(memcmp(original, (const void *)XBOX_PTR(quad), sizeof original) == 0);
}

int main(int argc, char **argv)
{
    wide = argc == 1 || strcmp(argv[1], "4:3") != 0;
    int enabled = wide && getenv("RECOMP_HORPLUS") == NULL;
    g_xbox_mem_offset = (ptrdiff_t)ram;
    MEM32(0x263EBC) = 0x3000;
    MEM32(0x3004) = 0;
    MEM32(0x283434) = 3;
    quad_case(-20, 660, -10, 490, 0); /* oversized menu backdrop */
    quad_case(-320, 260, 0, 480, 0); /* pause panel extending off-screen */
    quad_case(510, 590, 400, 480, 0);      /* circular HUD icon */
    quad_case(100, 540, 120, 340, 0);      /* modal stays with its text */
    draw_feng = 1;
    quad_case(12, 170, 15, 100, 0); /* left frame corner */
    quad_case(565, 615, 20, 25, 0);  /* right frame dash */
    quad_case(500, 625, 405, 465, 0); /* bottom corner */
    quad_case(12, 268, 15, 143, 0); /* transparent corner canvas */
    MEM32(0x283434) = 6;
    quad_case(25, 175, 20, 100, enabled ? -1 : 0); /* lap/timer */
    quad_case(450, 600, 330, 470, enabled ? 2 : 0); /* gauge */
    quad_case(400, 620, 20, 180, enabled ? 2 : 0); /* leaderboard */
    quad_case(35, 370, 400, 455, enabled ? -1 : 0); /* whole music banner */
    quad_case(290, 350, 200, 260, 0); /* centred notification */
    /* Glyphs on opposite sides must share the text object's anchor.
     * The cached object spans over 480 pixels: it must not be expanded. */
    for (int i = 0; i < 4; i++) {
        put_float(0x2010 + i * 16, i == 0 || i == 3 ? 15 : 25);
        put_float(0x2014 + i * 16, i < 2 ? 30 : 50);
        put_float(0x3010 + i * 16, i == 0 || i == 3 ? 510 : 520);
        put_float(0x3014 + i * 16, i < 2 ? 30 : 50);
    }
    MEM32(0x6018) = 0x1ff0;
    MEM32(0x1ff0) = 0x2ff0;
    MEM32(0x2ff0) = 0x6018;
    unsigned char saved_first[0x90], saved_last[0x90];
    memcpy(saved_first, (void *)XBOX_PTR(0x2000), sizeof saved_first);
    memcpy(saved_last, (void *)XBOX_PTR(0x3000), sizeof saved_last);
    cached_expected_shift = enabled ? -640.0f / 6.0f : 0;
    g_ecx = 0x6000; g_esp = 0x1000;
    int before = draws;
    sub_00016460();
    assert(draws == before + 2 && g_esp == 0x1004);
    assert(!memcmp(saved_first, (void *)XBOX_PTR(0x2000), sizeof saved_first));
    assert(!memcmp(saved_last, (void *)XBOX_PTR(0x3000), sizeof saved_last));
    /* Post-race menus still render in game flow 6. Their title, body,
     * footer and separate bevels must all undo the HUD projection. */
    draw_menu = 1;
    quad_case(40, 600, 95, 360, enabled ? 1 : 0); /* reward backdrop */
    quad_case(565, 585, 95, 360, enabled ? 1 : 0); /* separate right bevel */
    quad_case(70, 180, 330, 360, enabled ? 1 : 0); /* Next */
    quad_case(210, 290, 330, 360, enabled ? 1 : 0); /* Back: opposite anchor */
    quad_case(420, 660, 280, 480, enabled ? 1 : 0); /* result menu */
    quad_case(405, 430, 415, 480, enabled ? 1 : 0); /* Accept icon */
    draw_feng = 0;
    quad_case(-20, 660, -10, 490, enabled ? 1 : 0); /* uncached menu backdrop */
    draw_feng = 1;
    MEM32(0x283434) = 3;
    quad_case(12, 170, 15, 100, 0); /* FE package corner joins */
    draw_menu = 0;
    MEM32(0x283434) = 6;
    quad_case(25, 175, 20, 100, enabled ? -1 : 0); /* HUD scope restored */
    draw_feng = 0;
    quad_case(510, 590, 400, 480, 0); /* no FEng context leaks */
    draw_hud = 1;
    for (int state = 3; state <= 6; state++) {
        MEM32(0x283434) = state;
        for (int i = 0; i < 3; i++) {
            float x = i == 0 ? 100.0f : i == 1 ? 320.0f : 540.0f;
            expected_hud = x;
            if (enabled && state == 6 && i != 1)
                expected_hud += i == 0 ? -640.0f / 6.0f : 640.0f / 6.0f;
            g_ecx = 0x5000; g_esp = 0x1000;
            put_float(g_ecx + 0x68, x);
            sub_00016460();
            assert(g_esp == 0x1008);
            assert(read_float(0x5068) == x);
        }
    }
    draw_hud = 0;
    MEM32(0x283434) = 4;
    quad_case(-20, 660, -10, 490, 0);     /* loading backdrop stays fullscreen */
    MEM32(0x283434) = 3;
    g_esp = 0x1000;
    sub_000192C0();                      /* unrelated draws outside the UI pass */
    assert(g_esp == 0x1004);
    for (int state = 3; state <= 6; state++) {
        MEM32(0x283434) = state;
        g_esp = 0x1000; g_ecx = 0x4000;
        MEM32(g_esp) = 0x16548;
        put_float(g_ecx + 0x40, 1.0f);
        expected_projection = enabled && state == 6 ? 0.75f : 1.0f;
        sub_00018E40();
        assert(g_esp == 0x1004);
    }
    puts("UI aspect: draw coordinates, source restoration, ABI and exclusions passed");
    return 0;
}
