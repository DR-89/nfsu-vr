#pragma once
#include "../quest/runtime/quest_frame_timing.h"

static inline int nfsu1_timing_mode(int xr, unsigned flow, unsigned refresh, int fast60)
{
    if (!xr || flow != 6) return 0;
    if (refresh == 72 || refresh == 80 || refresh == 90 || refresh == 120) return 2;
    return fast60 ? 1 : 0;
}
static inline float nfsu1_timing_minimum(float original, int mode)
{
    return mode == 2 ? quest_sim_minimum_xr(original) :
           mode == 1 ? quest_sim_minimum_60hz(original) : original;
}
static inline uint32_t nfsu1_timing_present(uint32_t original, int mode)
{
    return mode == 2 ? quest_present_xr(original) :
           mode == 1 ? quest_present_60hz(original) : original;
}
/* 1410D0 rounds the 3600-Hz tick output again to a 4000-Hz grid before
 * computing carry. At high presentation rates that produces rate-dependent
 * drift. Feed back the actual emitted duration, keeping the existing tick
 * quantizer, time scale and maximum-step clamp. Retail/60-Hz stay unchanged. */
static inline float nfsu1_timing_carry(float elapsed, double rounded, double emitted, int mode)
{
    return (float)(elapsed - (mode == 2 ? emitted : rounded));
}
