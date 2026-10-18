/* Compile both actual lifted title routines from a private fixture. This
 * verifies physics cadence, ABI, maximum-step clamping and replay bypass. */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "../src/nfsu1_timing.h"
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
static int mode,record_count,replay_count;
static uint32_t recorded[2];
void sub_00138550(void) { g_eax=replay_count++==0?83:7;g_esp+=4; }
void sub_001385F0(void) { if(record_count<2)recorded[record_count]=MEM32(g_esp+4);++record_count;g_eax=0;g_esp+=4; }
#define NFSU1_VR_MINIMUM(v) nfsu1_timing_minimum(v,mode)
#define NFSU1_VR_CARRY(d,r,t) nfsu1_timing_carry(d,r,(double)(int32_t)(t)*MEMF(0x1fb030),mode)
#include "timing_generated.inc"

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } ++checks; } while(0)
static unsigned checks;
typedef struct { uint32_t ticks,ax,cx,dx,sp,bx,si,di;float dt,carry;int fp; } Result;
static int equal(Result a,Result b)
{
    return a.ticks==b.ticks && a.ax==b.ax && a.cx==b.cx && a.dx==b.dx &&
           a.sp==b.sp && a.bx==b.bx && a.si==b.si && a.di==b.di &&
           a.dt==b.dt && a.carry==b.carry && a.fp==b.fp;
}
static Result run(int updated,float dt,int replay)
{
    g_eax=0x11111111;g_ecx=0x22222222;g_edx=0x33333333;
    g_ebx=0x44444444;g_esi=0x55555555;g_edi=0x66666666;g_esp=0x30000;
    g_fp_top=3;g_fp_cmp=0;g_fp_cc=0;g_fp_control_word=0x37f;
    for(int i=0;i<8;++i)g_fp_stack[i]=i+.125;
    MEM32(g_esp)=0x4e4e4;MEMF(g_esp+4)=dt;MEM32(0x2ca14c)=replay;
    record_count=replay_count=0;
    if(updated)simulation_updated();else simulation_original();
    Result r={MEM32(0x2ca428),g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,MEMF(0x253164),MEMF(0x2ca430),g_fp_top};
    return r;
}
int main(void)
{
    unsigned char *memory=calloc(1,0x400000);CHECK(memory);g_xbox_mem_offset=(ptrdiff_t)memory;
    MEMF(0x1fb030)=1.0f/3600;MEMF(0x2079cc)=3600;
    MEMF(0x1f4da4)=4000;MEMF(0x1f432c)=.5f;MEMF(0x1f44cc)=.00025f;
    MEMF(0x207ab4)=1.016700029373169f;MEMF(0x1f8190)=1.0f/60;MEMF(0x1f84a8)=.02f;
    MEMF(0x25314c)=3;MEM32(0x2ca434)=0xab;
    CHECK(nfsu1_timing_mode(0,6,72,1)==0);
    CHECK(nfsu1_timing_mode(1,3,72,1)==0);
    CHECK(nfsu1_timing_mode(1,6,0,0)==0);
    CHECK(nfsu1_timing_mode(1,6,0,1)==1);
    CHECK(nfsu1_timing_mode(1,6,72,1)==2);
    CHECK(nfsu1_timing_mode(1,6,91,0)==0);
    for(int ntsc=0;ntsc<2;++ntsc) {
        MEM32(0x25313c)=ntsc;
        for(int i=0;i<300;++i) {
            float delta=i*.0005f;
            MEMF(0x2ca430)=.0001f;mode=0;Result original=run(0,delta,0);
            MEMF(0x2ca430)=.0001f;Result actual=run(1,delta,0);
            CHECK(equal(original,actual));
            CHECK(actual.sp==0x30004 && actual.bx==0x44444444 && actual.si==0x55555555 && actual.di==0x66666666 && actual.fp==3);
            CHECK(record_count==2 && recorded[0]==actual.ticks && recorded[1]==11);
            if(delta>=.021f) {
                MEMF(0x2ca430)=.0001f;mode=2;actual=run(1,delta,0);
                CHECK(original.ticks==actual.ticks && original.dt==actual.dt);
                CHECK(actual.sp==0x30004 && actual.fp==3);
            }
        }
        for(mode=0;mode<=2;++mode) {
            MEMF(0x2ca430)=.0001f;Result replay=run(1,0,1);
            CHECK(replay.ticks==83 && replay_count==2 && record_count==0 && replay.carry==.0001f);
        }
        const unsigned rates[]={72,80,90,120};
        mode=2;
        for(unsigned r=0;r<4;++r) {
            double elapsed=0,simulated=0;MEMF(0x2ca430)=0;
            unsigned frames=rates[r]*60;
            for(unsigned i=0;i<frames;++i) {
                float dt=1.0f/rates[r]+(i%3==0?-.002f:i%3==1?.002f:0);
                Result actual=run(1,dt,0);elapsed+=dt*MEMF(0x207ab4);simulated+=actual.dt;
                CHECK(actual.sp==0x30004 && actual.fp==3);
            }
            printf("%s %u Hz, 60 s: scaled elapsed %.6f, simulated %.6f, difference %.6f\n",ntsc?"NTSC":"PAL timing",rates[r],elapsed,simulated,simulated-elapsed);
            CHECK(fabs(simulated-elapsed)<.0004);
        }
    }
    free(memory);
    printf("NFSU1 timing: %u lifted-code cadence, ABI, replay and gating checks passed.\n",checks);
    return 0;
}
