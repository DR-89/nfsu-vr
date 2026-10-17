/* cc -O1 -ffunction-sections -fdata-sections -Isrc
 * -Iandroid/app/build/generated/recomp tests/options_blur.c
 * -Wl,--gc-sections -lm -o /tmp/nfsu-options-blur-test */
#include <assert.h>
#include <unistd.h>
#include "../src/recomp_manual.c"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
RECOMP_TLS uint32_t g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp;
static unsigned char ram[0x300000];
static unsigned calls;
#ifdef NFSU2_VULKAN
volatile int nv2a_vk_cube_maps, nv2a_vk_scale_pct, nv2a_vk_fxaa;
volatile int nv2a_vk_aniso, nv2a_vk_square;
#endif
void sub_00016050_gen(void)
{
    assert(MEM32(0x260CC0) == (s_blur ? 1u : 0u));
    assert(MEMF(0x21C0B4) == (s_blur ? .65f : 0.0f));
    assert(MEMF(0x264FA4) == (s_blur ? .75f : 0.0f));
    if (!s_refl)
        for (unsigned n = 0; n < 6; n++)
            assert(MEM8(ENVMAP_VIEW(10 + n) + 8) == 0);
    calls++;
}
int main(void)
{
    char directory[] = "/tmp/nfsu-options-blur-XXXXXX";
    assert(mkdtemp(directory) && chdir(directory) == 0);
    g_xbox_mem_offset = (ptrdiff_t)ram;
    assert(s_blur == 1);
    assert(!strcmp(s_rows[N_ROWS - 1].label, "Blur/Bloom:"));
    for (int reflections = 0; reflections < 2; reflections++) {
        s_refl = reflections;
        for (int enabled = 0; enabled < 2; enabled++) {
            s_blur = enabled;
            MEMF(0x21C0B4) = .65f;
            MEM32(0x260CC0) = 1;
            MEMF(0x264FA4) = .75f;
            for (unsigned n = 0; n < 6; n++)
                MEM8(ENVMAP_VIEW(10 + n) + 8) = n + 1;
            sub_00016050();
            assert(MEM32(0x260CC0) == 1);
            assert(MEMF(0x21C0B4) == .65f);
            for (unsigned n = 0; n < 6; n++)
                assert(MEM8(ENVMAP_VIEW(10 + n) + 8) == n + 1);
            options_save();
            s_blur = !enabled;
            nfsu1_options_load();
            assert(s_blur == enabled);
        }
    }
    assert(calls == 4);
    assert(unlink(NFSU1_OPTIONS_FILE) == 0);
    assert(chdir("/tmp") == 0 && rmdir(directory) == 0);
    puts("blur options persistence and render policy: OK");
    return 0;
}
