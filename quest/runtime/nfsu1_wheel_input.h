#pragma once
#include <math.h>
#include <stdint.h>

/* The supported NFSU1 Xbox executable removes 24% from each stick axis in
 * 14940, before its steering scanner runs. Encode the virtual wheel through
 * the inverse of that filter. Real sticks, menus and inactive wheel input
 * keep their original packets. The scanner's small center band is retained
 * to reject tracking noise; sensitivity and wheel animation stay physical.
 * Constants are verified against the privately generated retail routines by
 * nfsu1_steering_test. This is title-specific, not a generic XR deadzone. */
static inline int16_t nfsu1_wheel_packet(int16_t steering, int wheel_owned)
{
    if (!wheel_owned || !steering) return steering;
    float normalized = steering / (steering < 0 ? 32768.0f : 32767.0f);
    float encoded = copysignf(.24f + .76f * fabsf(normalized), normalized);
    /* Retail normalization is (signed_axis + .5) / 32767.5. */
    long packet = lrintf(encoded * 32767.5f - .5f);
    if (packet < -32768) packet = -32768;
    if (packet > 32767) packet = 32767;
    return (int16_t)packet;
}
