#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "lg4ff_hid.h"
#include "quest_wheel_limits.h"

/* A USB racing wheel (Logitech G29 / G923 for PlayStation, third_party/lg4ff-hid)
 * as the shared Xbox packet that quest_gamepad_sample produces for a pad:
 * steering on the left stick, full scale at full lock (the caller encodes
 * NFSU1's stick deadzone as for the virtual wheel), throttle/brake on the
 * triggers, paddles on right stick up/down (gear shifts), Cross/Enter, Circle,
 * Square, Triangle on A/B/X/Y, L2/R2 on Black/White, L3/R3 on the stick clicks
 * (rear view / camera), Options/Share on Start/Back. Returns 2 while the wheel
 * is being used, otherwise 1, like the pad sampler. */
static inline int quest_wheel_device_sample(const LG4FF_State *s,float sensitivity,
    uint16_t *buttons,uint8_t analog[8],int16_t sticks[4],int clicks[2])
{
    static const uint16_t dpad[8]={0x1,0x9,0x8,0xA,0x2,0x6,0x4,0x5};
    uint32_t b=s->buttons;
    *buttons=0;memset(analog,0,8);memset(sticks,0,8);
    if(s->dpad>=0 && s->dpad<8)*buttons|=dpad[s->dpad];
    if(b&LG4FF_BUTTON_OPTIONS)*buttons|=0x10;
    if(b&LG4FF_BUTTON_SHARE)*buttons|=0x20;
    const uint32_t face[6]={LG4FF_BUTTON_CROSS|LG4FF_BUTTON_ENTER,LG4FF_BUTTON_CIRCLE,LG4FF_BUTTON_SQUARE,
        LG4FF_BUTTON_TRIANGLE,LG4FF_BUTTON_L2,LG4FF_BUTTON_R2};
    for(int i=0;i<6;++i)analog[i]=(b&face[i])?255:0;
    analog[6]=(uint8_t)lrintf(fminf(1,fmaxf(0,s->brake))*255);
    analog[7]=(uint8_t)lrintf(fminf(1,fmaxf(0,s->throttle))*255);
    clicks[0]=(b&LG4FF_BUTTON_L3)!=0;clicks[1]=(b&LG4FF_BUTTON_R3)!=0;
    float steering=isfinite(s->steering)?fminf(1,fmaxf(-1,s->steering*sensitivity)):0;
    sticks[0]=(int16_t)lrintf(steering*(steering<0?32768.0f:32767.0f));
    sticks[3]=(b&LG4FF_BUTTON_PADDLE_RIGHT)?32767:(b&LG4FF_BUTTON_PADDLE_LEFT)?-32767:0;
    int used=*buttons || clicks[0] || clicks[1] || sticks[3] || fabsf(s->steering)>.05f;
    for(int i=0;i<8;++i)used|=analog[i]>20;
    return used?2:1;
}
/* The real rim's lock-to-lock rotation follows the virtual wheel's setting;
 * the wheel supports 40..900 degrees. Its centering spring follows the wheel
 * return speed: 0 turns it off, the maximum is the motor's full spring. */
static inline int quest_wheel_device_range(float rotation)
{
    return isfinite(rotation)?(int)lrintf(fminf(900,fmaxf(40,rotation))):900;
}
static inline float quest_wheel_device_spring(float return_speed)
{
    return isfinite(return_speed)?fminf(1,fmaxf(0,return_speed/QUEST_WHEEL_RETURN_MAX)):0;
}
