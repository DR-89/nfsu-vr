#include "../quest/runtime/quest_wheel_device.h"
#include <stdio.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);return 1;} } while(0)
static int sample(LG4FF_State s,float sensitivity,uint16_t *b,uint8_t a[8],int16_t st[4],int c[2])
{ return quest_wheel_device_sample(&s,sensitivity,b,a,st,c); }
int main(void)
{
    uint16_t b;uint8_t a[8];int16_t st[4];int c[2];
    LG4FF_State rest={0};rest.dpad=-1;
    /* At rest: neutral packet, not "used", pedals released. */
    CHECK(sample(rest,1,&b,a,st,c)==1);
    CHECK(!b && !c[0] && !c[1] && !st[0] && !st[1] && !st[2] && !st[3]);
    for(int i=0;i<8;++i)CHECK(!a[i]);
    /* Small rim noise does not select the wheel; a real turn does. */
    LG4FF_State s=rest;s.steering=.01f;
    CHECK(sample(s,1,&b,a,st,c)==1 && st[0]>0);
    s.steering=.2f;CHECK(sample(s,1,&b,a,st,c)==2);
    /* Full lock is full stick scale both ways; sensitivity scales and clamps. */
    s.steering=1;sample(s,1,&b,a,st,c);CHECK(st[0]==32767);
    s.steering=-1;sample(s,1,&b,a,st,c);CHECK(st[0]==-32768);
    s.steering=.5f;sample(s,2,&b,a,st,c);CHECK(st[0]==32767);
    s.steering=-.25f;sample(s,2,&b,a,st,c);CHECK(st[0]==-16384);
    s.steering=NAN;CHECK(sample(s,1,&b,a,st,c)==1 && st[0]==0);
    /* Pedals: brake on the left trigger, throttle on the right, as for a pad. */
    s=rest;s.throttle=1;s.brake=.5f;
    CHECK(sample(s,1,&b,a,st,c)==2 && a[7]==255 && a[6]==128);
    s.throttle=2;s.brake=-1;sample(s,1,&b,a,st,c);CHECK(a[7]==255 && a[6]==0);
    /* Paddles shift through right stick up/down (Y is positive upward). */
    s=rest;s.buttons=LG4FF_BUTTON_PADDLE_RIGHT;CHECK(sample(s,1,&b,a,st,c)==2 && st[3]==32767);
    s.buttons=LG4FF_BUTTON_PADDLE_LEFT;sample(s,1,&b,a,st,c);CHECK(st[3]==-32767);
    /* Face buttons in Xbox order A,B,X,Y,Black,White; Enter also accepts. */
    const uint32_t face[6]={LG4FF_BUTTON_CROSS,LG4FF_BUTTON_CIRCLE,LG4FF_BUTTON_SQUARE,
        LG4FF_BUTTON_TRIANGLE,LG4FF_BUTTON_L2,LG4FF_BUTTON_R2};
    for(int i=0;i<6;++i) {
        s.buttons=face[i];sample(s,1,&b,a,st,c);
        for(int j=0;j<8;++j)CHECK(a[j]==(j==i?255:0));
    }
    s.buttons=LG4FF_BUTTON_ENTER;sample(s,1,&b,a,st,c);CHECK(a[0]==255);
    /* Stick clicks (rear view, camera) and Start/Back. */
    s.buttons=LG4FF_BUTTON_L3|LG4FF_BUTTON_R3;sample(s,1,&b,a,st,c);CHECK(c[0] && c[1] && !b);
    s.buttons=LG4FF_BUTTON_OPTIONS;sample(s,1,&b,a,st,c);CHECK(b==0x10);
    s.buttons=LG4FF_BUTTON_SHARE;sample(s,1,&b,a,st,c);CHECK(b==0x20);
    /* PS, plus, minus and the dial are not mapped. */
    s.buttons=LG4FF_BUTTON_PS|LG4FF_BUTTON_PLUS|LG4FF_BUTTON_MINUS;
    CHECK(sample(s,1,&b,a,st,c)==1 && !b);
    /* Hat: 0 up, clockwise; the same bits as SDL's D-pad. */
    const uint16_t hat[8]={0x1,0x9,0x8,0xA,0x2,0x6,0x4,0x5};
    for(int i=0;i<8;++i){s=rest;s.dpad=i;CHECK(sample(s,1,&b,a,st,c)==2 && b==hat[i]);}
    /* Range follows the virtual wheel within 40..900; spring follows return speed. */
    CHECK(quest_wheel_device_range(360)==360 && quest_wheel_device_range(1080)==900);
    CHECK(quest_wheel_device_range(10)==40 && quest_wheel_device_range(NAN)==900);
    CHECK(quest_wheel_device_spring(0)==0 && quest_wheel_device_spring(QUEST_WHEEL_RETURN_MAX)==1);
    CHECK(quest_wheel_device_spring(QUEST_WHEEL_RETURN_DEFAULT)>.4f && quest_wheel_device_spring(QUEST_WHEEL_RETURN_DEFAULT)<.5f);
    CHECK(quest_wheel_device_spring(-5)==0 && quest_wheel_device_spring(NAN)==0);
    puts("quest_wheel_device_test: OK");
    return 0;
}
