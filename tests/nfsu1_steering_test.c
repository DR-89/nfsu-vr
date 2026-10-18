/* Exercise the actual lifted retail stick filter, byte packing, steering
 * curve and event scanner. Only the Xbox device service is stubbed. */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "../quest/runtime/quest_wheel_grip.h"
#include "../quest/runtime/nfsu1_wheel_input.h"
#include <stdio.h>
#include <stdlib.h>

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top,g_fp_cmp;
RECOMP_TLS uint16_t g_fp_cc,g_fp_control_word;
RECOMP_TLS RecompXmm g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7;
volatile int g_gil_contended;
void recomp_preempt(void) { abort(); }
static int16_t input;
void sub_001ECF50(void) {
    MEM32(MEM32(g_esp+8))=0; MEM32(MEM32(g_esp+12))=0;
    g_esp+=16;
}
void sub_001ED013(void) { abort(); }
void sub_001ECFBD(void) { abort(); }
void sub_001ED01F(void) { abort(); }
void sub_001ED1F7(void) {
    uint32_t packet=MEM32(g_esp+8);
    memset((void *)XBOX_PTR(packet),0,22);
    MEM16(packet+14)=(uint16_t)input;
    g_eax=0;g_esp+=12;
}
#include "steering_generated.inc"

static unsigned checks;
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } ++checks; } while(0)
static unsigned scan(int16_t axis)
{
    input=axis;
    g_esp=0x30000;MEM32(g_esp)=0x1234;
    g_fp_top=3;g_fp_control_word=0x37f;
    MEM32(0x260eb4)=0;
    sub_00014330();
    if(g_esp!=0x30004 || g_fp_top!=3) abort();
    g_esp=0x30000;MEM32(g_esp)=0x1234;
    MEM32(g_esp+4)=0x33000; /* scanner */
    MEM32(g_esp+8)=0x260e30; /* packed input */
    MEM32(g_esp+12)=0;
    MEM32(g_esp+16)=0x34000; /* binding */
    MEM32(g_esp+20)=0x35000; /* curves */
    MEM32(0x3301c)=1; /* force initial event */
    sub_00131140();
    if(g_esp!=0x30004 || g_fp_top!=3 || (g_eax&255)!=0x88) abort();
    return (g_eax>>8)&255;
}

int main(void)
{
    unsigned char *memory=calloc(1,0x400000);CHECK(memory);
    g_xbox_mem_offset=(ptrdiff_t)memory;
    retail_constants();
    MEM32(0x260eb0)=0x36000;MEM32(0x360d0)=1;
    MEM32(0x33008)=0x88;MEM32(0x34008)=0x88;
    MEM8(0x34012)=80;MEM8(0x34015)=1;
    MEM32(0x35054)=0x35100;MEM32(0x35058)=4;
    /* Steering curve read from the running title's type-0x41 device. */
    const float curve[]={0,0,126,128,130,128,255,255};
    memcpy((void *)XBOX_PTR(0x35100),curve,sizeof curve);
    CHECK(scan(0)==128);
    QuestWheelGrip wheel={0};wheel.latched=1;wheel.limit=QUEST_WHEEL_PI/2;wheel.sensitivity=2;
    for(int sign=-1;sign<=1;sign+=2) {
        for(int degrees=2;degrees<=10;++degrees) {
            wheel.angle=sign*degrees*QUEST_WHEEL_PI/180;
            int16_t raw=quest_wheel_input(&wheel,1,0);
            unsigned before=scan(raw),after=scan(nfsu1_wheel_packet(raw,1));
            CHECK(before==128);
            CHECK(sign<0?after<128:after>128);
            if(degrees==2 || degrees==10)printf("%+d deg: retail=%u corrected=%u\n",sign*degrees,before,after);
        }
    }
    unsigned previous=0;
    for(int axis=-32768;axis<=32767;++axis) {
        int16_t mapped=nfsu1_wheel_packet((int16_t)axis,1);
        CHECK(nfsu1_wheel_packet((int16_t)axis,0)==axis);
        unsigned value=scan(mapped);
        CHECK(value>=previous);previous=value;
        CHECK(axis<0?value<=128:value>=128);
    }
    CHECK(scan(nfsu1_wheel_packet(-32768,1))==0);
    CHECK(nfsu1_wheel_packet(-32768,1)==-32768);
    CHECK(nfsu1_wheel_packet(32767,1)==32767);
    CHECK(scan(nfsu1_wheel_packet(32767,1))>=250);
    CHECK(nfsu1_wheel_packet(0,1)==0);
    /* Tracking noise around center stays inside the original fine filter. */
    for(int axis=-100;axis<=100;++axis)CHECK(scan(nfsu1_wheel_packet((int16_t)axis,1))==128);
    free(memory);
    printf("NFSU1 steering: %u retail-path, range, center and passthrough checks passed.\n",checks);
    return 0;
}
