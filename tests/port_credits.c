/* cc -O1 -D__ANDROID__ -ffunction-sections -fdata-sections -Isrc
 * -Iandroid/app/build/generated/recomp tests/port_credits.c
 * -Wl,--gc-sections -lm -o /tmp/nfsu-port-credits-test */
#include <assert.h>
#include "../src/recomp_manual.c"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
RECOMP_TLS uint32_t g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp;
static unsigned char ram[0x300000];
static unsigned opened, forwarded, captions;
static const uint32_t screen = 0x20000, object = 0x21000, label = 0x22000;

void android_show_port_credits(void) { opened++; }
void sub_000C1FD0_gen(void) { forwarded++; g_esp += 20; }
uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment)
{
    assert(size <= 0x800 && alignment == 16);
    return 0x100000;
}
void sub_000DC340(void)
{
    assert(MEM32(g_esp + 4) == 0x23000);
    assert(MEM32(g_esp + 8) == 0x8816789Fu);
    g_eax = object;
    g_esp += 4;
}
void sub_000DC410(void)
{
    assert(MEM32(g_esp + 4) == label);
    assert(!strcmp((const char *)XBOX_PTR(MEM32(g_esp + 8)), "ANDROID PORT"));
    captions++;
    g_esp += 4;
}
static void event(uint32_t message, uint32_t hash)
{
    g_ecx = screen; g_esp = 0x10000;
    MEM32(screen + 0xC) = 0x23000;
    MEM32(g_esp + 4) = message;
    MEM32(g_esp + 8) = object;
    MEM32(object + 0x10) = hash;
    sub_000C1FD0();
    assert(g_esp == 0x10014);
}
int main(void)
{
    g_xbox_mem_offset = (ptrdiff_t)ram;
    event(0x0C407210u, 0x8816789Fu);
    assert(opened == 1 && forwarded == 0);
    event(0x0C407210u, 0xFD8A3B6Du); /* CREDITS stays retail. */
    event(0xABC08912u, 0x8816789Fu); /* Focus must not open. */
    assert(opened == 1 && forwarded == 2);
    MEM32(object + 0x18) = 5;
    MEM32(object + 0x60) = label;
    MEM32(label + 0x18) = 2;
    g_eax = 123; g_edx = 456;
    event(0x35F8620Bu, 0x8816789Fu);
    assert(captions == 1 && forwarded == 3 && opened == 1);
    assert(g_eax == 123 && g_edx == 456 && g_ecx == screen);
    puts("port credits: activation, focus, retail fallback, caption and stack passed");
    return 0;
}
